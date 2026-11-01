// zaro command line entry point. The editor and every agent workflow sit on top of this.
#include "app/crowd_app.h"
#include "app/pipeline.h"
#include "app/tools.h"
#include "core/log.h"
#include "zaro/zaro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

void print_usage() {
    puts("usage: zaro <command> [options]\n"
         "\n"
         "commands:\n"
         "  version    print engine and plugin ABI version\n"
         "  run        run a scene: zaro run [scene.toml] [options] (no scene: built-in crowd demo)\n"
         "  schema     print every component (fields, types, defaults, ranges) as JSON\n"
         "  check      validate a scene (with its screens, strings and script) or a screen file:\n"
         "             zaro check <file.toml> [--json]\n"
         "  test       run scene tests: zaro test [paths...] [--update] [--json] (default path: tests)\n"
         "  screenshot render a scene headless to PNG: zaro screenshot <scene.toml> [out.png]\n"
         "             [--frames N (default 60)] [--size WxH] [--lang LANG] [--click SCREEN/NODE]...\n"
         "  export     write a Gradle project for an Android APK: zaro export android <scene.toml>\n"
         "             [--out DIR (default build/android-project)] [--runtime DIR] [--package ID]\n"
         "             [--version-code N (default 1)] [--version-name S (default 1.0)] [--portrait]\n"
         "             (runtime: python tools/build.py android; APK or AAB: tools/android.py apk|aab)\n"
         "  build      run a game's build pipeline (validate, pre, native, export, post, package, sign):\n"
         "             zaro build <build.toml> [--env NAME] [--version-code N] [--out DIR] [--plan]\n"
         "             [--target android [--package aab|apk] [--release]]\n"
         "  import-design  turn a Claude Design kit into a UI theme and prefab stubs:\n"
         "             zaro import-design <kit dir> <out dir> [--check]\n"
         "  help       print this message\n"
         "\n"
         "run options:\n"
         "  --headless           render offscreen, scripted player, fixed frame count\n"
         "  --sim-only           headless without a GPU (simulation + state hash)\n"
         "  --frames N           measured frames in headless mode (default 600)\n"
         "  --warmup N           frames before measuring (default 60)\n"
         "  --enemies N          enemy count; overrides the scene (default 50000)\n"
         "  --seed N             simulation seed; overrides the scene (default 1)\n"
         "  --threads N          worker threads, 0 = hardware count (default 0)\n"
         "  --size WxH           render size (default 1920x1080)\n"
         "  --zoom F             camera distance multiplier (default 1)\n"
         "  --no-vsync           windowed: present without vsync\n"
         "  --backend NAME       vulkan | d3d12 | metal (default: wgpu's choice)\n"
         "  --perf PATH          write a perf report (JSON) to PATH\n"
         "  --screenshot PATH    headless: save the last frame as PNG\n"
         "  --lang LANG          UI language, one of the scene's languages (default: its first)\n"
         "  --click SCREEN/NODE  headless: click that button on the first frames, one per frame;\n"
         "                       repeat for more (up to 8)\n"
         "\n"
         "planned (not implemented yet): plugin");
}

bool parse_u32(const char* s, uint32_t* out) {
    char* end = nullptr;
    const unsigned long v = strtoul(s, &end, 10);
    if (!s[0] || *end) return false;
    *out = (uint32_t)v;
    return true;
}

int usage_error(const char* what, const char* arg) {
    zaro::log_error(zaro::kErrCliUsage, "%s '%s' (try 'zaro help')", what, arg);
    return 2;
}

int cmd_run(int argc, char** argv) {
    zaro::app::RunOptions o;
    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        auto need = [&](uint32_t* out) {
            if (!next || !parse_u32(next, out)) return false;
            ++i;
            return true;
        };
        if (strcmp(a, "--headless") == 0) {
            o.headless = true;
        } else if (strcmp(a, "--sim-only") == 0) {
            o.sim_only = true;
        } else if (strcmp(a, "--no-vsync") == 0) {
            o.vsync = false;
        } else if (strcmp(a, "--frames") == 0) {
            if (!need(&o.frames)) return usage_error("--frames needs a number, got", next ? next : "");
        } else if (strcmp(a, "--warmup") == 0) {
            if (!need(&o.warmup)) return usage_error("--warmup needs a number, got", next ? next : "");
        } else if (strcmp(a, "--enemies") == 0) {
            if (!need(&o.enemies)) return usage_error("--enemies needs a number, got", next ? next : "");
            o.enemies_set = true;
        } else if (strcmp(a, "--threads") == 0) {
            if (!need(&o.threads)) return usage_error("--threads needs a number, got", next ? next : "");
        } else if (strcmp(a, "--seed") == 0) {
            uint32_t seed = 0;
            if (!need(&seed)) return usage_error("--seed needs a number, got", next ? next : "");
            o.seed = seed;
            o.seed_set = true;
        } else if (strcmp(a, "--size") == 0) {
            unsigned w = 0, h = 0;
            if (!next || sscanf(next, "%ux%u", &w, &h) != 2 || !w || !h) {
                return usage_error("--size needs WxH, got", next ? next : "");
            }
            o.width = w;
            o.height = h;
            ++i;
        } else if (strcmp(a, "--zoom") == 0) {
            if (!next) return usage_error("--zoom needs a number, got", "");
            o.zoom = (float)atof(next);
            ++i;
        } else if (strcmp(a, "--backend") == 0) {
            if (!next) return usage_error("--backend needs a name, got", "");
            if (strcmp(next, "vulkan") == 0) o.backend = zaro::rhi::Backend::kVulkan;
            else if (strcmp(next, "d3d12") == 0) o.backend = zaro::rhi::Backend::kD3D12;
            else if (strcmp(next, "metal") == 0) o.backend = zaro::rhi::Backend::kMetal;
            else return usage_error("unknown backend", next);
            ++i;
        } else if (strcmp(a, "--perf") == 0) {
            if (!next) return usage_error("--perf needs a path, got", "");
            o.perf_path = next;
            ++i;
        } else if (strcmp(a, "--screenshot") == 0) {
            if (!next) return usage_error("--screenshot needs a path, got", "");
            o.screenshot_path = next;
            ++i;
        } else if (strcmp(a, "--lang") == 0) {
            if (!next) return usage_error("--lang needs a language, got", "");
            o.language = next;
            ++i;
        } else if (strcmp(a, "--click") == 0) {
            if (!next || !strchr(next, '/')) return usage_error("--click needs SCREEN/NODE, got", next ? next : "");
            if (o.click_count == zaro::app::RunOptions::kMaxClicks) return usage_error("too many clicks at", next);
            o.clicks[o.click_count++] = next;
            ++i;
        } else if (a[0] != '-' && !o.scene_path) {
            o.scene_path = a;
        } else {
            return usage_error("unknown option", a);
        }
    }
    return zaro::app::run_crowd(o);
}

int cmd_screenshot(int argc, char** argv) {
    zaro::app::RunOptions o;
    o.headless = true;
    o.warmup = 0;
    o.frames = 60;
    const char* out = nullptr;
    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (strcmp(a, "--frames") == 0) {
            if (!next || !parse_u32(next, &o.frames) || o.frames == 0) {
                return usage_error("--frames needs a positive number, got", next ? next : "");
            }
            ++i;
        } else if (strcmp(a, "--size") == 0) {
            unsigned w = 0, h = 0;
            if (!next || sscanf(next, "%ux%u", &w, &h) != 2 || !w || !h) {
                return usage_error("--size needs WxH, got", next ? next : "");
            }
            o.width = w;
            o.height = h;
            ++i;
        } else if (strcmp(a, "--lang") == 0) {
            if (!next) return usage_error("--lang needs a language, got", "");
            o.language = next;
            ++i;
        } else if (strcmp(a, "--click") == 0) {
            if (!next || !strchr(next, '/')) return usage_error("--click needs SCREEN/NODE, got", next ? next : "");
            if (o.click_count == zaro::app::RunOptions::kMaxClicks) return usage_error("too many clicks at", next);
            o.clicks[o.click_count++] = next;
            ++i;
        } else if (a[0] == '-') {
            return usage_error("unknown option", a);
        } else if (!o.scene_path) {
            o.scene_path = a;
        } else if (!out) {
            out = a;
        } else {
            return usage_error("unexpected argument", a);
        }
    }
    if (!o.scene_path) return usage_error("screenshot needs a scene path, got", "");
    o.screenshot_path = out ? out : "screenshot.png";
    const int code = zaro::app::run_crowd(o);
    if (code == 0) printf("wrote %s\n", o.screenshot_path);
    return code;
}

int cmd_export(int argc, char** argv) {
    if (argc < 3 || strcmp(argv[2], "android") != 0) {
        return usage_error("export needs a target ('android'), got", argc < 3 ? "" : argv[2]);
    }
    zaro::app::ExportOptions o;
    for (int i = 3; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        const char** value = nullptr;
        if (strcmp(a, "--out") == 0) value = &o.out_dir;
        else if (strcmp(a, "--runtime") == 0) value = &o.runtime_dir;
        else if (strcmp(a, "--package") == 0) value = &o.package;
        else if (strcmp(a, "--version-code") == 0) value = &o.version_code;
        else if (strcmp(a, "--version-name") == 0) value = &o.version_name;
        if (value) {
            if (!next) return usage_error("missing value for", a);
            *value = next;
            ++i;
        } else if (strcmp(a, "--portrait") == 0) {
            o.portrait = true;
        } else if (a[0] == '-') {
            return usage_error("unknown option", a);
        } else if (!o.scene_path) {
            o.scene_path = a;
        } else {
            return usage_error("unexpected argument", a);
        }
    }
    if (!o.scene_path) return usage_error("export android needs a scene path, got", "");
    return zaro::app::export_android(o);
}

int cmd_build(int argc, char** argv) {
    zaro::app::BuildOptions o;
    for (int i = 2; i < argc; ++i) {
        const char* a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        const char** value = nullptr;
        if (strcmp(a, "--env") == 0) value = &o.environment;
        else if (strcmp(a, "--version-code") == 0) value = &o.version_code;
        else if (strcmp(a, "--out") == 0) value = &o.out_dir;
        else if (strcmp(a, "--target") == 0) value = &o.target;
        else if (strcmp(a, "--package") == 0) value = &o.package;
        if (value) {
            if (!next) return usage_error("missing value for", a);
            *value = next;
            ++i;
        } else if (strcmp(a, "--plan") == 0) {
            o.plan = true;
        } else if (strcmp(a, "--release") == 0) {
            o.release = true;
        } else if (a[0] == '-') {
            return usage_error("unknown option", a);
        } else if (!o.build_file) {
            o.build_file = a;
        } else {
            return usage_error("unexpected argument", a);
        }
    }
    if (!o.build_file) return usage_error("build needs a build file, got", "");
    return zaro::app::build_game(o);
}

}  // namespace

int main(int argc, char** argv) {
    // Android: unpacks the APK's assets and enters their directory before any file is read.
    if (!zaro::app::prepare_process()) return 1;
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        const zaro_version_info v = zaro_get_version();
        printf("zaro %u.%u.%u (abi %u)\n", v.major, v.minor, v.patch, v.abi_major);
        return 0;
    }
    if (strcmp(argv[1], "run") == 0) return cmd_run(argc, argv);
    if (strcmp(argv[1], "schema") == 0) return zaro::app::print_schema();
    if (strcmp(argv[1], "check") == 0) {
        const char* path = nullptr;
        bool json = false;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--json") == 0) json = true;
            else if (!path) path = argv[i];
            else return usage_error("unexpected argument", argv[i]);
        }
        if (!path) return usage_error("check needs a scene path, got", "");
        return zaro::app::check_scene(path, json);
    }
    if (strcmp(argv[1], "test") == 0) {
        const char* paths[64];
        int count = 0;
        bool update = false, json = false;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--update") == 0) update = true;
            else if (strcmp(argv[i], "--json") == 0) json = true;
            else if (argv[i][0] == '-') return usage_error("unknown option", argv[i]);
            else if (count < 64) paths[count++] = argv[i];
        }
        return zaro::app::run_scene_tests(paths, count, update, json);
    }
    if (strcmp(argv[1], "screenshot") == 0) return cmd_screenshot(argc, argv);
    if (strcmp(argv[1], "export") == 0) return cmd_export(argc, argv);
    if (strcmp(argv[1], "build") == 0) return cmd_build(argc, argv);
    if (strcmp(argv[1], "import-design") == 0) {
        const char* dirs[2] = {nullptr, nullptr};
        bool check = false;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--check") == 0) check = true;
            else if (argv[i][0] == '-') return usage_error("unknown option", argv[i]);
            else if (!dirs[0]) dirs[0] = argv[i];
            else if (!dirs[1]) dirs[1] = argv[i];
            else return usage_error("unexpected argument", argv[i]);
        }
        if (!dirs[1]) return usage_error("import-design needs <kit dir> <out dir>, got", dirs[0] ? dirs[0] : "");
        return zaro::app::import_design(dirs[0], dirs[1], check);
    }
    zaro::log_error(zaro::kErrCliUsage, "unknown command '%s' (try 'zaro help')", argv[1]);
    return 2;
}
