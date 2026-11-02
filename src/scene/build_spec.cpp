#include "scene/build_spec.h"

#include "core/path.h"
#include "core/reflect.h"
#include "scene/build_steps.h"

#include <tomlc17.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

namespace {

const char* spec_kind_name(toml_type_t t) {
    switch (t) {
        case TOML_STRING: return "a string";
        case TOML_INT64: return "an integer";
        case TOML_FP64: return "a number";
        case TOML_BOOLEAN: return "a boolean";
        case TOML_ARRAY: return "an array";
        case TOML_TABLE: return "a table";
        default: return "another type";
    }
}

void spec_unexpected_key(const char* file, const toml_datum_t& v, const char* key, const char* where,
                    const char* const* expected, uint32_t count, DiagList* d) {
    const char* hint = reflect::suggest_name(key, expected, count);
    char list[256] = {};
    size_t used = 0;
    for (uint32_t i = 0; i < count && used < sizeof(list); ++i) {
        used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%s", i ? ", " : "", expected[i]);
    }
    diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "unexpected key '%s' in %s%s%s%s (expected %s)", key, where,
             hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "", list);
}

bool spec_copy_text(const toml_datum_t& v, char* out, size_t size, const char* what, const char* file, DiagList* d) {
    if (v.type != TOML_STRING) {
        diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "%s must be a string, got %s", what, spec_kind_name(v.type));
        return false;
    }
    if (!v.u.str.ptr[0] || strlen(v.u.str.ptr) >= size) {
        diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "%s must be 1 to %zu bytes", what, size - 1);
        return false;
    }
    snprintf(out, size, "%s", v.u.str.ptr);
    return true;
}

// The last path component without an extension, lowercase: "C:/x/PowerShell.exe" -> "powershell".
void spec_program_name(const char* program, char* out, size_t size) {
    const char* base = program;
    for (const char* c = program; *c; ++c) {
        if (*c == '/' || *c == '\\') base = c + 1;
    }
    size_t n = 0;
    for (const char* c = base; *c && *c != '.' && n + 1 < size; ++c) {
        out[n++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
    }
    out[n] = 0;
}

// A step runs a program as written: no shells, and no interpreter given code inline.
const char* spec_hidden_code(const BuildStep& s) {
    static const char* const kShells[] = {"sh", "bash", "zsh", "dash", "ksh", "fish", "cmd", "powershell", "pwsh"};
    static const char* const kInterpreters[] = {"python", "python3", "py", "node", "ruby", "perl", "lua", "luau"};
    char name[64];
    spec_program_name(s.args[0], name, sizeof(name));
    for (const char* shell : kShells) {
        if (strcmp(name, shell) == 0) return "runs a shell; name the program to run instead";
    }
    for (const char* interpreter : kInterpreters) {
        if (strcmp(name, interpreter) != 0) continue;
        for (uint32_t i = 1; i < s.arg_count; ++i) {
            if (strcmp(s.args[i], "-c") == 0 || strcmp(s.args[i], "-e") == 0 || strcmp(s.args[i], "--eval") == 0) {
                return "gives an interpreter inline code; put the code in a file and run that";
            }
        }
    }
    return nullptr;
}

bool spec_read_args(const toml_datum_t& v, const char* what, BuildStep* s, const char* file, DiagList* d) {
    if (v.type != TOML_ARRAY) {
        diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "%s must be an array of strings", what);
        return false;
    }
    if ((uint32_t)v.u.arr.size > kMaxStepArgs) {
        diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "%s has more than %u entries", what, kMaxStepArgs);
        return false;
    }
    for (int32_t i = 0; i < v.u.arr.size; ++i) {
        if (!spec_copy_text(v.u.arr.elem[i], s->args[s->arg_count], kStepArgCapacity, what, file, d)) return false;
        ++s->arg_count;
    }
    return true;
}

void spec_read_step(const toml_datum_t& t, const char* file, BuildStep* s, DiagList* d) {
    static const char* const kKeys[] = {"stage", "name", "run", "uses", "with"};
    snprintf(s->file, sizeof(s->file), "%s", file);
    s->line = t.lineno;
    bool has_stage = false, has_run = false, has_uses = false;
    const toml_datum_t* with = nullptr;
    for (int32_t i = 0; i < t.u.tab.size; ++i) {
        const char* key = t.u.tab.key[i];
        const toml_datum_t& v = t.u.tab.value[i];
        if (strcmp(key, "stage") == 0) {
            if (v.type == TOML_STRING && strcmp(v.u.str.ptr, "pre") == 0) s->stage = BuildStage::kPre;
            else if (v.type == TOML_STRING && strcmp(v.u.str.ptr, "post") == 0) s->stage = BuildStage::kPost;
            else diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "stage must be \"pre\" or \"post\"");
            has_stage = true;
        } else if (strcmp(key, "name") == 0) {
            spec_copy_text(v, s->name, sizeof(s->name), "step name", file, d);
        } else if (strcmp(key, "run") == 0) {
            has_run = spec_read_args(v, "run", s, file, d);
            if (has_run && s->arg_count == 0) {
                diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "run needs the program to start");
                has_run = false;
            }
            const char* problem = has_run ? spec_hidden_code(*s) : nullptr;
            if (problem) diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "step %s", problem);
        } else if (strcmp(key, "uses") == 0) {
            has_uses = spec_copy_text(v, s->uses, sizeof(s->uses), "uses", file, d);
        } else if (strcmp(key, "with") == 0) {
            with = &v;
        } else {
            spec_unexpected_key(file, v, key, "[[step]]", kKeys, 5, d);
        }
    }
    if (!has_stage) diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "[[step]] needs stage = \"pre\" or \"post\"");
    if (!s->name[0]) diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "[[step]] needs a name");
    if (has_run == has_uses) {
        diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "[[step]] needs either run = [...] or uses = \"...\"");
    }
    if (with && !has_uses) {
        diag_add(d, kErrBuildSpec, file, with->lineno, with->colno, "with gives arguments to uses; a run step has them in run");
    } else if (with) {
        spec_read_args(*with, "with", s, file, d);
    }
}

void spec_read_environment(const char* name, const toml_datum_t& t, BuildEnvironment* e, const char* file, DiagList* d) {
    static const char* const kKeys[] = {"release", "debug_tools", "config"};
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->line = t.lineno;
    int debug_line = 0;
    const toml_datum_t* config = nullptr;
    for (int32_t i = 0; i < t.u.tab.size; ++i) {
        const char* key = t.u.tab.key[i];
        const toml_datum_t& v = t.u.tab.value[i];
        if (strcmp(key, "release") == 0 || strcmp(key, "debug_tools") == 0) {
            if (v.type != TOML_BOOLEAN) {
                diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "%s must be true or false", key);
            } else if (key[0] == 'r') {
                e->release = v.u.boolean;
            } else {
                e->debug_tools = v.u.boolean;
                debug_line = v.lineno;
            }
        } else if (strcmp(key, "config") == 0 && v.type == TOML_TABLE) {
            config = &v;
        } else {
            spec_unexpected_key(file, v, key, "an [environment.*] table", kKeys, 3, d);
        }
    }
    if (config) {
        for (int32_t i = 0; i < config->u.tab.size; ++i) {
            const char* key = config->u.tab.key[i];
            const toml_datum_t& v = config->u.tab.value[i];
            if (e->config_count == kMaxConfigEntries || strlen(key) >= sizeof(ConfigEntry::key)) {
                diag_add(d, kErrBuildSpec, file, v.lineno, v.colno, "config holds at most %u keys of %zu bytes",
                         kMaxConfigEntries, sizeof(ConfigEntry::key) - 1);
                break;
            }
            ConfigEntry& c = e->config[e->config_count];
            snprintf(c.key, sizeof(c.key), "%s", key);
            c.line = v.lineno;
            if (v.type == TOML_STRING && strlen(v.u.str.ptr) < sizeof(c.value)) {
                c.type = ConfigType::kString;
                snprintf(c.value, sizeof(c.value), "%s", v.u.str.ptr);
            } else if (v.type == TOML_INT64) {
                c.type = ConfigType::kNumber;
                snprintf(c.value, sizeof(c.value), "%lld", (long long)v.u.int64);
            } else if (v.type == TOML_FP64) {
                c.type = ConfigType::kNumber;
                snprintf(c.value, sizeof(c.value), "%.17g", v.u.fp64);
            } else if (v.type == TOML_BOOLEAN) {
                c.type = ConfigType::kBool;
                snprintf(c.value, sizeof(c.value), "%s", v.u.boolean ? "true" : "false");
            } else {
                diag_add(d, kErrBuildSpec, file, v.lineno, v.colno,
                         "config value '%s' must be a string (up to %zu bytes), a number or a boolean", key,
                         sizeof(c.value) - 1);
                continue;
            }
            if (e->release && strncmp(key, "debug", 5) == 0) {
                diag_add(d, kErrBuildRelease, file, v.lineno, v.colno,
                         "release environment '%s' cannot set '%s': debugging switches belong in debug_tools, "
                         "which a release environment cannot enable", name, key);
            }
            ++e->config_count;
        }
    }
    if (e->release && e->debug_tools) {
        diag_add(d, kErrBuildRelease, file, debug_line, 1,
                 "release environment '%s' cannot enable debug_tools; use another environment for debugging", name);
    }
}

}  // namespace

void read_build_steps(const toml_datum_t& steps, const char* file, BuildStep* out, uint32_t* count, DiagList* d) {
    if (steps.type != TOML_ARRAY) {
        diag_add(d, kErrBuildSpec, file, steps.lineno, steps.colno, "step must be an array of tables ([[step]])");
        return;
    }
    for (int32_t i = 0; i < steps.u.arr.size; ++i) {
        const toml_datum_t& t = steps.u.arr.elem[i];
        if (t.type != TOML_TABLE) {
            diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "step must be an array of tables ([[step]])");
            continue;
        }
        if (*count == kMaxBuildSteps) {
            diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "more than %u steps in one file", kMaxBuildSteps);
            return;
        }
        BuildStep* s = &out[*count];
        memset(s, 0, sizeof(*s));
        spec_read_step(t, file, s, d);
        for (uint32_t k = 0; k < *count; ++k) {
            if (s->name[0] && strcmp(out[k].name, s->name) == 0) {
                diag_add(d, kErrBuildSpec, file, t.lineno, t.colno, "step name '%s' is already used on line %d",
                         s->name, out[k].line);
            }
        }
        ++*count;
    }
}

BuildSpec* load_build_spec(const char* path, DiagList* d) {
    BuildSpec* b = (BuildSpec*)calloc(1, sizeof(BuildSpec));
    snprintf(b->path, sizeof(b->path), "%s", path);
    FILE* f = fopen(path, "rb");
    if (!f) {
        diag_add(d, kErrBuildSpec, path, 0, 0, "cannot open build file");
        free(b);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc((size_t)(size > 0 ? size : 0) + 1);
    const size_t n = fread(text, 1, (size_t)(size > 0 ? size : 0), f);
    text[n] = 0;
    fclose(f);
    const uint32_t before = d->count;
    toml_result_t r = toml_parse_named(text, (int)n, path);
    if (!r.ok) {
        diag_add(d, kErrSceneSyntax, path, 0, 0, "invalid TOML: %s", r.errmsg);
    } else {
        static const char* const kTop[] = {"game", "environment", "step"};
        static const char* const kGame[] = {"name", "scene", "version", "version_code", "package", "title",
                                            "orientation"};
        const toml_datum_t top = r.toptab;
        int game_line = 0;
        for (int32_t i = 0; i < top.u.tab.size; ++i) {
            const char* key = top.u.tab.key[i];
            const toml_datum_t& v = top.u.tab.value[i];
            if (strcmp(key, "game") == 0 && v.type == TOML_TABLE) {
                game_line = v.lineno ? v.lineno : 1;
                for (int32_t k = 0; k < v.u.tab.size; ++k) {
                    const char* gk = v.u.tab.key[k];
                    const toml_datum_t& gv = v.u.tab.value[k];
                    char rel[260];
                    if (strcmp(gk, "name") == 0) {
                        spec_copy_text(gv, b->name, sizeof(b->name), "game name", path, d);
                    } else if (strcmp(gk, "scene") == 0) {
                        if (spec_copy_text(gv, rel, sizeof(rel), "game scene", path, d)) {
                            path_join_relative(path, rel, b->scene, sizeof(b->scene));
                            b->scene_line = gv.lineno;
                        }
                    } else if (strcmp(gk, "version") == 0) {
                        spec_copy_text(gv, b->version, sizeof(b->version), "game version", path, d);
                    } else if (strcmp(gk, "version_code") == 0) {
                        if (gv.type != TOML_INT64 || gv.u.int64 < 1 || gv.u.int64 > 2100000000) {
                            diag_add(d, kErrBuildSpec, path, gv.lineno, gv.colno,
                                     "version_code must be a whole number from 1 to 2100000000");
                        } else {
                            b->version_code = (uint32_t)gv.u.int64;
                        }
                    } else if (strcmp(gk, "package") == 0) {
                        if (spec_copy_text(gv, b->package, sizeof(b->package), "game package", path, d) &&
                            !valid_package_name(b->package)) {
                            diag_add(d, kErrBuildSpec, path, gv.lineno, gv.colno,
                                     "package '%s' must be two or more parts like com.example.game: letters, "
                                     "digits and '_', each part starting with a letter", b->package);
                        }
                    } else if (strcmp(gk, "title") == 0) {
                        if (spec_copy_text(gv, b->title, sizeof(b->title), "game title", path, d) &&
                            !valid_app_title(b->title)) {
                            diag_add(d, kErrBuildSpec, path, gv.lineno, gv.colno,
                                     "title '%s' cannot hold control characters or < > & \" ' \\, or start "
                                     "with @ or ?", b->title);
                        }
                    } else if (strcmp(gk, "orientation") == 0) {
                        if (gv.type == TOML_STRING && strcmp(gv.u.str.ptr, "portrait") == 0) b->portrait = true;
                        else if (gv.type != TOML_STRING || strcmp(gv.u.str.ptr, "landscape") != 0) {
                            diag_add(d, kErrBuildSpec, path, gv.lineno, gv.colno,
                                     "orientation must be \"portrait\" or \"landscape\"");
                        }
                    } else {
                        spec_unexpected_key(path, gv, gk, "[game]", kGame, 7, d);
                    }
                }
            } else if (strcmp(key, "environment") == 0 && v.type == TOML_TABLE) {
                for (int32_t k = 0; k < v.u.tab.size; ++k) {
                    const toml_datum_t& ev = v.u.tab.value[k];
                    if (ev.type != TOML_TABLE) {
                        diag_add(d, kErrBuildSpec, path, ev.lineno, ev.colno,
                                 "environment.%s must be a table ([environment.%s])", v.u.tab.key[k], v.u.tab.key[k]);
                    } else if (b->environment_count == kMaxEnvironments ||
                               strlen(v.u.tab.key[k]) >= sizeof(BuildEnvironment::name)) {
                        diag_add(d, kErrBuildSpec, path, ev.lineno, ev.colno,
                                 "at most %u environments with names of %zu bytes", kMaxEnvironments,
                                 sizeof(BuildEnvironment::name) - 1);
                    } else {
                        spec_read_environment(v.u.tab.key[k], ev, &b->environments[b->environment_count++], path, d);
                    }
                }
            } else if (strcmp(key, "step") == 0) {
                read_build_steps(v, path, b->steps, &b->step_count, d);
            } else {
                spec_unexpected_key(path, v, key, "the build file", kTop, 3, d);
            }
        }
        if (!game_line) {
            diag_add(d, kErrBuildSpec, path, 1, 1, "a build file needs a [game] table");
        } else {
            if (!b->name[0]) diag_add(d, kErrBuildSpec, path, game_line, 1, "[game] needs name = \"...\"");
            if (!b->scene[0]) diag_add(d, kErrBuildSpec, path, game_line, 1, "[game] needs scene = \"<file>.toml\"");
            if (!b->version[0]) diag_add(d, kErrBuildSpec, path, game_line, 1, "[game] needs version = \"...\"");
        }
        if (!b->environment_count) {
            diag_add(d, kErrBuildSpec, path, 1, 1, "a build file needs at least one [environment.<name>] table");
        }
    }
    if (r.ok || r.__internal) toml_free(r);
    free(text);
    if (d->count != before) {
        free(b);
        return nullptr;
    }
    return b;
}

void free_build_spec(BuildSpec* spec) { free(spec); }

bool valid_package_name(const char* id) {
    int segments = 0;
    const char* p = id;
    while (*p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
        while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') ++p;
        ++segments;
        if (*p == '.') {
            ++p;
            if (!*p) return false;
        } else if (*p) {
            return false;
        }
    }
    return segments >= 2;
}

// The title goes into an XML attribute as it is, so it holds nothing that needs escaping there or
// that Android would read as a resource reference.
bool valid_app_title(const char* title) {
    const size_t n = strlen(title);
    if (n == 0 || n > 100 || title[0] == '@' || title[0] == '?') return false;
    for (const char* c = title; *c; ++c) {
        const unsigned char ch = (unsigned char)*c;
        if (ch < 0x20 || ch == 0x7f || strchr("<>&\"'\\", ch)) return false;
    }
    return true;
}

const BuildEnvironment* find_environment(const BuildSpec* spec, const char* name) {
    for (uint32_t i = 0; i < spec->environment_count; ++i) {
        if (strcmp(spec->environments[i].name, name) == 0) return &spec->environments[i];
    }
    return nullptr;
}

}  // namespace zaro::scene
