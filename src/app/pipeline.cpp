#include "app/pipeline.h"

#include "app/tools.h"
#include "core/diag.h"
#include "core/log.h"
#include "core/path.h"
#include "platform/platform.h"
#include "scene/build_extension.h"
#include "scene/build_spec.h"
#include "scene/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::app {

namespace {

// --- Built-in steps --------------------------------------------------------------------------------

int step_check(const char* const* args, uint32_t count, const char* game_scene) {
    if (count == 0) return check_scene(game_scene, false);
    int code = 0;
    for (uint32_t i = 0; i < count; ++i) code |= check_scene(args[i], false);
    return code;
}

int step_test(const char* const* args, uint32_t count, const char*) {
    return run_scene_tests(args, (int)count, false, false);
}

int step_design_check(const char* const* args, uint32_t count, const char*) {
    if (count != 2) return 2;
    return import_design(args[0], args[1], true);
}

const BuiltinStep kBuiltinSteps[] = {
    {"check", "[files...] (default: the game's scene)", step_check},
    {"test", "<test files or directories...>", step_test},
    {"design-check", "<kit dir> <theme dir>", step_design_check},
};

const BuiltinStep* find_builtin(const char* name) {
    for (const BuiltinStep& b : kBuiltinSteps) {
        if (strcmp(b.name, name) == 0) return &b;
    }
    return nullptr;
}

// --- The pipeline ------------------------------------------------------------------------------------

constexpr size_t kArgCapacity = 1024;

struct Pipeline {
    scene::BuildSpec* spec = nullptr;
    const scene::BuildEnvironment* env = nullptr;
    uint32_t version_code = 0;
    char version_code_text[16] = {};
    char out[512] = {};
    scene::BuildExtension* extensions[scene::kMaxBuildExtensions] = {};
    uint32_t extension_count = 0;
};

// The steps of one stage in the order they run: plugins first, in scene order, then the game's.
uint32_t stage_steps(const Pipeline& p, scene::BuildStage stage, const scene::BuildStep** out, uint32_t max) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < p.extension_count; ++i) {
        for (uint32_t k = 0; k < p.extensions[i]->step_count && n < max; ++k) {
            if (p.extensions[i]->steps[k].stage == stage) out[n++] = &p.extensions[i]->steps[k];
        }
    }
    for (uint32_t k = 0; k < p.spec->step_count && n < max; ++k) {
        if (p.spec->steps[k].stage == stage) out[n++] = &p.spec->steps[k];
    }
    return n;
}

// Replaces {out}, {env}, {version} and {version_code} in one argument.
void expand(const Pipeline& p, const char* in, char* out, size_t size) {
    const struct {
        const char* key;
        const char* value;
    } vars[] = {{"{out}", p.out}, {"{env}", p.env->name}, {"{version}", p.spec->version},
                {"{version_code}", p.version_code_text}};
    size_t o = 0;
    while (*in && o + 1 < size) {
        bool replaced = false;
        for (const auto& v : vars) {
            const size_t n = strlen(v.key);
            if (strncmp(in, v.key, n) == 0) {
                o += (size_t)snprintf(out + o, size - o, "%s", v.value);
                if (o >= size) o = size - 1;
                in += n;
                replaced = true;
                break;
            }
        }
        if (!replaced) out[o++] = *in++;
    }
    out[o] = 0;
}

void step_dir(const scene::BuildStep& s, char* out, size_t size) {
    snprintf(out, size, "%s", s.file);
    char* slash = strrchr(out, '/');
    if (!slash) slash = strrchr(out, '\\');
    if (slash) *slash = 0;
    else snprintf(out, size, ".");
}

void print_step(const scene::BuildStep& s) {
    printf("  %s (%s:%d): %s", s.name, s.file, s.line, s.uses[0] ? "uses " : "run");
    if (s.uses[0]) printf("%s", s.uses);
    for (uint32_t i = 0; i < s.arg_count; ++i) printf(" %s", s.args[i]);
    putchar('\n');
}

int run_step(const Pipeline& p, const scene::BuildStep& s) {
    char (*args)[kArgCapacity] = new char[scene::kMaxStepArgs + 1][kArgCapacity];
    const char* argv[scene::kMaxStepArgs + 1] = {};
    char dir[512];
    step_dir(s, dir, sizeof(dir));
    for (uint32_t i = 0; i < s.arg_count; ++i) {
        char expanded[kArgCapacity];
        expand(p, s.args[i], expanded, sizeof(expanded));
        if (s.uses[0]) {
            path_join_relative(s.file, expanded, args[i], kArgCapacity);  // built-in arguments are paths
        } else if (i == 0 && strcmp(expanded, "zaro") == 0) {
            snprintf(args[i], kArgCapacity, "%szaro", platform::base_path());  // this binary
        } else {
            snprintf(args[i], kArgCapacity, "%s", expanded);
        }
        argv[i] = args[i];
    }
    printf("zaro build: %s (%s:%d)\n", s.name, s.file, s.line);
    fflush(stdout);
    int code = 0;
    if (s.uses[0]) {
        const BuiltinStep* b = find_builtin(s.uses);
        code = b->run(argv, s.arg_count, p.spec->scene);
        if (code == 2) {
            log_error(kErrBuildStep, "%s:%d: step '%s': uses %s needs with = [%s]", s.file, s.line, s.name, b->name,
                      b->usage);
        }
    } else {
        code = platform::run_process(argv, dir, nullptr, 0);
        if (code == -1) {
            log_error(kErrBuildStep, "%s:%d: step '%s': cannot start '%s' (not found on PATH?)", s.file, s.line,
                      s.name, argv[0]);
        }
    }
    if (code != 0) log_error(kErrBuildStep, "%s:%d: step '%s' failed (exit code %d)", s.file, s.line, s.name, code);
    delete[] args;
    return code;
}

void write_toml_string(FILE* f, const char* s) {
    fputc('"', f);
    for (const char* c = s; *c; ++c) {
        if (*c == '"' || *c == '\\') fputc('\\', f);
        if (*c == '\n') fputs("\\n", f);
        else fputc(*c, f);
    }
    fputc('"', f);
}

// The game's commit, or "unknown" outside a git checkout.
void game_commit(const Pipeline& p, char* out, size_t size) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", p.spec->path);
    char* slash = strrchr(dir, '/');
    if (slash) *slash = 0;
    else snprintf(dir, sizeof(dir), ".");
    const char* argv[] = {"git", "-C", dir, "rev-parse", "--short=12", "HEAD", nullptr};
    char text[64] = {};
    if (platform::run_process(argv, nullptr, text, sizeof(text)) != 0 || !text[0]) {
        snprintf(out, size, "unknown");
        return;
    }
    text[strcspn(text, "\r\n")] = 0;
    snprintf(out, size, "%s", text);
}

bool write_build_info(const Pipeline& p) {
    char path[600];
    snprintf(path, sizeof(path), "%s/build_info.toml", p.out);
    FILE* f = fopen(path, "w");
    if (!f) {
        log_error(kErrIo, "%s: cannot write", path);
        return false;
    }
    char commit[64];
    game_commit(p, commit, sizeof(commit));
    fputs("# Written by zaro build (D-027) for this build; do not edit.\n[build]\ngame = ", f);
    write_toml_string(f, p.spec->name);
    fputs("\nversion = ", f);
    write_toml_string(f, p.spec->version);
    if (p.version_code) fprintf(f, "\nversion_code = %u", p.version_code);
    fputs("\ncommit = ", f);
    write_toml_string(f, commit);
    fputs("\nenvironment = ", f);
    write_toml_string(f, p.env->name);
    fprintf(f, "\nrelease = %s\ndebug_tools = %s\n\n[config]\n", p.env->release ? "true" : "false",
            p.env->debug_tools ? "true" : "false");
    for (uint32_t i = 0; i < p.env->config_count; ++i) {
        const scene::ConfigEntry& c = p.env->config[i];
        fprintf(f, "%s = ", c.key);
        if (c.type == scene::ConfigType::kString) write_toml_string(f, c.value);
        else fputs(c.value, f);
        fputc('\n', f);
    }
    const bool ok = fclose(f) == 0;
    printf("zaro build: wrote %s (version %s, commit %s, environment %s)\n", path, p.spec->version, commit,
           p.env->name);
    return ok;
}

// validate: the build file, the environment, the scene, its extensions and every step's built-in.
int validate(const BuildOptions& o, Pipeline& p) {
    DiagList* diags = new DiagList();
    p.spec = scene::load_build_spec(o.build_file, diags);
    diag_print_text(diags, stderr);
    delete diags;
    if (!p.spec) return 1;

    p.env = o.environment ? scene::find_environment(p.spec, o.environment) : &p.spec->environments[0];
    if (!p.env) {
        char names[256] = {};
        size_t used = 0;
        for (uint32_t i = 0; i < p.spec->environment_count && used < sizeof(names); ++i) {
            used += (size_t)snprintf(names + used, sizeof(names) - used, "%s%s", i ? ", " : "",
                                     p.spec->environments[i].name);
        }
        log_error(kErrBuildSpec, "%s: no environment '%s' (it has %s)", o.build_file, o.environment, names);
        return 2;
    }
    p.version_code = p.spec->version_code;
    if (o.version_code) {
        char* end = nullptr;
        const unsigned long long v = strtoull(o.version_code, &end, 10);
        if (!o.version_code[0] || *end || v < 1 || v > 2100000000ull) {
            log_error(kErrCliUsage, "--version-code '%s' must be a whole number from 1 to 2100000000", o.version_code);
            return 2;
        }
        p.version_code = (uint32_t)v;
    }
    if (p.version_code) snprintf(p.version_code_text, sizeof(p.version_code_text), "%u", p.version_code);
    if (o.out_dir) snprintf(p.out, sizeof(p.out), "%s", o.out_dir);
    else snprintf(p.out, sizeof(p.out), "build/zaro-build/%s-%s", p.spec->name, p.env->name);

    if (check_scene(p.spec->scene, false) != 0) {
        log_error(kErrBuildSpec, "%s:%d: the game's scene has errors (above)", o.build_file, p.spec->scene_line);
        return 1;
    }
    diags = new DiagList();
    scene::Scene* s = scene::load_scene(p.spec->scene, diags);
    for (uint32_t i = 0; s && i < s->build_extension_count; ++i) {
        p.extensions[p.extension_count] = scene::load_build_extension(s->build_extensions[i], diags);
        if (p.extensions[p.extension_count]) ++p.extension_count;
    }
    diag_print_text(diags, stderr);
    const bool loaded = s && diags->count == 0;
    delete diags;
    scene::free_scene(s);
    if (!loaded) return 1;

    // Every built-in a step uses exists, and step names are unique across the game and its plugins.
    const scene::BuildStep* all[scene::kMaxBuildSteps * (scene::kMaxBuildExtensions + 1)];
    uint32_t n = stage_steps(p, scene::BuildStage::kPre, all, sizeof(all) / sizeof(all[0]));
    n += stage_steps(p, scene::BuildStage::kPost, all + n, sizeof(all) / sizeof(all[0]) - n);
    int code = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (all[i]->uses[0] && !find_builtin(all[i]->uses)) {
            char names[128] = {};
            size_t used = 0;
            for (const BuiltinStep& b : kBuiltinSteps) {
                used += (size_t)snprintf(names + used, sizeof(names) - used, "%s%s", used ? ", " : "", b.name);
            }
            log_error(kErrBuildSpec, "%s:%d: step '%s' uses '%s', which is not a built-in step (%s)", all[i]->file,
                      all[i]->line, all[i]->name, all[i]->uses, names);
            code = 1;
        }
        for (uint32_t k = 0; k < i; ++k) {
            if (strcmp(all[k]->name, all[i]->name) == 0) {
                log_error(kErrBuildSpec, "%s:%d: step name '%s' is also used at %s:%d", all[i]->file, all[i]->line,
                          all[i]->name, all[k]->file, all[k]->line);
                code = 1;
            }
        }
    }
    return code;
}

void print_plan(const Pipeline& p) {
    printf("zaro build: %s %s (%s), environment %s%s%s\n", p.spec->name, p.spec->version,
           p.version_code ? p.version_code_text : "no version code", p.env->name, p.env->release ? ", release" : "",
           p.env->debug_tools ? ", debug tools" : "");
    const scene::BuildStep* steps[scene::kMaxBuildSteps * (scene::kMaxBuildExtensions + 1)];
    const char* stages[] = {"validate", "pre", "native", "export", "post", "package", "sign"};
    for (const char* stage : stages) {
        printf("%s\n", stage);
        if (strcmp(stage, "validate") == 0) {
            printf("  %s, %s and %u build extension(s)\n", p.spec->path, p.spec->scene, p.extension_count);
        } else if (strcmp(stage, "pre") == 0 || strcmp(stage, "post") == 0) {
            const uint32_t n = stage_steps(p, stage[1] == 'r' ? scene::BuildStage::kPre : scene::BuildStage::kPost,
                                           steps, sizeof(steps) / sizeof(steps[0]));
            for (uint32_t i = 0; i < n; ++i) print_step(*steps[i]);
            if (strcmp(stage, "post") == 0 && n) printf("  (runs only for a target)\n");
        } else {
            printf("  (no target)\n");
        }
    }
}

}  // namespace

const BuiltinStep* builtin_steps(uint32_t* count) {
    *count = sizeof(kBuiltinSteps) / sizeof(kBuiltinSteps[0]);
    return kBuiltinSteps;
}

int build_game(const BuildOptions& o) {
    register_all_components();
    Pipeline p;
    int code = validate(o, p);
    if (code == 0 && o.plan) {
        print_plan(p);
    } else if (code == 0) {
        printf("zaro build: validate ok (%s, environment %s)\n", p.spec->name, p.env->name);
        if (!platform::create_directories(p.out)) {
            log_error(kErrIo, "%s: cannot create the build directory", p.out);
            code = 1;
        }
        if (code == 0 && !write_build_info(p)) code = 1;
        const scene::BuildStep* steps[scene::kMaxBuildSteps * (scene::kMaxBuildExtensions + 1)];
        const uint32_t n = stage_steps(p, scene::BuildStage::kPre, steps, sizeof(steps) / sizeof(steps[0]));
        for (uint32_t i = 0; i < n && code == 0; ++i) code = run_step(p, *steps[i]) ? 1 : 0;
        if (code == 0) printf("zaro build: done (native, export, post, package and sign need a target)\n");
    }
    for (uint32_t i = 0; i < p.extension_count; ++i) scene::free_build_extension(p.extensions[i]);
    scene::free_build_spec(p.spec);
    return code;
}

}  // namespace zaro::app
