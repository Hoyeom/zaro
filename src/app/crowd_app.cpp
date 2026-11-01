#include "app/crowd_app.h"

#include "app/tools.h"
#include "app/ui_layer.h"
#include "core/diag.h"
#include "core/jobs.h"
#include "core/png.h"
#include "core/profile.h"
#include "platform/platform.h"
#include "render/crowd_renderer.h"
#include "scene/scene.h"
#include "script/script.h"
#include "sim/crowd.h"
#include "sim/crowd_components.h"
#include "zaro/zaro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

// What one run owns: the simulation, and optionally the scene it came from and its script.
struct Game {
    sim::Crowd* crowd = nullptr;
    scene::Scene* scene = nullptr;
    script::Script* script = nullptr;
    bool script_paused = false;  // windowed: a failing script waits for the next reload
    UiLayer* ui = nullptr;       // the scene's screens, when it has any
    DiagList* diags = nullptr;
};

void print_diags(Game& g) {
    diag_print_text(g.diags, stderr);
    g.diags->count = 0;
    g.diags->dropped = 0;
}

// One fixed tick: the script's on_tick, then the values it may have changed, then the sim.
// Returns false if the script failed.
bool game_tick(Game& g, const sim::CrowdInput& input) {
    bool ok = true;
    if (g.script && !g.script_paused) {
        ok = script::call_tick(g.script, sim::tick(g.crowd) + 1, g.diags);
        const auto* spawner = (const sim::CrowdSpawner*)scene::first_component(g.scene, &sim::kCrowdSpawnerDesc, nullptr);
        const auto* player =
            (const sim::PlayerController*)scene::first_component(g.scene, &sim::kPlayerControllerDesc, nullptr);
        sim::set_tuning(g.crowd, spawner ? spawner->speed : sim::CrowdSpawner{}.speed,
                        player ? player->speed : sim::PlayerController{}.speed);
    }
    sim::step(g.crowd, input);
    return ok;
}

struct Stats {
    double* frame_ms = nullptr;
    uint32_t count = 0;
    double sim_ms = 0.0;
    double prep_ms = 0.0;
    double gpu_total_ms = 0.0;
    double gpu_pass_ms[rhi::kMaxTimedPasses] = {};
    const char* gpu_pass_names[rhi::kMaxTimedPasses] = {};
    uint32_t gpu_pass_count = 0;
    uint32_t gpu_samples = 0;
    uint32_t draw_calls = 0;
};

int compare_double(const void* a, const void* b) {
    const double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

void add_gpu_sample(Stats& s, const rhi::GpuTimings& t) {
    if (!t.valid) return;
    s.gpu_total_ms += t.total_ms;
    s.gpu_pass_count = t.pass_count;
    for (uint32_t i = 0; i < t.pass_count; ++i) {
        s.gpu_pass_names[i] = t.names[i];
        s.gpu_pass_ms[i] += t.ms[i];
    }
    s.gpu_samples++;
}

bool write_perf_json(const char* path, const RunOptions& o, const Stats& s, double wall_seconds,
                     const sim::Crowd* crowd, rhi::Device* device) {
    FILE* f = fopen(path, "w");
    if (!f) {
        log_error(kErrIo, "%s: cannot write perf report", path);
        return false;
    }
    double* sorted = (double*)malloc(sizeof(double) * (s.count ? s.count : 1));
    memcpy(sorted, s.frame_ms, sizeof(double) * s.count);
    qsort(sorted, s.count, sizeof(double), compare_double);
    double sum = 0.0;
    for (uint32_t i = 0; i < s.count; ++i) sum += sorted[i];
    const double avg = s.count ? sum / s.count : 0.0;
    const double p50 = s.count ? sorted[s.count / 2] : 0.0;
    const double p99 = s.count ? sorted[(uint32_t)((s.count - 1) * 0.99)] : 0.0;
    const double worst = s.count ? sorted[s.count - 1] : 0.0;
    free(sorted);

    const zaro_version_info v = zaro_get_version();
    fprintf(f, "{\n");
    fprintf(f, "  \"schema\": 1,\n");
    fprintf(f, "  \"zaro_version\": \"%u.%u.%u\",\n", v.major, v.minor, v.patch);
    fprintf(f, "  \"demo\": \"crowd\",\n");
    fputs("  \"scene\": ", f);
    if (o.scene_path) json_write_string(f, o.scene_path);
    else fputs("null", f);
    fputs(",\n", f);
    fprintf(f, "  \"mode\": \"%s\",\n", o.sim_only ? "sim-only" : (o.headless ? "headless" : "windowed"));
    fprintf(f, "  \"adapter\": \"%s\",\n", device ? rhi::adapter_name(device) : "");
    fprintf(f, "  \"backend\": \"%s\",\n", device ? rhi::backend_name(device) : "none");
    fprintf(f, "  \"resolution\": [%u, %u],\n", o.width, o.height);
    fprintf(f, "  \"threads\": %u,\n", jobs::thread_count());
    fprintf(f, "  \"frames\": %u,\n", s.count);
    fprintf(f, "  \"warmup_frames\": %u,\n", o.warmup);
    fprintf(f, "  \"wall_seconds\": %.3f,\n", wall_seconds);
    fprintf(f, "  \"fps\": %.2f,\n", avg > 0.0 ? 1000.0 / avg : 0.0);
    fprintf(f, "  \"frame_ms\": {\"avg\": %.3f, \"p50\": %.3f, \"p99\": %.3f, \"max\": %.3f},\n", avg, p50, p99, worst);
    fprintf(f, "  \"cpu_ms\": {\"sim\": %.3f, \"render_prep\": %.3f},\n", s.count ? s.sim_ms / s.count : 0.0,
            s.count ? s.prep_ms / s.count : 0.0);
    if (s.gpu_samples) {
        fprintf(f, "  \"gpu_ms\": {\"total\": %.3f", s.gpu_total_ms / s.gpu_samples);
        for (uint32_t i = 0; i < s.gpu_pass_count; ++i) {
            fprintf(f, ", \"%s\": %.3f", s.gpu_pass_names[i], s.gpu_pass_ms[i] / s.gpu_samples);
        }
        fprintf(f, "},\n");
    } else {
        fprintf(f, "  \"gpu_ms\": null,\n");
    }
    fprintf(f, "  \"draw_calls\": %u,\n", s.draw_calls);
    fprintf(f, "  \"entities\": %u,\n", sim::enemy_count(crowd) + 1);
    fprintf(f, "  \"enemies\": %u,\n", sim::enemy_count(crowd));
    fprintf(f, "  \"ticks\": %llu,\n", (unsigned long long)sim::tick(crowd));
    fprintf(f, "  \"hits\": %llu,\n", (unsigned long long)sim::total_hits(crowd));
    fprintf(f, "  \"seed\": %llu,\n", (unsigned long long)o.seed);
    fprintf(f, "  \"state_hash\": \"%016llx\"\n", (unsigned long long)sim::state_hash(crowd));
    fprintf(f, "}\n");
    return fclose(f) == 0;
}

void shader_dir(char* out, size_t size) { snprintf(out, size, "%sshaders/", platform::base_path()); }

uint64_t shader_mtime(const char* dir) {
    char path[600];
    snprintf(path, sizeof(path), "%scrowd_cull.wgsl", dir);
    const uint64_t a = platform::file_mtime(path);
    snprintf(path, sizeof(path), "%scrowd_draw.wgsl", dir);
    const uint64_t b = platform::file_mtime(path);
    return a > b ? a : b;
}

render::CrowdScene make_scene(const sim::Crowd* crowd, const float* instances, float alpha, float zoom) {
    render::CrowdScene scene;
    scene.enemy_instances = instances;
    scene.enemy_count = sim::enemy_count(crowd);
    sim::player_position(crowd, alpha, &scene.player_x, &scene.player_z);
    scene.arena_size = sim::arena_size(crowd);
    scene.time_seconds = (float)sim::tick(crowd) * sim::kTickSeconds;
    scene.zoom = zoom;
    return scene;
}

double ms_since(uint64_t start) { return platform::ticks_to_seconds(platform::now_ticks() - start) * 1000.0; }

int run_headless(const RunOptions& o, Game& g, float* instances) {
    sim::Crowd* crowd = g.crowd;
    rhi::Device* device = nullptr;
    render::CrowdRenderer* renderer = nullptr;
    if (!o.sim_only) {
        rhi::DeviceDesc dd;
        dd.width = o.width;
        dd.height = o.height;
        dd.backend = o.backend;
        device = rhi::create_device(dd);
        if (!device) return 1;
        const uint32_t* cells = nullptr;
        const uint32_t ncells = sim::obstacle_cells(crowd, &cells);
        char dir[512];
        shader_dir(dir, sizeof(dir));
        renderer = render::create_crowd_renderer(device, dir, sim::enemy_count(crowd), cells, ncells);
        if (!renderer || (g.ui && !attach_gpu(g.ui, device, dir))) {
            render::destroy_crowd_renderer(renderer);
            rhi::destroy_device(device);
            return 1;
        }
    }

    Stats stats;
    stats.frame_ms = (double*)calloc(o.frames ? o.frames : 1, sizeof(double));
    const uint32_t total = o.warmup + o.frames;
    uint64_t measure_start = platform::now_ticks();
    int exit_code = 0;
    for (uint32_t frame = 0; frame < total; ++frame) {
        if (frame == o.warmup) measure_start = platform::now_ticks();
        const uint64_t t0 = platform::now_ticks();
        if (!game_tick(g, sim::scripted_input(sim::tick(crowd)))) {
            print_diags(g);
            exit_code = 1;
            break;
        }
        const double sim_ms = ms_since(t0);
        double prep_ms = 0.0;
        if (g.ui) update_ui(g.ui, nullptr, (float)o.width, (float)o.height, (float)sim::kTickSeconds);
        if (device) {
            const uint64_t t1 = platform::now_ticks();
            sim::write_enemy_instances(crowd, 1.0f, instances);
            if (rhi::begin_frame(device)) {
                render::render_crowd(renderer, make_scene(crowd, instances, 1.0f, o.zoom));
                if (g.ui) render_ui_layer(g.ui);
                prep_ms = ms_since(t1);
                rhi::end_frame(device);
            }
            if (rhi::device_failed(device)) {
                exit_code = 1;
                break;
            }
        }
        ZARO_FRAME_MARK();
        if (frame >= o.warmup) {
            stats.frame_ms[stats.count++] = ms_since(t0);
            stats.sim_ms += sim_ms;
            stats.prep_ms += prep_ms;
            if (device) {
                add_gpu_sample(stats, rhi::last_gpu_timings(device));
                stats.draw_calls = rhi::last_draw_calls(device);
            }
        }
    }
    if (device) rhi::wait_idle(device);
    const double wall = platform::ticks_to_seconds(platform::now_ticks() - measure_start);

    if (exit_code == 0 && device && o.screenshot_path) {
        uint8_t* pixels = (uint8_t*)malloc((size_t)o.width * o.height * 4);
        if (!rhi::capture_rgba8(device, pixels) || !write_png_rgba8(o.screenshot_path, o.width, o.height, pixels)) {
            log_error(kErrIo, "%s: cannot write screenshot", o.screenshot_path);
            exit_code = 1;
        }
        free(pixels);
    }
    if (exit_code == 0 && o.perf_path && !write_perf_json(o.perf_path, o, stats, wall, crowd, device)) exit_code = 1;
    printf("state_hash=%016llx ticks=%llu\n", (unsigned long long)sim::state_hash(crowd),
           (unsigned long long)sim::tick(crowd));
    free(stats.frame_ms);
    render::destroy_crowd_renderer(renderer);
    rhi::destroy_device(device);
    return exit_code;
}

int run_windowed(const RunOptions& o, Game& g, float* instances) {
    sim::Crowd* crowd = g.crowd;
    platform::Window* window = platform::create_window("ZARO crowd", (int)o.width, (int)o.height);
    if (!window) return 1;
    int pw = 0, ph = 0;
    platform::window_pixel_size(window, &pw, &ph);
    const platform::NativeWindow native = platform::native_window(window);
    rhi::DeviceDesc dd;
    dd.window = &native;
    dd.width = (uint32_t)pw;
    dd.height = (uint32_t)ph;
    dd.vsync = o.vsync;
    dd.backend = o.backend;
    rhi::Device* device = rhi::create_device(dd);
    if (!device) {
        platform::destroy_window(window);
        return 1;
    }
    const uint32_t* cells = nullptr;
    const uint32_t ncells = sim::obstacle_cells(crowd, &cells);
    char dir[512];
    shader_dir(dir, sizeof(dir));
    render::CrowdRenderer* renderer = render::create_crowd_renderer(device, dir, sim::enemy_count(crowd), cells, ncells);
    if (!renderer || (g.ui && !attach_gpu(g.ui, device, dir))) {
        render::destroy_crowd_renderer(renderer);
        rhi::destroy_device(device);
        platform::destroy_window(window);
        return 1;
    }

    uint64_t shaders_mtime = shader_mtime(dir);
    uint64_t script_mtime = g.script ? platform::file_mtime(script::script_path(g.script)) : 0;
    uint64_t last = platform::now_ticks();
    uint64_t title_timer = last;
    double accumulator = 0.0, sim_ms_acc = 0.0;
    uint32_t frames = 0, ticks = 0;
    int exit_code = 0;
    platform::Input input;
    while (!input.quit) {
        platform::poll_input(&input);
        const uint64_t now = platform::now_ticks();
        const double frame_seconds = platform::ticks_to_seconds(now - last);
        accumulator += frame_seconds;
        last = now;
        platform::window_pixel_size(window, &pw, &ph);
        if (g.ui) update_ui(g.ui, &input, (float)pw, (float)ph, (float)frame_seconds);
        if (input.nav_back && !ui_used_back(g.ui)) break;  // back with nothing to close quits
        // Fixed tick; never run more than 5 ticks in one frame (spiral-of-death guard).
        uint32_t steps = 0;
        while (accumulator >= sim::kTickSeconds && steps < 5) {
            const uint64_t t0 = platform::now_ticks();
            if (!game_tick(g, sim::CrowdInput{input.move_x, -input.move_y})) {
                print_diags(g);
                g.script_paused = true;
                log_info("script paused until %s is saved again", script::script_path(g.script));
            }
            sim_ms_acc += ms_since(t0);
            accumulator -= sim::kTickSeconds;
            ++steps;
            ++ticks;
        }
        if (steps == 5) accumulator = 0.0;
        const float alpha = (float)(accumulator / sim::kTickSeconds);

        rhi::resize(device, (uint32_t)pw, (uint32_t)ph);
        sim::write_enemy_instances(crowd, alpha, instances);
        if (rhi::begin_frame(device)) {
            render::render_crowd(renderer, make_scene(crowd, instances, alpha, o.zoom));
            if (g.ui) render_ui_layer(g.ui);
            rhi::end_frame(device);
        }
        ZARO_FRAME_MARK();
        if (rhi::device_failed(device)) {
            exit_code = 1;
            break;
        }
        ++frames;

        const double since_title = platform::ticks_to_seconds(now - title_timer);
        if (since_title >= 0.5) {
            const rhi::GpuTimings gt = rhi::last_gpu_timings(device);
            char title[256];
            snprintf(title, sizeof(title), "ZARO crowd | %u enemies | %.0f fps | sim %.2f ms | gpu %.2f ms | %s",
                     sim::enemy_count(crowd), frames / since_title, ticks ? sim_ms_acc / ticks : 0.0,
                     gt.valid ? gt.total_ms : 0.0, rhi::backend_name(device));
            platform::set_window_title(window, title);
            title_timer = now;
            frames = ticks = 0;
            sim_ms_acc = 0.0;
            // Shader hot reload: rebuild with `python tools/build.py` while this window runs.
            const uint64_t m = shader_mtime(dir);
            if (m != shaders_mtime) {
                shaders_mtime = m;
                rhi::wait_idle(device);
                if (render::reload_shaders(renderer)) log_info("shaders reloaded");
            }
            // Script hot reload: save the .luau file while this window runs.
            if (g.script) {
                const uint64_t sm = platform::file_mtime(script::script_path(g.script));
                if (sm != script_mtime) {
                    script_mtime = sm;
                    if (script::reload(g.script, g.diags)) {
                        g.script_paused = false;
                        log_info("script reloaded");
                    } else {
                        print_diags(g);
                    }
                }
            }
        }
    }
    render::destroy_crowd_renderer(renderer);
    rhi::destroy_device(device);
    platform::destroy_window(window);
    return exit_code;
}

}  // namespace

namespace {

// Loads the scene (if any), derives the simulation config, then loads the script and runs its
// on_start. Errors stay in g.diags. On failure everything already loaded is released.
bool setup_game(const RunOptions& o, Game& g, sim::CrowdConfig* cfg) {
    if (o.scene_path) {
        register_all_components();
        g.scene = scene::load_scene(o.scene_path, g.diags);
        if (!g.scene) return false;
        if (const auto* a = (const sim::CrowdArena*)scene::first_component(g.scene, &sim::kCrowdArenaDesc, nullptr)) {
            cfg->grid_size = a->grid_size;
            cfg->seed = a->seed;
        }
        if (const auto* sp = (const sim::CrowdSpawner*)scene::first_component(g.scene, &sim::kCrowdSpawnerDesc, nullptr)) {
            cfg->enemy_count = sp->count;
            cfg->enemy_speed = sp->speed;
            cfg->enemy_radius = sp->radius;
        }
        if (const auto* p =
                (const sim::PlayerController*)scene::first_component(g.scene, &sim::kPlayerControllerDesc, nullptr)) {
            cfg->player_speed = p->speed;
        }
    }
    if (!o.scene_path || o.enemies_set) cfg->enemy_count = o.enemies;
    if (!o.scene_path || o.seed_set) cfg->seed = o.seed;
    // Keep the scene in step with CLI overrides, so scripts read the values actually used.
    if (g.scene && o.enemies_set) {
        if (auto* sp = (sim::CrowdSpawner*)scene::first_component_mut(g.scene, &sim::kCrowdSpawnerDesc)) {
            sp->count = o.enemies;
        }
    }
    if (g.scene && o.seed_set) {
        if (auto* a = (sim::CrowdArena*)scene::first_component_mut(g.scene, &sim::kCrowdArenaDesc)) a->seed = o.seed;
    }
    if (g.scene) {
        bool ui_failed = false;
        g.ui = create_ui_layer(g.scene, o.language, g.diags, &ui_failed);
        if (ui_failed) {
            scene::free_scene(g.scene);
            g.scene = nullptr;
            return false;
        }
    }
    if (g.scene && g.scene->script[0]) {
        script::ScriptDesc sd;
        sd.path = g.scene->script;
        sd.scene = g.scene;
        sd.seed = cfg->seed;
        g.script = script::load_script(sd, g.diags);
        if (!g.script || !script::call_start(g.script, g.diags)) {
            script::destroy_script(g.script);
            g.script = nullptr;
            destroy_ui_layer(g.ui);
            g.ui = nullptr;
            scene::free_scene(g.scene);
            g.scene = nullptr;
            return false;
        }
    }
    return true;
}

}  // namespace

int run_crowd(const RunOptions& o) {
    const bool headless = o.headless || o.sim_only;
    Game g;
    g.diags = new DiagList();
    sim::CrowdConfig cfg;
    if (!setup_game(o, g, &cfg)) {
        diag_print_text(g.diags, stderr);
        delete g.diags;
        return 1;
    }
    if (!platform::init(headless)) return 1;
    jobs::init(o.threads);
    g.crowd = sim::create_crowd(cfg);
    float* instances = (float*)malloc(sizeof(float) * 4 * (cfg.enemy_count ? cfg.enemy_count : 1));

    RunOptions effective = o;
    effective.enemies = cfg.enemy_count;
    effective.seed = cfg.seed;
    const int code = headless ? run_headless(effective, g, instances) : run_windowed(effective, g, instances);

    free(instances);
    sim::destroy_crowd(g.crowd);
    script::destroy_script(g.script);
    destroy_ui_layer(g.ui);
    scene::free_scene(g.scene);
    delete g.diags;
    jobs::shutdown();
    platform::shutdown();
    return code;
}

bool simulate_scene(const RunOptions& o, uint32_t ticks, DiagList* diags, scene::Scene** scene_out,
                    SimulateResult* out) {
    Game g;
    g.diags = diags;
    sim::CrowdConfig cfg;
    *scene_out = nullptr;
    if (!setup_game(o, g, &cfg)) return false;
    jobs::init(o.threads);
    g.crowd = sim::create_crowd(cfg);
    bool ok = true;
    for (uint32_t t = 0; t < ticks && ok; ++t) ok = game_tick(g, sim::scripted_input(sim::tick(g.crowd)));
    out->state_hash = sim::state_hash(g.crowd);
    out->ticks = sim::tick(g.crowd);
    sim::destroy_crowd(g.crowd);
    script::destroy_script(g.script);
    destroy_ui_layer(g.ui);
    jobs::shutdown();
    *scene_out = g.scene;
    return ok;
}

}  // namespace zaro::app
