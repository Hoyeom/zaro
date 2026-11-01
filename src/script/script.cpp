#include "script/script.h"

#include "core/reflect.h"
#include "scene/ui_state.h"

#include <lua.h>
#include <luacode.h>
#include <lualib.h>

#include <chrono>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::script {

using reflect::ComponentDesc;
using reflect::FieldDesc;
using reflect::FieldType;
using Clock = std::chrono::steady_clock;

struct Script {
    lua_State* L = nullptr;  // main state: read-only standard library and `zaro`
    lua_State* T = nullptr;  // sandboxed thread whose globals belong to the script
    int thread_ref = 0;
    char path[scene::kPathCapacity] = {};
    char chunkname[scene::kPathCapacity + 1] = {};
    scene::Scene* scene = nullptr;
    scene::UiState* ui = nullptr;
    const UiHost* host = nullptr;
    uint64_t tick = 0;
    double max_call_seconds = 1.0;
    Clock::time_point deadline;
    bool deadline_armed = false;
    bool timed_out = false;
};

namespace {

Script* self(lua_State* L) { return (Script*)lua_callbacks(L)->userdata; }

void on_interrupt(lua_State* L, int gc) {
    if (gc >= 0) return;  // garbage-collector step, not a safepoint we care about
    Script* s = self(L);
    if (s && s->deadline_armed && Clock::now() > s->deadline) {
        s->deadline_armed = false;
        s->timed_out = true;
        luaL_error(L, "script call took longer than %g s (infinite loop?)", s->max_call_seconds);
    }
}

// Splits "path:LINE: message" (Luau's error position format) into line and message.
void report(Script* s, ErrorId id, const char* raw, DiagList* diags) {
    const size_t n = strlen(s->path);
    int line = 0;
    const char* msg = raw;
    if (strncmp(raw, s->path, n) == 0 && raw[n] == ':') {
        char* end = nullptr;
        const long l = strtol(raw + n + 1, &end, 10);
        if (end && end != raw + n + 1 && end[0] == ':') {
            line = (int)l;
            msg = end[1] == ' ' ? end + 2 : end + 1;
        }
    }
    if (s->timed_out) id = kErrScriptTimeout;
    diag_add(diags, id, s->path, line, line ? 1 : 0, "%s", msg);
}

// --- zaro.* bindings. These raise Lua errors with luaL_error (longjmp), so they hold no
// objects with destructors.

scene::Entity* entity_arg(lua_State* L, int arg) {
    Script* s = self(L);
    const char* name = luaL_checklstring(L, arg, nullptr);
    for (uint32_t i = 0; i < s->scene->entity_count; ++i) {
        if (strcmp(s->scene->entities[i].name, name) == 0) return &s->scene->entities[i];
    }
    luaL_error(L, "no entity named '%s' in scene '%s'", name, s->scene->name);
}

void* component_arg(lua_State* L, scene::Entity* e, int arg, const ComponentDesc** out) {
    const char* name = luaL_checklstring(L, arg, nullptr);
    const ComponentDesc* desc = reflect::find_component(name);
    if (!desc) {
        const char* hint = reflect::suggest_component(name);
        luaL_error(L, "unknown component '%s'%s%s%s", name, hint ? " (did you mean '" : "", hint ? hint : "",
                   hint ? "'?)" : "");
    }
    for (uint32_t i = 0; i < e->component_count; ++i) {
        if (e->components[i].desc == desc) {
            *out = desc;
            return e->components[i].data;
        }
    }
    luaL_error(L, "entity '%s' has no %s component", e->name, desc->name);
}

const FieldDesc* field_arg(lua_State* L, const ComponentDesc* desc, int arg) {
    const char* name = luaL_checklstring(L, arg, nullptr);
    const FieldDesc* f = reflect::find_field(desc, name);
    if (!f) {
        const char* hint = reflect::suggest_field(desc, name);
        luaL_error(L, "%s has no field '%s'%s%s%s", desc->name, name, hint ? " (did you mean '" : "", hint ? hint : "",
                   hint ? "'?)" : "");
    }
    return f;
}

int vector_size(FieldType t) { return t == FieldType::kVec2 ? 2 : (t == FieldType::kVec3 ? 3 : 4); }
const char* const kAxes[4] = {"x", "y", "z", "w"};

// Pushes a field's value: number, boolean, string or {x, y[, z[, w]]}.
void push_field(lua_State* L, const FieldDesc* f, const uint8_t* p) {
    switch (f->type) {
        case FieldType::kBool: lua_pushboolean(L, *(const bool*)p); break;
        case FieldType::kI32: lua_pushnumber(L, *(const int32_t*)p); break;
        case FieldType::kU32: lua_pushnumber(L, *(const uint32_t*)p); break;
        case FieldType::kU64: lua_pushnumber(L, (double)*(const uint64_t*)p); break;
        case FieldType::kF32: lua_pushnumber(L, *(const float*)p); break;
        case FieldType::kString: lua_pushstring(L, (const char*)p); break;
        case FieldType::kColor:
        case FieldType::kEnum: {
            char text[reflect::kStringCapacity];
            reflect::format_text_value(*f, p, text, sizeof(text));
            lua_pushstring(L, text);
            break;
        }
        case FieldType::kVec2:
        case FieldType::kVec3:
        case FieldType::kVec4: {
            const int n = vector_size(f->type);
            lua_createtable(L, 0, n);
            for (int i = 0; i < n; ++i) {
                lua_pushnumber(L, ((const float*)p)[i]);
                lua_setfield(L, -2, kAxes[i]);
            }
            break;
        }
    }
}

int zaro_get(lua_State* L) {
    scene::Entity* e = entity_arg(L, 1);
    const ComponentDesc* desc = nullptr;
    const uint8_t* data = (const uint8_t*)component_arg(L, e, 2, &desc);
    const FieldDesc* f = field_arg(L, desc, 3);
    push_field(L, f, data + f->offset);
    return 1;
}

double number_in_range(lua_State* L, const ComponentDesc* desc, const FieldDesc* f, double x) {
    if (f->min <= f->max && (x < f->min || x > f->max)) {
        luaL_error(L, "%s.%s = %g is outside [%g, %g]", desc->name, f->name, x, f->min, f->max);
    }
    return x;
}

// Stores the Lua value at `arg` into a field, with the scene loader's type and range checks.
void store_field(lua_State* L, const ComponentDesc* desc, const FieldDesc* f, uint8_t* p, int arg) {
    const int t = lua_type(L, arg);
    const char* tn = reflect::type_name(f->type);
    switch (f->type) {
        case FieldType::kBool:
            if (t != LUA_TBOOLEAN) luaL_error(L, "%s.%s expects %s, got %s", desc->name, f->name, tn, luaL_typename(L, arg));
            *(bool*)p = lua_toboolean(L, arg) != 0;
            break;
        case FieldType::kI32:
        case FieldType::kU32:
        case FieldType::kU64:
        case FieldType::kF32: {
            if (t != LUA_TNUMBER) luaL_error(L, "%s.%s expects %s, got %s", desc->name, f->name, tn, luaL_typename(L, arg));
            const double x = number_in_range(L, desc, f, lua_tonumber(L, arg));
            if (f->type == FieldType::kF32) {
                *(float*)p = (float)x;
                break;
            }
            const bool integral = floor(x) == x;
            const bool fits = f->type == FieldType::kI32   ? (x >= INT32_MIN && x <= INT32_MAX)
                              : f->type == FieldType::kU32 ? (x >= 0 && x <= (double)UINT32_MAX)
                                                           : (x >= 0 && x <= 9007199254740992.0);
            if (!integral || !fits) luaL_error(L, "%s.%s = %g does not fit %s", desc->name, f->name, x, tn);
            if (f->type == FieldType::kI32) *(int32_t*)p = (int32_t)x;
            else if (f->type == FieldType::kU32) *(uint32_t*)p = (uint32_t)x;
            else *(uint64_t*)p = (uint64_t)x;
            break;
        }
        case FieldType::kString: {
            if (t != LUA_TSTRING) luaL_error(L, "%s.%s expects %s, got %s", desc->name, f->name, tn, luaL_typename(L, arg));
            size_t len = 0;
            const char* str = lua_tolstring(L, arg, &len);
            if (len >= reflect::kStringCapacity) {
                luaL_error(L, "%s.%s is %d bytes (max %u)", desc->name, f->name, (int)len, reflect::kStringCapacity - 1);
            }
            memcpy(p, str, len + 1);
            break;
        }
        case FieldType::kColor:
        case FieldType::kEnum: {
            if (t != LUA_TSTRING) luaL_error(L, "%s.%s expects %s, got %s", desc->name, f->name, tn, luaL_typename(L, arg));
            // luaL_error does not return, so the message must be copied to the Lua stack first.
            char err[300];
            uint32_t value = 0;
            if (!reflect::parse_text_value(*f, lua_tostring(L, arg), &value, err, sizeof(err))) {
                lua_pushfstring(L, "%s.%s %s", desc->name, f->name, err);
                lua_error(L);
            }
            *(uint32_t*)p = value;
            break;
        }
        case FieldType::kVec2:
        case FieldType::kVec3:
        case FieldType::kVec4: {
            const int n = vector_size(f->type);
            if (t != LUA_TTABLE) luaL_error(L, "%s.%s expects %s {x, y...}, got %s", desc->name, f->name, tn, luaL_typename(L, arg));
            float v[4];
            for (int i = 0; i < n; ++i) {
                lua_getfield(L, arg, kAxes[i]);
                if (lua_type(L, -1) != LUA_TNUMBER) luaL_error(L, "%s.%s needs a number in .%s", desc->name, f->name, kAxes[i]);
                v[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
            memcpy(p, v, sizeof(float) * (size_t)n);
            break;
        }
    }
}

int zaro_set(lua_State* L) {
    scene::Entity* e = entity_arg(L, 1);
    const ComponentDesc* desc = nullptr;
    uint8_t* data = (uint8_t*)component_arg(L, e, 2, &desc);
    const FieldDesc* f = field_arg(L, desc, 3);
    store_field(L, desc, f, data + f->offset, 4);
    return 0;
}

// --- ui.* bindings (P2 step 4): view-model values, widget fields of the scene's screens, and
// commands for the app. Same rules as zaro.*: no objects with destructors.

const UiHost* host_of(lua_State* L, const char* what) {
    const UiHost* h = self(L)->host;
    if (!h) luaL_error(L, "%s: the scene has no ui", what);
    return h;
}

scene::Scene* screen_arg(lua_State* L, int arg) {
    const UiHost* h = host_of(L, "ui");
    const char* name = luaL_checklstring(L, arg, nullptr);
    if (scene::Scene* doc = h->find_screen(h->user, name)) return doc;
    const char* names[64];
    const char* hint = reflect::suggest_name(name, names, h->screen_names(h->user, names, 64));
    luaL_error(L, "screen '%s' is not shown%s%s%s", name, hint ? " (did you mean '" : "", hint ? hint : "",
               hint ? "'?)" : "");
}

// Raises err as a script error. The message is copied to the Lua stack first, because luaL_error
// does not return and err lives in the caller's frame.
void raise(lua_State* L, const char* err) {
    lua_pushstring(L, err);
    lua_error(L);
}

bool opt_flag(lua_State* L, int table, const char* key) {
    lua_getfield(L, table, key);
    const bool on = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return on;
}

int ui_open(lua_State* L) {
    const UiHost* h = host_of(L, "ui.open");
    const char* name = luaL_checklstring(L, 1, nullptr);
    const char* key = nullptr;
    bool wait = false, force = false, unskippable = false;
    if (lua_type(L, 2) == LUA_TTABLE) {
        lua_getfield(L, 2, "key");
        if (lua_type(L, -1) == LUA_TSTRING) key = lua_tostring(L, -1);  // stays on the stack while used
        wait = opt_flag(L, 2, "wait");
        force = opt_flag(L, 2, "force");
        unskippable = opt_flag(L, 2, "unskippable");
    } else if (!lua_isnoneornil(L, 2)) {
        luaL_error(L, "ui.open(screen, options): options must be a table, got %s", luaL_typename(L, 2));
    }
    char err[300];
    if (!h->open(h->user, name, key, wait, force, unskippable, err, sizeof(err))) raise(L, err);
    return 0;
}

int ui_ready(lua_State* L) {
    const UiHost* h = host_of(L, "ui.ready");
    const char* name = luaL_checklstring(L, 1, nullptr);
    if (!h->ready(h->user, name)) luaL_error(L, "ui.ready: no screen '%s' is opening", name);
    return 0;
}

int ui_close(lua_State* L) {
    const UiHost* h = host_of(L, "ui.close");
    lua_pushboolean(L, h->close(h->user, luaL_checklstring(L, 1, nullptr)));
    return 1;
}

int ui_back(lua_State* L) {
    const UiHost* h = host_of(L, "ui.back");
    h->back(h->user);
    return 0;
}

int ui_home(lua_State* L) {
    const UiHost* h = host_of(L, "ui.home");
    h->home(h->user);
    return 0;
}

int ui_abort_all(lua_State* L) {
    const UiHost* h = host_of(L, "ui.abort_all");
    h->abort_all(h->user);
    return 0;
}

int ui_state(lua_State* L) {
    const UiHost* h = host_of(L, "ui.state");
    lua_pushstring(L, h->state(h->user, luaL_checklstring(L, 1, nullptr)));
    return 1;
}

scene::Entity* node_arg(lua_State* L, scene::Scene* screen, int arg) {
    const char* name = luaL_checklstring(L, arg, nullptr);
    for (uint32_t i = 0; i < screen->entity_count; ++i) {
        if (strcmp(screen->entities[i].name, name) == 0) return &screen->entities[i];
    }
    luaL_error(L, "screen '%s' has no node '%s'", screen->name, name);
}

int ui_set(lua_State* L) {
    Script* s = self(L);
    const char* key = luaL_checklstring(L, 1, nullptr);
    if (!s->ui) luaL_error(L, "ui.set: the scene has no ui");
    const int t = lua_type(L, 2);
    if (t == LUA_TNUMBER) scene::set_value_number(s->ui, key, lua_tonumber(L, 2));
    else if (t == LUA_TSTRING) scene::set_value_string(s->ui, key, lua_tostring(L, 2));
    else luaL_error(L, "ui.set('%s', value) expects a number or a string, got %s", key, luaL_typename(L, 2));
    return 0;
}

int ui_get(lua_State* L) {
    Script* s = self(L);
    const char* key = luaL_checklstring(L, 1, nullptr);
    double number = 0.0;
    const char* string = nullptr;
    if (!s->ui || !scene::get_value(s->ui, key, &number, &string)) lua_pushnil(L);
    else if (string) lua_pushstring(L, string);
    else lua_pushnumber(L, number);
    return 1;
}

int ui_get_field(lua_State* L) {
    scene::Entity* e = node_arg(L, screen_arg(L, 1), 2);
    const ComponentDesc* desc = nullptr;
    const uint8_t* data = (const uint8_t*)component_arg(L, e, 3, &desc);
    const FieldDesc* f = field_arg(L, desc, 4);
    push_field(L, f, data + f->offset);
    return 1;
}

int ui_set_field(lua_State* L) {
    scene::Entity* e = node_arg(L, screen_arg(L, 1), 2);
    const ComponentDesc* desc = nullptr;
    uint8_t* data = (uint8_t*)component_arg(L, e, 3, &desc);
    const FieldDesc* f = field_arg(L, desc, 4);
    store_field(L, desc, f, data + f->offset, 5);
    return 0;
}

int ui_language(lua_State* L) {
    const UiHost* h = host_of(L, "ui.language");
    char err[300];
    if (!h->language(h->user, luaL_checklstring(L, 1, nullptr), err, sizeof(err))) raise(L, err);
    return 0;
}

int zaro_has(lua_State* L) {
    Script* s = self(L);
    const char* name = luaL_checklstring(L, 1, nullptr);
    const scene::Entity* e = scene::find_entity(s->scene, name);
    bool found = e != nullptr;
    if (found && lua_gettop(L) >= 2) {
        const ComponentDesc* desc = reflect::find_component(luaL_checklstring(L, 2, nullptr));
        found = desc && scene::get_component(e, desc);
    }
    lua_pushboolean(L, found);
    return 1;
}

int zaro_tick(lua_State* L) {
    lua_pushnumber(L, (double)self(L)->tick);
    return 1;
}

int zaro_log(lua_State* L) {
    const int n = lua_gettop(L);
    fprintf(stderr, "script:");
    for (int i = 1; i <= n; ++i) fprintf(stderr, " %s", luaL_tolstring(L, i, nullptr));
    fputc('\n', stderr);
    lua_settop(L, n);
    return 0;
}

bool read_file(const char* path, char** text, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *text = (char*)malloc((size_t)n + 1);
    *size = fread(*text, 1, (size_t)n, f);
    (*text)[*size] = 0;
    fclose(f);
    return true;
}

void arm(Script* s) {
    s->timed_out = false;
    s->deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                     std::chrono::duration<double>(s->max_call_seconds));
    s->deadline_armed = true;
}

// Compiles and runs the file's top level on the script thread.
bool run_file(Script* s, DiagList* diags) {
    char* text = nullptr;
    size_t size = 0;
    if (!read_file(s->path, &text, &size)) {
        diag_add(diags, kErrScriptCompile, s->path, 0, 0, "cannot open script");
        return false;
    }
    size_t bc_size = 0;
    char* bytecode = luau_compile(text, size, nullptr, &bc_size);
    free(text);
    const int load = luau_load(s->T, s->chunkname, bytecode, bc_size, 0);
    free(bytecode);
    if (load != 0) {
        report(s, kErrScriptCompile, lua_tostring(s->T, -1), diags);
        lua_pop(s->T, 1);
        return false;
    }
    arm(s);
    const int status = lua_pcall(s->T, 0, 0, 0);
    s->deadline_armed = false;
    if (status != 0) {
        report(s, kErrScriptRuntime, lua_tostring(s->T, -1), diags);
        lua_pop(s->T, 1);
        return false;
    }
    return true;
}

// Calls a global hook with the nargs values on top of the stack. *result (if given) is true when
// the hook returned true. A missing hook is not an error.
bool call_hook(Script* s, const char* name, int nargs, DiagList* diags, bool* result = nullptr) {
    if (result) *result = false;
    lua_getfield(s->T, LUA_GLOBALSINDEX, name);
    if (lua_type(s->T, -1) != LUA_TFUNCTION) {
        lua_pop(s->T, 1 + nargs);
        return true;
    }
    if (nargs) lua_insert(s->T, -(nargs + 1));  // function below its arguments
    arm(s);
    const int status = lua_pcall(s->T, nargs, result ? 1 : 0, 0);
    s->deadline_armed = false;
    if (status != 0) {
        report(s, kErrScriptRuntime, lua_tostring(s->T, -1), diags);
        lua_pop(s->T, 1);
        return false;
    }
    if (result) {
        *result = lua_toboolean(s->T, -1) != 0;
        lua_pop(s->T, 1);
    }
    return true;
}

}  // namespace

Script* load_script(const ScriptDesc& desc, DiagList* diags) {
    Script* s = new Script();
    snprintf(s->path, sizeof(s->path), "%s", desc.path);
    snprintf(s->chunkname, sizeof(s->chunkname), "=%s", desc.path);
    s->scene = desc.scene;
    s->ui = desc.ui;
    s->host = desc.host;
    s->max_call_seconds = desc.max_call_seconds;

    lua_State* L = luaL_newstate();
    s->L = L;
    lua_callbacks(L)->userdata = s;
    lua_callbacks(L)->interrupt = on_interrupt;
    luaL_openlibs(L);

    // No clock, date or file access: scripts must be deterministic and contained.
    lua_pushnil(L);
    lua_setglobal(L, "os");
    lua_pushnil(L);
    lua_setglobal(L, "loadstring");

    // math.random follows the scene seed.
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushnumber(L, (double)(desc.seed & 0xFFFFFFFFull));
    lua_call(L, 1, 0);
    lua_pop(L, 1);

    static const luaL_Reg kApi[] = {{"get", zaro_get}, {"set", zaro_set}, {"has", zaro_has},
                                    {"tick", zaro_tick}, {"log", zaro_log}, {nullptr, nullptr}};
    lua_createtable(L, 0, 5);
    for (const luaL_Reg* r = kApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "zaro");

    static const luaL_Reg kUiApi[] = {
        {"set", ui_set},         {"get", ui_get},     {"get_field", ui_get_field}, {"set_field", ui_set_field},
        {"language", ui_language}, {"open", ui_open}, {"ready", ui_ready},         {"close", ui_close},
        {"back", ui_back},       {"home", ui_home},   {"abort_all", ui_abort_all}, {"state", ui_state},
        {nullptr, nullptr}};
    lua_createtable(L, 0, 12);
    for (const luaL_Reg* r = kUiApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "ui");

    luaL_sandbox(L);  // standard library and `zaro` become read-only
    s->T = lua_newthread(L);
    s->thread_ref = lua_ref(L, -1);
    lua_pop(L, 1);
    luaL_sandboxthread(s->T);  // script globals live in a table of their own

    if (!run_file(s, diags)) {
        destroy_script(s);
        return nullptr;
    }
    return s;
}

void destroy_script(Script* s) {
    if (!s) return;
    if (s->L) lua_close(s->L);
    delete s;
}

bool call_start(Script* s, DiagList* diags) { return call_hook(s, "on_start", 0, diags); }

bool call_tick(Script* s, uint64_t tick, DiagList* diags) {
    s->tick = tick;
    lua_pushnumber(s->T, (double)tick);
    return call_hook(s, "on_tick", 1, diags);
}

bool call_ui(Script* s, const char* action, const char* screen, const char* node, DiagList* diags) {
    lua_pushstring(s->T, action);
    lua_pushstring(s->T, screen);
    lua_pushstring(s->T, node);
    return call_hook(s, "on_ui", 3, diags);
}

bool call_back(Script* s, const char* screen, bool* claimed, DiagList* diags) {
    lua_pushstring(s->T, screen);
    return call_hook(s, "on_back", 1, diags, claimed);
}

bool call_screen(Script* s, const char* event, const char* screen, DiagList* diags) {
    lua_pushstring(s->T, event);
    lua_pushstring(s->T, screen);
    return call_hook(s, "on_screen", 2, diags);
}

bool reload(Script* s, DiagList* diags) { return run_file(s, diags); }

const char* script_path(const Script* s) { return s->path; }

}  // namespace zaro::script
