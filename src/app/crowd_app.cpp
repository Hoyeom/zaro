#include "app/crowd_app.h"

#include "core/jobs.h"
#include "core/log.h"
#include "core/png.h"
#include "core/profile.h"
#include "platform/platform.h"
#include "render/crowd_renderer.h"
#include "sim/crowd.h"
#include "zaro/zaro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

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

int run_headless(const RunOptions& o, sim::Crowd* crowd, float* instances) {
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
        sim::step(crowd, sim::scripted_input(sim::tick(crowd)));
        const double sim_ms = ms_since(t0);
        double prep_ms = 0.0;
        if (device) {
            const uint64_t t1 = platform::now_ticks();
            sim::write_enemy_instances(crowd, 1.0f, instances);
            if (rhi::begin_frame(device)) {
                render::render_crowd(renderer, make_scene(crowd, instances, 1.0f, o.zoom));
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

int run_windowed(const RunOptions& o, sim::Crowd* crowd, float* instances) {
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
    if (!renderer) {
        rhi::destroy_device(device);
        platform::destroy_window(window);
        return 1;
    }

    uint64_t shaders_mtime = shader_mtime(dir);
    uint64_t last = platform::now_ticks();
    uint64_t title_timer = last;
    double accumulator = 0.0, sim_ms_acc = 0.0;
    uint32_t frames = 0, ticks = 0;
    int exit_code = 0;
    platform::Input input;
    while (!input.quit) {
        platform::poll_input(&input);
        const uint64_t now = platform::now_ticks();
        accumulator += platform::ticks_to_seconds(now - last);
        last = now;
        // Fixed tick; never run more than 5 ticks in one frame (spiral-of-death guard).
        uint32_t steps = 0;
        while (accumulator >= sim::kTickSeconds && steps < 5) {
            const uint64_t t0 = platform::now_ticks();
            sim::step(crowd, sim::CrowdInput{input.move_x, -input.move_y});
            sim_ms_acc += ms_since(t0);
            accumulator -= sim::kTickSeconds;
            ++steps;
            ++ticks;
        }
        if (steps == 5) accumulator = 0.0;
        const float alpha = (float)(accumulator / sim::kTickSeconds);

        platform::window_pixel_size(window, &pw, &ph);
        rhi::resize(device, (uint32_t)pw, (uint32_t)ph);
        sim::write_enemy_instances(crowd, alpha, instances);
        if (rhi::begin_frame(device)) {
            render::render_crowd(renderer, make_scene(crowd, instances, alpha, o.zoom));
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
        }
    }
    render::destroy_crowd_renderer(renderer);
    rhi::destroy_device(device);
    platform::destroy_window(window);
    return exit_code;
}

}  // namespace

int run_crowd(const RunOptions& o) {
    const bool headless = o.headless || o.sim_only;
    if (!platform::init(headless)) return 1;
    jobs::init(o.threads);

    sim::CrowdConfig cfg;
    cfg.enemy_count = o.enemies;
    cfg.seed = o.seed;
    sim::Crowd* crowd = sim::create_crowd(cfg);
    float* instances = (float*)malloc(sizeof(float) * 4 * (o.enemies ? o.enemies : 1));

    const int code = headless ? run_headless(o, crowd, instances) : run_windowed(o, crowd, instances);

    free(instances);
    sim::destroy_crowd(crowd);
    jobs::shutdown();
    platform::shutdown();
    return code;
}

}  // namespace zaro::app
