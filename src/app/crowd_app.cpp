#include "app/crowd_app.h"

#include "app/fx_layer.h"
#include "app/sprite_layer.h"
#include "app/tools.h"
#include "app/ui_layer.h"
#include "core/diag.h"
#include "core/hash.h"
#include "core/jobs.h"
#include "core/png.h"
#include "core/profile.h"
#include "platform/platform.h"
#include "render/crowd_renderer.h"
#include "scene/scene.h"
#include "scene/test_spec.h"
#include "scene/ui_state.h"
#include "scene/world.h"
#include "script/script.h"
#include "ui/screen.h"
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
    SpriteLayer* sprites = nullptr;  // the scene's sprites, when it has any
    FxLayer* fx = nullptr;           // the scene's frame effects (HDR and bloom), when it has any
    DiagList* diags = nullptr;
};

void print_diags(Game& g) {
    diag_print_text(g.diags, stderr);
    g.diags->count = 0;
    g.diags->dropped = 0;
}

// The frame's clicks and back press through the navigation policy and the script's UI hooks.
// Returns false if a hook failed; other problems (an open:<screen> naming no screen) stay in g.diags.
bool handle_ui_events(Game& g, bool back_pressed) {
    if (!g.ui) return true;
    set_script(g.ui, g.script_paused ? nullptr : g.script);
    return handle_ui(g.ui, back_pressed, g.diags);
}

// Clicks "screen/node" on the UI; false (with a ZE0603 diagnostic) if there is no such button to click.
bool click_path(Game& g, const char* path, const char* file, int line) {
    char screen[64];
    const char* slash = strchr(path, '/');
    const size_t len = slash ? (size_t)(slash - path) : 0;
    if (slash && len < sizeof(screen)) {
        memcpy(screen, path, len);
        screen[len] = 0;
        if (g.ui && click(g.ui, screen, slash + 1)) return true;
    }
    diag_add(g.diags, kErrTestClick, file, line, line ? 1 : 0,
             "cannot click '%s': no such screen/node, or the button is hidden, disabled or behind a modal", path);
    return false;
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
    if (g.scene) {
        scene::flush_entities(g.scene);  // what scripts destroyed this tick (D-031)
        scene::update_transforms(g.scene);
    }
    return step_sprites(g.sprites, sim::tick(g.crowd), g.diags) && ok;
}

// The run's state: the crowd and the scene's gameplay data (scene::world_hash).
uint64_t game_state_hash(const Game& g) {
    const uint64_t crowd = sim::state_hash(g.crowd);
    if (!g.scene) return crowd;
    const uint64_t world = scene::world_hash(g.scene);
    return fnv1a(&world, sizeof(world), crowd);
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
                     const Game& g, rhi::Device* device, const FxLayer* fx) {
    const sim::Crowd* crowd = g.crowd;
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
    if (fx) fprintf(f, "  \"hdr\": \"%s\",\n", fx_format_name(fx));
    else fputs("  \"hdr\": null,\n", f);
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
    fprintf(f, "  \"state_hash\": \"%016llx\"\n", (unsigned long long)game_state_hash(g));
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
    snprintf(path, sizeof(path), "%ssprite.wgsl", dir);
    const uint64_t c = platform::file_mtime(path);
    snprintf(path, sizeof(path), "%spost.wgsl", dir);
    const uint64_t d = platform::file_mtime(path);
    const uint64_t ab = a > b ? a : b, cd = c > d ? c : d;
    return ab > cd ? ab : cd;
}

render::CrowdScene make_scene(const Game& g, const float* instances, float alpha, float zoom) {
    const sim::Crowd* crowd = g.crowd;
    render::CrowdScene scene;
    scene.enemy_instances = instances;
    scene.enemy_count = sim::enemy_count(crowd);
    sim::player_position(crowd, alpha, &scene.player_x, &scene.player_z);
    scene.arena_size = sim::arena_size(crowd);
    scene.time_seconds = (float)sim::tick(crowd) * sim::kTickSeconds;
    scene.zoom = zoom;
    scene.keep_depth = has_world_sprites(g.sprites);
    return scene;
}

// The frame's passes: the crowd and the scene's sprites in its view (into the HDR frame when the
// scene has effects, then bloom and tonemap), then the UI over both.
void render_frame(Game& g, rhi::Device* device, render::CrowdRenderer* renderer, render::CrowdScene scene) {
    scene.target = fx_begin_frame(g.fx);
    render::render_crowd(renderer, scene);
    if (g.sprites) {
        uint32_t w, h;
        rhi::target_size(device, &w, &h);
        float view_proj[16], right[3];
        render::crowd_camera(scene, w, h, view_proj, right);
        render_sprite_layer(g.sprites, view_proj, right, scene.target);
    }
    render_fx(g.fx);
    if (g.ui) render_ui_layer(g.ui);
}

// The GPU side of a run: the crowd renderer (with the crowd's sprites, their SVG art cooked first
// when it changed), then the UI and the scene's sprites. nullptr after printing what failed.
render::CrowdRenderer* attach_renderers(Game& g, rhi::Device* device, const char* dir) {
    render::CrowdSprites look;
    if (g.sprites && (!prepare_sprite_art(g.sprites, g.diags) || !load_crowd_sprites(g.sprites, &look))) {
        print_diags(g);
        return nullptr;
    }
    if (!attach_fx_gpu(g.fx, device, dir)) {
        free_crowd_sprites(&look);
        return nullptr;
    }
    const rhi::TextureFormat format = fx_scene_format(g.fx);
    const uint32_t* cells = nullptr;
    const uint32_t ncells = sim::obstacle_cells(g.crowd, &cells);
    render::CrowdRenderer* renderer = render::create_crowd_renderer(
        device, dir, sim::enemy_count(g.crowd), cells, ncells, look.frame_count ? &look : nullptr, format);
    free_crowd_sprites(&look);
    if (!renderer || (g.ui && !attach_gpu(g.ui, device, dir)) ||
        (g.sprites && !attach_sprite_gpu(g.sprites, device, dir, format))) {
        print_diags(g);
        render::destroy_crowd_renderer(renderer);
        return nullptr;
    }
    return renderer;
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
        char dir[512];
        shader_dir(dir, sizeof(dir));
        renderer = attach_renderers(g, device, dir);
        if (!renderer) {
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
        if (g.ui) {
            update_ui(g.ui, nullptr, (float)o.width, (float)o.height, (float)sim::kTickSeconds);
            if (frame < o.click_count && !click_path(g, o.clicks[frame], "--click", 0)) {
                print_diags(g);
                exit_code = 1;
                break;
            }
            if (!handle_ui_events(g, false) || g.diags->count) {
                print_diags(g);
                exit_code = 1;
                break;
            }
        }
        if (device) {
            const uint64_t t1 = platform::now_ticks();
            sim::write_enemy_instances(crowd, 1.0f, instances);
            if (rhi::begin_frame(device)) {
                render_frame(g, device, renderer, make_scene(g, instances, 1.0f, o.zoom));
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
    if (exit_code == 0 && o.perf_path && !write_perf_json(o.perf_path, o, stats, wall, g, device, g.fx)) {
        exit_code = 1;
    }
    printf("state_hash=%016llx ticks=%llu\n", (unsigned long long)game_state_hash(g),
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
    char dir[512];
    shader_dir(dir, sizeof(dir));
    render::CrowdRenderer* renderer = attach_renderers(g, device, dir);
    if (!renderer) {
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
    bool has_surface = true;
    while (!input.quit) {
        platform::poll_input(&input);
        // Mobile: the OS takes the window away in the background and may hand back a new one.
        if (input.background && has_surface) {
            rhi::set_window(device, nullptr);
            has_surface = false;
        }
        if (input.window_changed) {
            const platform::NativeWindow nw = platform::native_window(window);
            rhi::set_window(device, &nw);
            has_surface = true;
            last = platform::now_ticks();  // no catch-up ticks for the time away
        }
        if (input.background) continue;
        const uint64_t now = platform::now_ticks();
        const double frame_seconds = platform::ticks_to_seconds(now - last);
        accumulator += frame_seconds;
        last = now;
        platform::window_pixel_size(window, &pw, &ph);
        if (g.ui) {
            update_ui(g.ui, &input, (float)pw, (float)ph, (float)frame_seconds);
            if (!handle_ui_events(g, input.nav_back)) {
                print_diags(g);
                g.script_paused = true;
                log_info("script paused until %s is saved again", script::script_path(g.script));
            } else if (g.diags->count) {
                print_diags(g);
            }
            if (back_unhandled(g.ui)) break;  // back with nothing to close and nothing that took it quits
        } else if (input.nav_back) {
            break;
        }
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
            render_frame(g, device, renderer, make_scene(g, instances, alpha, o.zoom));
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
                const bool sprites = !g.sprites || reload_sprite_layer_shaders(g.sprites);
                const bool fx = reload_fx_shaders(g.fx);
                if (render::reload_shaders(renderer) && sprites && fx) log_info("shaders reloaded");
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
        bool sprites_failed = false;
        if (!ui_failed) g.sprites = create_sprite_layer(g.scene, g.diags, &sprites_failed);
        g.fx = create_fx_layer(g.scene);
        if (ui_failed || sprites_failed) {
            destroy_ui_layer(g.ui);
            g.ui = nullptr;
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
        sd.ui = ui_state(g.ui);
        sd.host = ui_host(g.ui);
        g.script = script::load_script(sd, g.diags);
        if (g.ui && g.script) set_script(g.ui, g.script);
        if (!g.script || !script::call_start(g.script, g.diags)) {
            script::destroy_script(g.script);
            g.script = nullptr;
            destroy_ui_layer(g.ui);
            g.ui = nullptr;
            destroy_sprite_layer(g.sprites);
            g.sprites = nullptr;
            destroy_fx_layer(g.fx);
            g.fx = nullptr;
            scene::free_scene(g.scene);
            g.scene = nullptr;
            return false;
        }
    }
    if (g.scene) scene::update_transforms(g.scene);
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
    destroy_sprite_layer(g.sprites);
    destroy_fx_layer(g.fx);
    scene::free_scene(g.scene);
    delete g.diags;
    jobs::shutdown();
    platform::shutdown();
    return code;
}

bool simulate_scene(const RunOptions& o, uint32_t ticks, const scene::TestSpec* spec, DiagList* diags,
                    scene::Scene** scene_out, UiLayer** ui_out, SimulateResult* out) {
    Game g;
    g.diags = diags;
    sim::CrowdConfig cfg;
    *scene_out = nullptr;
    *ui_out = nullptr;
    if (!setup_game(o, g, &cfg)) return false;
    jobs::init(o.threads);
    g.crowd = sim::create_crowd(cfg);
    bool ok = true;
    for (uint32_t t = 0; t < ticks && ok; ++t) {
        if (g.ui) {
            update_ui(g.ui, nullptr, (float)o.width, (float)o.height, (float)sim::kTickSeconds);
            bool back = false;
            for (uint32_t c = 0; spec && c < spec->click_count; ++c) {
                const scene::Click& k = spec->clicks[c];
                if (k.tick != t + 1) continue;
                if (k.back) {
                    back = true;
                    continue;
                }
                char path[132];
                snprintf(path, sizeof(path), "%s/%s", k.screen, k.node);
                ok = click_path(g, path, spec->path, k.line) && ok;
            }
            ok = handle_ui_events(g, back) && ok;
        } else if (spec && spec->click_count) {
            diag_add(diags, kErrTestClick, spec->path, spec->clicks[0].line, 1, "the scene has no ui to click");
            ok = false;
        }
        if (ok) ok = game_tick(g, sim::scripted_input(sim::tick(g.crowd)));
    }
    // Lay out once more so texts and rectangles reflect the last tick.
    if (g.ui) update_ui(g.ui, nullptr, (float)o.width, (float)o.height, (float)sim::kTickSeconds);
    out->state_hash = game_state_hash(g);
    out->ticks = sim::tick(g.crowd);
    sim::destroy_crowd(g.crowd);
    script::destroy_script(g.script);
    destroy_sprite_layer(g.sprites);  // the Sprite components keep their last frame in the scene
    destroy_fx_layer(g.fx);
    jobs::shutdown();
    *scene_out = g.scene;
    *ui_out = g.ui;
    return ok;
}

}  // namespace zaro::app
