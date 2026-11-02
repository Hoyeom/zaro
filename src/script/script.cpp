#include "script/script.h"

#include "core/reflect.h"
#include "scene/audio_state.h"
#include "scene/save_spec.h"
#include "scene/ui_state.h"
#include "scene/tween.h"
#include "scene/world.h"

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

// A zaro.after / zaro.every timer: fires before on_tick of tick `due`.
struct Timer {
    uint32_t handle;
    int ref;            // the callback in the registry
    uint64_t due;
    uint64_t interval;  // 0 for zaro.after
    int64_t remaining;  // firings left; -1 forever
    bool cancelled;
};

// A tween's `done` callback.
struct TweenDone {
    uint32_t handle;
    int ref;
};

// A zaro.on subscription.
struct Handler {
    uint32_t handle;
    int ref;
    char event[kEventNameCapacity];
    bool cancelled;
};

struct Script {
    lua_State* L = nullptr;  // main state: read-only standard library and `zaro`
    lua_State* T = nullptr;  // sandboxed thread whose globals belong to the script
    int thread_ref = 0;
    char path[scene::kPathCapacity] = {};
    char chunkname[scene::kPathCapacity + 1] = {};
    scene::Scene* scene = nullptr;
    scene::UiState* ui = nullptr;
    const UiHost* host = nullptr;
    const CrowdHost* crowd = nullptr;
    scene::AudioState* audio = nullptr;
    scene::SaveData* save = nullptr;
    uint64_t tick = 0;
    double max_call_seconds = 1.0;
    Clock::time_point deadline;
    bool deadline_armed = false;
    bool timed_out = false;
    // Timers, subscriptions and the event queue (D-031). Handles share one counter, so
    // zaro.cancel takes any of them.
    uint32_t next_handle = 1;
    Timer* timers = nullptr;
    uint32_t timer_count = 0, timer_capacity = 0;
    Handler* handlers = nullptr;
    uint32_t handler_count = 0, handler_capacity = 0;
    TweenDone* tween_done = nullptr;
    uint32_t tween_done_count = 0, tween_done_capacity = 0;
    int queue_ref = 0;  // registry table: name, payload, name, payload, ... from queue_head on
    int queue_head = 1, queue_tail = 1;
    uint32_t events_this_tick = 0;  // delivered since the last on_tick (kMaxEventsPerTick)
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

// Raises err as a script error at the calling line, like luaL_error. The message is copied to the
// Lua stack first, because lua_error does not return and err lives in the caller's frame.
void raise(lua_State* L, const char* err) {
    luaL_where(L, 1);
    lua_pushstring(L, err);
    lua_concat(L, 2);
    lua_error(L);
}

// An entity by id (number) or name (string), or nullptr when there is no such live entity.
scene::Entity* entity_opt(lua_State* L, int arg) {
    Script* s = self(L);
    if (lua_type(L, arg) == LUA_TNUMBER) {
        const double id = lua_tonumber(L, arg);
        scene::Entity* e = id >= 1 && id <= 4294967295.0 ? scene::entity_by_id(s->scene, (uint32_t)id) : nullptr;
        return e && (double)e->id == id ? e : nullptr;
    }
    return scene::entity_by_name(s->scene, luaL_checklstring(L, arg, nullptr));
}

// The same, raising for an unknown or destroyed entity.
scene::Entity* entity_arg(lua_State* L, int arg) {
    Script* s = self(L);
    if (scene::Entity* e = entity_opt(L, arg)) return e;
    if (lua_type(L, arg) == LUA_TNUMBER) {
        luaL_error(L, "no entity with id %g (it was destroyed, or never existed)", lua_tonumber(L, arg));
    }
    const char* name = lua_tostring(L, arg);
    const char* hint = scene::suggest_entity(s->scene, name);
    luaL_error(L, "no entity named '%s' in scene '%s'%s%s%s", name, s->scene->name, hint ? " (did you mean '" : "",
               hint ? hint : "", hint ? "'?)" : "");
}

// A scene component by name, raising with "did you mean" for an unknown one.
const ComponentDesc* component_name_arg(lua_State* L, const char* name) {
    const ComponentDesc* desc = reflect::find_component(name);
    if (!desc || desc->kind != reflect::ComponentKind::kScene) {
        const char* hint = reflect::suggest_component(name);
        luaL_error(L, "unknown component '%s'%s%s%s", name, hint ? " (did you mean '" : "", hint ? hint : "",
                   hint ? "'?)" : "");
    }
    return desc;
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
    if (reflect::is_visual(desc)) scene::stop_tweens(self(L)->scene, e->id, nullptr, nullptr, desc, f);
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

int ui_reset(lua_State* L) {
    const UiHost* h = host_of(L, "ui.reset");
    h->reset(h->user);
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
    scene::stop_tweens(self(L)->scene, 0, lua_tostring(L, 1), e->name, desc, f);
    return 0;
}

int ui_tween(lua_State* L);

int ui_language(lua_State* L) {
    const UiHost* h = host_of(L, "ui.language");
    char err[300];
    if (!h->language(h->user, luaL_checklstring(L, 1, nullptr), err, sizeof(err))) raise(L, err);
    return 0;
}

// --- audio.* (D-032). Requests go into scene::AudioState; nothing comes back from the mixer.

scene::AudioState* audio_of(lua_State* L, const char* what) {
    Script* s = self(L);
    if (!s->audio) luaL_error(L, "%s: the scene has no sounds (add sounds = \"<file>\" to [scene])", what);
    return s->audio;
}

// The index of a named sound or group, or a script error with "did you mean".
uint32_t sound_name_arg(lua_State* L, const scene::SoundBank* bank, int arg, bool group) {
    const char* name = luaL_checklstring(L, arg, nullptr);
    const int i = group ? scene::find_sound_group(bank, name) : scene::find_sound(bank, name);
    if (i < 0) {
        const char* hint = group ? scene::suggest_sound_group(bank, name) : scene::suggest_sound(bank, name);
        char err[300];
        snprintf(err, sizeof(err), "%s has no %s '%s'%s%s%s", bank->path, group ? "group" : "sound", name,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        raise(L, err);
    }
    return (uint32_t)i;
}

// An optional number field of an options table, checked against [lo, hi].
float option_number(lua_State* L, int table, const char* key, float fallback, double lo, double hi) {
    if (lua_type(L, table) != LUA_TTABLE) return fallback;
    lua_getfield(L, table, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    if (!lua_isnumber(L, -1) || lua_tonumber(L, -1) < lo || lua_tonumber(L, -1) > hi) {
        luaL_error(L, "option %s must be a number in %g..%g", key, lo, hi);
    }
    const float x = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return x;
}

int sound_play(lua_State* L) {
    scene::AudioState* a = audio_of(L, "audio.play");
    const uint32_t sound = sound_name_arg(L, a->bank, 1, false);
    if (!lua_isnoneornil(L, 2)) luaL_checktype(L, 2, LUA_TTABLE);
    const float volume = option_number(L, 2, "volume", 1.0f, 0.0, 4.0);
    const float pitch = option_number(L, 2, "pitch", 0.0f, 0.25, 4.0);
    const float pan = option_number(L, 2, "pan", 0.0f, -1.0, 1.0);
    scene::audio_play(a, sound, volume, pitch, pan);
    return 0;
}

int sound_music(lua_State* L) {
    scene::AudioState* a = audio_of(L, "audio.music");
    const int sound = lua_isnoneornil(L, 1) ? -1 : (int)sound_name_arg(L, a->bank, 1, false);
    const double fade = luaL_optnumber(L, 2, 0.5);
    if (fade < 0.0 || fade > 60.0) luaL_error(L, "audio.music: fade must be in 0..60 seconds");
    scene::audio_music(a, sound, (float)fade);
    return 0;
}

int sound_track(lua_State* L) {
    const scene::AudioState* a = audio_of(L, "audio.track");
    if (a->music < 0) lua_pushnil(L);
    else lua_pushstring(L, a->bank->sounds[a->music].name);
    return 1;
}

int sound_volume(lua_State* L) {
    scene::AudioState* a = audio_of(L, "audio.volume");
    const uint32_t group = sound_name_arg(L, a->bank, 1, true);
    if (!lua_isnoneornil(L, 2)) {
        if (a->group_follows_save[group]) {
            luaL_error(L, "audio.volume: group '%s' follows the save field '%s'; change it with save.set",
                       a->bank->groups[group].name, a->bank->groups[group].save);
        }
        const double v = luaL_checknumber(L, 2);
        if (v < 0.0 || v > 1.0) luaL_error(L, "audio.volume: value must be in 0..1, got %g", v);
        scene::set_group_volume(a, group, (float)v);
    }
    lua_pushnumber(L, (double)a->group_volume[group]);
    return 1;
}

// --- save.* (D-033): the declared fields of the game's save.

scene::SaveData* save_of(lua_State* L, const char* what) {
    Script* s = self(L);
    if (!s->save) luaL_error(L, "%s: the scene has no save (add save = \"<file>\" to [scene])", what);
    return s->save;
}

uint32_t save_field_arg(lua_State* L, const scene::SaveData* data, int arg) {
    const char* name = luaL_checklstring(L, arg, nullptr);
    const int f = scene::find_save_field(data->spec, name);
    if (f < 0) {
        const char* hint = scene::suggest_save_field(data->spec, name);
        char err[300];
        snprintf(err, sizeof(err), "%s declares no field '%s'%s%s%s", data->spec->path, name,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        raise(L, err);
    }
    return (uint32_t)f;
}

int save_get(lua_State* L) {
    const scene::SaveData* data = save_of(L, "save.get");
    const scene::SaveValue& v = data->values[save_field_arg(L, data, 1)];
    switch (v.type) {
        case scene::kSaveInt: lua_pushnumber(L, (double)v.i); break;
        case scene::kSaveNumber: lua_pushnumber(L, v.n); break;
        case scene::kSaveBool: lua_pushboolean(L, v.b); break;
        default: lua_pushstring(L, v.s); break;
    }
    return 1;
}

int save_set(lua_State* L) {
    scene::SaveData* data = save_of(L, "save.set");
    const uint32_t f = save_field_arg(L, data, 1);
    scene::SaveValue v = {};
    switch (lua_type(L, 2)) {
        case LUA_TNUMBER: v.type = scene::kSaveNumber, v.n = lua_tonumber(L, 2); break;
        case LUA_TBOOLEAN: v.type = scene::kSaveBool, v.b = lua_toboolean(L, 2) != 0; break;
        case LUA_TSTRING: {
            size_t n = 0;
            const char* str = lua_tolstring(L, 2, &n);
            if (n >= sizeof(v.s)) luaL_error(L, "save.set: strings are at most %d bytes", (int)sizeof(v.s) - 1);
            v.type = scene::kSaveString;
            memcpy(v.s, str, n + 1);
            break;
        }
        default: luaL_error(L, "save.set expects a number, boolean or string, got %s", luaL_typename(L, 2));
    }
    char err[300];
    if (!scene::set_save_value(data, f, v, err, sizeof(err))) raise(L, err);
    return 0;
}

int save_commit(lua_State* L) {
    save_of(L, "save.commit")->commit_requested = true;
    return 0;
}

int zaro_has(lua_State* L) {
    const scene::Entity* e = entity_opt(L, 1);
    bool found = e != nullptr;
    if (found && lua_gettop(L) >= 2) {
        const ComponentDesc* desc = reflect::find_component(luaL_checklstring(L, 2, nullptr));
        found = desc && scene::get_component(e, desc);
    }
    lua_pushboolean(L, found);
    return 1;
}

// --- Entities at run time (D-031, scene/world.h).

int compare_names(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

int zaro_spawn(lua_State* L) {
    Script* s = self(L);
    const char* prefab = luaL_checklstring(L, 1, nullptr);
    const int p = scene::find_spawn_prefab(s->scene, prefab);
    if (p < 0) {
        const char* hint = scene::suggest_spawn_prefab(s->scene, prefab);
        luaL_error(L, "no prefab named '%s' in the scene's [scene] prefabs%s%s%s", prefab,
                   hint ? " (did you mean '" : "", hint ? hint : "",
                   hint ? "'?)" : (s->scene->prefab_count ? "" : " (it lists none)"));
    }
    const bool options = lua_type(L, 2) == LUA_TTABLE;
    if (!options && !lua_isnoneornil(L, 2)) {
        luaL_error(L, "zaro.spawn(prefab, options): options must be a table, got %s", luaL_typename(L, 2));
    }
    const char* name = nullptr;
    scene::Entity* parent = nullptr;
    // Component tables in the options, applied in name order so every run builds the same entity.
    constexpr int kMaxOverrides = 32;
    const char* comps[kMaxOverrides];
    int ncomps = 0;
    if (options) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "zaro.spawn: option keys must be strings");
            const char* key = lua_tostring(L, -2);  // the options table keeps its key strings alive
            if (strcmp(key, "name") == 0) {
                if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "zaro.spawn: name must be a string");
                name = lua_tostring(L, -1);
            } else if (strcmp(key, "parent") == 0) {
                parent = entity_arg(L, lua_gettop(L));
            } else {
                component_name_arg(L, key);
                if (lua_type(L, -1) != LUA_TTABLE) luaL_error(L, "zaro.spawn: %s must be a table of fields", key);
                if (ncomps == kMaxOverrides) luaL_error(L, "zaro.spawn: more than %d component tables", kMaxOverrides);
                comps[ncomps++] = key;
            }
            lua_pop(L, 1);
        }
    }
    qsort(comps, (size_t)ncomps, sizeof(comps[0]), compare_names);
    char err[200];
    const uint32_t parent_id = parent ? parent->id : 0;  // spawning may move the entities
    scene::Entity* e = scene::spawn_entity(s->scene, p, name, err, sizeof(err));
    if (!e) raise(L, err);
    e->parent_id = parent_id;
    for (int c = 0; c < ncomps; ++c) {
        const ComponentDesc* desc = reflect::find_component(comps[c]);
        uint8_t* data = (uint8_t*)scene::add_component(e, desc);
        lua_getfield(L, 2, comps[c]);
        const int fields = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, fields) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) luaL_error(L, "zaro.spawn: %s takes field names as keys", desc->name);
            lua_pushvalue(L, -2);
            const FieldDesc* f = field_arg(L, desc, lua_gettop(L));
            lua_pop(L, 1);
            store_field(L, desc, f, data + f->offset, lua_gettop(L));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pushnumber(L, (double)e->id);
    return 1;
}

// --- Hierarchies (D-031).

int zaro_parent(lua_State* L) {
    const scene::Entity* p = scene::parent_of(self(L)->scene, entity_arg(L, 1));
    if (p) lua_pushnumber(L, (double)p->id);
    else lua_pushnil(L);
    return 1;
}

int zaro_set_parent(lua_State* L) {
    scene::Entity* child = entity_arg(L, 1);
    scene::Entity* parent = lua_isnoneornil(L, 2) ? nullptr : entity_arg(L, 2);
    char err[200];
    if (!scene::set_parent(self(L)->scene, child, parent, err, sizeof(err))) raise(L, err);
    return 0;
}

int zaro_children(lua_State* L) {
    Script* s = self(L);
    const uint32_t id = entity_arg(L, 1)->id;
    lua_newtable(L);
    int n = 0;
    for (uint32_t i = 0; i < s->scene->entity_count; ++i) {
        const scene::Entity& e = s->scene->entities[i];
        if (e.destroyed || e.parent_id != id) continue;
        lua_pushnumber(L, (double)e.id);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

// zaro.world(entity) -> {x, y, z}, rotation, scale
int zaro_world(lua_State* L) {
    const scene::WorldTransform w = scene::world_transform(self(L)->scene, entity_arg(L, 1));
    lua_createtable(L, 0, 3);
    for (int i = 0; i < 3; ++i) {
        lua_pushnumber(L, w.position[i]);
        lua_setfield(L, -2, kAxes[i]);
    }
    lua_pushnumber(L, w.rotation);
    lua_pushnumber(L, w.scale);
    return 3;
}

int zaro_destroy(lua_State* L) {
    // A name that matches nothing is a typo and raises; an id that is gone was destroyed already.
    scene::Entity* e = lua_type(L, 1) == LUA_TNUMBER ? entity_opt(L, 1) : entity_arg(L, 1);
    if (e) scene::destroy_entity(self(L)->scene, e);
    lua_pushboolean(L, e != nullptr);
    return 1;
}

int zaro_alive(lua_State* L) {
    lua_pushboolean(L, entity_opt(L, 1) != nullptr);
    return 1;
}

int zaro_find(lua_State* L) {
    const scene::Entity* e = scene::entity_by_name(self(L)->scene, luaL_checklstring(L, 1, nullptr));
    if (e) lua_pushnumber(L, (double)e->id);
    else lua_pushnil(L);
    return 1;
}

int zaro_name(lua_State* L) {
    lua_pushstring(L, entity_arg(L, 1)->name);
    return 1;
}

int zaro_id(lua_State* L) {
    lua_pushnumber(L, (double)entity_arg(L, 1)->id);
    return 1;
}

// Counts the live entities that have every component named by the arguments 1..ncomps, in id
// order; with fill, also appends their ids to the table on top of the stack.
int collect_entities(lua_State* L, int ncomps, bool fill) {
    Script* s = self(L);
    constexpr int kMaxQuery = 8;
    if (ncomps > kMaxQuery) luaL_error(L, "a query names at most %d components", kMaxQuery);
    const ComponentDesc* want[kMaxQuery];
    for (int i = 0; i < ncomps; ++i) want[i] = component_name_arg(L, luaL_checklstring(L, i + 1, nullptr));
    int n = 0;
    for (uint32_t i = 0; i < s->scene->entity_count; ++i) {
        const scene::Entity* e = &s->scene->entities[i];
        bool all = !e->destroyed;
        for (int c = 0; c < ncomps && all; ++c) all = scene::get_component(e, want[c]) != nullptr;
        if (!all) continue;
        ++n;
        if (fill) {
            lua_pushnumber(L, (double)e->id);
            lua_rawseti(L, -2, n);
        }
    }
    return n;
}

int zaro_query(lua_State* L) {
    const int ncomps = lua_gettop(L);
    lua_newtable(L);
    collect_entities(L, ncomps, true);
    return 1;
}

int zaro_count(lua_State* L) {
    lua_pushnumber(L, collect_entities(L, lua_gettop(L), false));
    return 1;
}

// --- Timers and events (D-031). Callbacks are kept as registry refs and run in the fixed tick.

template <typename T>
T* grow(T* items, uint32_t count, uint32_t* capacity) {
    if (count < *capacity) return items;
    *capacity = *capacity ? *capacity * 2 : 16;
    return (T*)realloc(items, sizeof(T) * *capacity);
}

// Seconds of tick time as a whole number of ticks, at least one.
uint64_t ticks_arg(lua_State* L, int arg, const char* what) {
    const double seconds = luaL_checknumber(L, arg);
    if (!(seconds >= 0.0) || seconds > 1e7) luaL_error(L, "%s: seconds must be 0 or more, got %g", what, seconds);
    const uint64_t ticks = (uint64_t)floor(seconds * 60.0 + 0.5);
    return ticks ? ticks : 1;
}

int add_timer(lua_State* L, uint64_t ticks, uint64_t interval, int64_t times, int fn_arg) {
    Script* s = self(L);
    s->timers = grow(s->timers, s->timer_count, &s->timer_capacity);
    Timer& t = s->timers[s->timer_count++];
    t.handle = s->next_handle++;
    t.ref = lua_ref(L, fn_arg);
    t.due = s->tick + ticks;
    t.interval = interval;
    t.remaining = times;
    t.cancelled = false;
    lua_pushnumber(L, (double)t.handle);
    return 1;
}

int zaro_after(lua_State* L) {
    const uint64_t ticks = ticks_arg(L, 1, "zaro.after");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    return add_timer(L, ticks, 0, 1, 2);
}

int zaro_every(lua_State* L) {
    const uint64_t ticks = ticks_arg(L, 1, "zaro.every");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int64_t times = -1;
    if (!lua_isnoneornil(L, 3)) {
        const double n = luaL_checknumber(L, 3);
        if (n < 1 || floor(n) != n) luaL_error(L, "zaro.every: times must be a whole number of 1 or more, got %g", n);
        times = (int64_t)n;
    }
    return add_timer(L, ticks, ticks, times, 2);
}

const char* event_arg(lua_State* L, int arg, const char* what) {
    size_t len = 0;
    const char* event = luaL_checklstring(L, arg, &len);
    if (!len || len >= kEventNameCapacity) luaL_error(L, "%s: event names are 1 to %u bytes", what, kEventNameCapacity - 1);
    return event;
}

int zaro_on(lua_State* L) {
    Script* s = self(L);
    const char* event = event_arg(L, 1, "zaro.on");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    s->handlers = grow(s->handlers, s->handler_count, &s->handler_capacity);
    Handler& h = s->handlers[s->handler_count++];
    h.handle = s->next_handle++;
    h.ref = lua_ref(L, 2);
    snprintf(h.event, sizeof(h.event), "%s", event);
    h.cancelled = false;
    lua_pushnumber(L, (double)h.handle);
    return 1;
}

// Appends (name, the payload on top of the stack) to the event queue and pops the payload.
void enqueue(lua_State* L, Script* s, const char* name) {
    lua_getref(L, s->queue_ref);
    lua_pushstring(L, name);
    lua_rawseti(L, -2, s->queue_tail);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, s->queue_tail + 1);
    lua_pop(L, 2);
    s->queue_tail += 2;
}

int zaro_emit(lua_State* L) {
    const char* event = event_arg(L, 1, "zaro.emit");
    lua_settop(L, 2);  // the payload, or nil
    enqueue(L, self(L), event);
    return 0;
}

int zaro_cancel(lua_State* L) {
    Script* s = self(L);
    const double handle = luaL_checknumber(L, 1);
    bool found = false;
    for (uint32_t i = 0; i < s->timer_count && !found; ++i) {
        Timer& t = s->timers[i];
        if (!t.cancelled && (double)t.handle == handle) found = t.cancelled = true;
    }
    for (uint32_t i = 0; i < s->handler_count && !found; ++i) {
        Handler& h = s->handlers[i];
        if (!h.cancelled && (double)h.handle == handle) found = h.cancelled = true;
    }
    if (!found && handle >= 1 && handle <= 4294967295.0) found = scene::cancel_tween(s->scene, (uint32_t)handle);
    lua_pushboolean(L, found);
    return 1;
}

// --- Tweens (D-031, scene/tween.h).

// Reads a tween value for the field from the Lua value at `arg`: a number, {x, y[, z[, w]]} or a
// colour string.
void tween_lanes(lua_State* L, const ComponentDesc* desc, const FieldDesc* f, int arg, float lanes[4]) {
    uint8_t tmp[16] = {};
    FieldDesc local = *f;
    local.offset = 0;
    local.min = 1.0;  // a tween may overshoot (back, elastic), so no range check here
    local.max = 0.0;
    store_field(L, desc, &local, tmp, arg);
    scene::read_lanes(&local, tmp, lanes);
}

double opt_number(lua_State* L, int table, const char* key, double fallback) {
    lua_getfield(L, table, key);
    double v = fallback;
    if (lua_type(L, -1) == LUA_TNUMBER) v = lua_tonumber(L, -1);
    else if (!lua_isnil(L, -1)) luaL_error(L, "tween option %s must be a number", key);
    lua_pop(L, 1);
    return v;
}

// One of names[] for the string option `key`, or `fallback` when it is absent.
uint8_t opt_choice(lua_State* L, int table, const char* key, const char* const* names, uint8_t fallback) {
    lua_getfield(L, table, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    const char* v = lua_tostring(L, -1);
    uint32_t n = 0;
    while (names[n]) ++n;
    for (uint32_t i = 0; v && i < n; ++i) {
        if (strcmp(names[i], v) == 0) {
            lua_pop(L, 1);
            return (uint8_t)i;
        }
    }
    const char* hint = v ? reflect::suggest_name(v, names, n) : nullptr;
    luaL_error(L, "unknown tween %s '%s'%s%s%s", key, v ? v : "?", hint ? " (did you mean '" : "", hint ? hint : "",
               hint ? "'?)" : "");
}

// Builds the spec from (to, options) at `to_arg` and `to_arg + 1`; returns the handle it will use.
uint32_t tween_spec(lua_State* L, const ComponentDesc* desc, const FieldDesc* f, int to_arg, scene::TweenSpec* spec) {
    if (const char* why = scene::tween_target_error(desc, f)) luaL_error(L, "%s.%s %s", desc->name, f->name, why);
    const int opts = to_arg + 1;
    if (lua_type(L, opts) != LUA_TTABLE) luaL_error(L, "a tween needs options with at least {time = seconds}");
    memset(spec, 0, sizeof(*spec));
    spec->component = desc;
    spec->field = f;
    tween_lanes(L, desc, f, to_arg, spec->to);
    lua_getfield(L, opts, "from");
    if (!lua_isnil(L, -1)) {
        tween_lanes(L, desc, f, lua_gettop(L), spec->from);
        spec->has_from = true;
    }
    lua_pop(L, 1);
    spec->time = opt_number(L, opts, "time", -1.0);
    if (!(spec->time > 0.0)) luaL_error(L, "tween time must be seconds above 0");
    spec->delay = opt_number(L, opts, "delay", 0.0);
    if (spec->delay < 0.0) luaL_error(L, "tween delay must be 0 or more");
    spec->ease = opt_choice(L, opts, "ease", scene::kEaseNames, scene::kEaseLinear);
    spec->loop = opt_choice(L, opts, "loop", scene::kLoopNames, scene::kLoopNone);
    const double count = opt_number(L, opts, "count", 0.0);
    if (count < 0 || floor(count) != count) luaL_error(L, "tween count must be a whole number, 0 or more");
    spec->count = (uint32_t)count;
    Script* s = self(L);
    const uint32_t handle = s->next_handle++;
    lua_getfield(L, opts, "done");
    if (lua_type(L, -1) == LUA_TFUNCTION) {
        s->tween_done = grow(s->tween_done, s->tween_done_count, &s->tween_done_capacity);
        s->tween_done[s->tween_done_count++] = {handle, lua_ref(L, -1)};
    } else if (!lua_isnil(L, -1)) {
        luaL_error(L, "tween option done must be a function");
    }
    lua_pop(L, 1);
    return handle;
}

// zaro.tween(entity, component, field, to, {time, from, delay, ease, loop, count, done}) -> handle
int zaro_tween(lua_State* L) {
    Script* s = self(L);
    scene::Entity* e = entity_arg(L, 1);
    const ComponentDesc* desc = nullptr;
    component_arg(L, e, 2, &desc);
    const FieldDesc* f = field_arg(L, desc, 3);
    scene::TweenSpec spec;
    const uint32_t handle = tween_spec(L, desc, f, 4, &spec);
    scene::start_tween(s->scene, spec, e->id, nullptr, nullptr, handle, s->scene->tick);
    lua_pushnumber(L, (double)handle);
    return 1;
}

// zaro.crowd_kill(x, z, radius[, max[, kind]]) -> count, {[spawner name] = count, ...}
int zaro_crowd_kill(lua_State* L) {
    Script* s = self(L);
    const CrowdHost* c = s->crowd;
    const double x = luaL_checknumber(L, 1), z = luaL_checknumber(L, 2), r = luaL_checknumber(L, 3);
    const double max = luaL_optnumber(L, 4, 0.0);
    if (!(r >= 0.0)) luaL_error(L, "zaro.crowd_kill: radius must be 0 or more, got %g", r);
    if (max < 0 || floor(max) != max) luaL_error(L, "zaro.crowd_kill: max must be a whole number, 0 or more");
    const uint32_t* kind_ids = nullptr;
    const uint32_t kinds = c ? c->kinds(c->user, &kind_ids) : 0;
    uint32_t kind = 0xFFFFFFFFu;
    if (!lua_isnoneornil(L, 5)) {
        const scene::Entity* e = entity_arg(L, 5);
        for (uint32_t k = 0; k < kinds && kind == 0xFFFFFFFFu; ++k) {
            if (kind_ids[k] == e->id) kind = k;
        }
        if (kind == 0xFFFFFFFFu) {
            luaL_error(L, "zaro.crowd_kill: entity '%s' is not a kind of the crowd (an entity of the scene file "
                          "with a CrowdSpawner)", e->name);
        }
    }
    uint32_t per_kind[kMaxCrowdKinds] = {};
    const uint32_t killed = c ? c->kill(c->user, (float)x, (float)z, (float)r, (uint32_t)max, kind, per_kind) : 0;
    lua_pushnumber(L, (double)killed);
    lua_createtable(L, 0, 0);
    for (uint32_t k = 0; k < kinds && killed; ++k) {
        const scene::Entity* e = per_kind[k] ? scene::entity_by_id(s->scene, kind_ids[k]) : nullptr;
        if (!e) continue;
        lua_pushnumber(L, (double)per_kind[k]);
        lua_setfield(L, -2, e->name);
    }
    return 2;
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

// ui.tween(screen, node, component, field, to, options) -> handle; like zaro.tween on a widget.
int ui_tween(lua_State* L) {
    Script* s = self(L);
    scene::Entity* e = node_arg(L, screen_arg(L, 1), 2);
    const ComponentDesc* desc = nullptr;
    component_arg(L, e, 3, &desc);
    const FieldDesc* f = field_arg(L, desc, 4);
    scene::TweenSpec spec;
    const uint32_t handle = tween_spec(L, desc, f, 5, &spec);
    scene::start_tween(s->scene, spec, 0, lua_tostring(L, 1), e->name, handle, s->scene->tick);
    lua_pushnumber(L, (double)handle);
    return 1;
}

// Calls the registry function `ref` with the nargs values on top of the stack. Errors are ZE0501
// with the line inside the callback.
bool call_ref(Script* s, int ref, int nargs, DiagList* diags) {
    lua_getref(s->T, ref);
    if (nargs) lua_insert(s->T, -(nargs + 1));
    arm(s);
    const int status = lua_pcall(s->T, nargs, 0, 0);
    s->deadline_armed = false;
    if (status != 0) {
        report(s, kErrScriptRuntime, lua_tostring(s->T, -1), diags);
        lua_pop(s->T, 1);
        return false;
    }
    return true;
}

// Fires the timers due by s->tick, earliest first and in creation order within a tick. Timers a
// callback creates are at least one tick ahead, so they wait for a later tick.
bool fire_timers(Script* s, DiagList* diags) {
    bool ok = true;
    while (ok) {
        int next = -1;
        for (uint32_t i = 0; i < s->timer_count; ++i) {
            const Timer& t = s->timers[i];
            if (t.cancelled || t.due > s->tick) continue;
            if (next < 0 || t.due < s->timers[next].due ||
                (t.due == s->timers[next].due && t.handle < s->timers[next].handle)) {
                next = (int)i;
            }
        }
        if (next < 0) break;
        Timer& t = s->timers[next];
        const int ref = t.ref;
        if (t.remaining > 0) --t.remaining;
        if (t.remaining == 0) t.cancelled = true;
        else t.due += t.interval;
        lua_pushnumber(s->T, (double)s->tick);
        ok = call_ref(s, ref, 1, diags);  // may add timers, which can move s->timers
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->timer_count; ++i) {
        if (s->timers[i].cancelled) lua_unref(s->T, s->timers[i].ref);
        else s->timers[kept++] = s->timers[i];
    }
    s->timer_count = kept;
    return ok;
}

// Empties the event queue (after too many events in one tick).
void clear_queue(Script* s) {
    lua_newtable(s->T);
    lua_unref(s->T, s->queue_ref);
    s->queue_ref = lua_ref(s->T, -1);
    lua_pop(s->T, 1);
    s->queue_head = s->queue_tail = 1;
}

// Delivers queued events to their handlers, in emit order and subscription order, including the
// events handlers emit. A handler subscribed while an event is delivered gets only later events.
bool drain_events(Script* s, DiagList* diags) {
    bool ok = true;
    while (ok && s->queue_head < s->queue_tail) {
        lua_getref(s->T, s->queue_ref);
        const int queue = lua_gettop(s->T);
        lua_rawgeti(s->T, queue, s->queue_head);
        char name[kEventNameCapacity];
        snprintf(name, sizeof(name), "%s", lua_tostring(s->T, -1));
        lua_pop(s->T, 1);
        if (++s->events_this_tick > kMaxEventsPerTick) {
            diag_add(diags, kErrEventLimit, s->path, 0, 0,
                     "more than %u events in one tick (the last was '%s'); do handlers emit each other in a loop?",
                     kMaxEventsPerTick, name);
            lua_settop(s->T, queue - 1);
            clear_queue(s);
            ok = false;
            break;
        }
        lua_rawgeti(s->T, queue, s->queue_head + 1);  // the payload stays on the stack while delivered
        lua_pushnil(s->T);
        lua_rawseti(s->T, queue, s->queue_head);
        lua_pushnil(s->T);
        lua_rawseti(s->T, queue, s->queue_head + 1);
        s->queue_head += 2;
        const uint32_t n = s->handler_count;
        for (uint32_t i = 0; i < n && ok; ++i) {
            if (s->handlers[i].cancelled || strcmp(s->handlers[i].event, name) != 0) continue;
            lua_pushvalue(s->T, -1);
            lua_pushstring(s->T, name);
            ok = call_ref(s, s->handlers[i].ref, 2, diags);  // may subscribe, which can move s->handlers
        }
        lua_settop(s->T, queue - 1);
    }
    if (s->queue_head == s->queue_tail) s->queue_head = s->queue_tail = 1;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->handler_count; ++i) {
        if (s->handlers[i].cancelled) lua_unref(s->T, s->handlers[i].ref);
        else s->handlers[kept++] = s->handlers[i];
    }
    s->handler_count = kept;
    return ok;
}

}  // namespace

Script* load_script(const ScriptDesc& desc, DiagList* diags) {
    Script* s = new Script();
    snprintf(s->path, sizeof(s->path), "%s", desc.path);
    snprintf(s->chunkname, sizeof(s->chunkname), "=%s", desc.path);
    s->scene = desc.scene;
    s->ui = desc.ui;
    s->host = desc.host;
    s->crowd = desc.crowd;
    s->audio = desc.audio;
    s->save = desc.save;
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

    static const luaL_Reg kApi[] = {
        {"get", zaro_get},       {"set", zaro_set},     {"has", zaro_has},     {"tick", zaro_tick},
        {"log", zaro_log},       {"spawn", zaro_spawn}, {"destroy", zaro_destroy}, {"alive", zaro_alive},
        {"find", zaro_find},     {"name", zaro_name},   {"id", zaro_id},       {"query", zaro_query},
        {"count", zaro_count},   {"after", zaro_after}, {"every", zaro_every}, {"on", zaro_on},
        {"emit", zaro_emit},     {"cancel", zaro_cancel}, {"parent", zaro_parent}, {"set_parent", zaro_set_parent},
        {"children", zaro_children}, {"world", zaro_world}, {"tween", zaro_tween}, {"crowd_kill", zaro_crowd_kill}, {nullptr, nullptr}};
    lua_createtable(L, 0, 25);
    for (const luaL_Reg* r = kApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "zaro");

    static const luaL_Reg kUiApi[] = {
        {"set", ui_set},         {"get", ui_get},     {"get_field", ui_get_field}, {"set_field", ui_set_field},
        {"language", ui_language}, {"open", ui_open}, {"ready", ui_ready},         {"close", ui_close},
        {"back", ui_back},       {"home", ui_home},   {"reset", ui_reset}, {"state", ui_state},
        {"tween", ui_tween},     {nullptr, nullptr}};
    lua_createtable(L, 0, 13);
    for (const luaL_Reg* r = kUiApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "ui");

    static const luaL_Reg kAudioApi[] = {
        {"play", sound_play}, {"music", sound_music}, {"track", sound_track}, {"volume", sound_volume}, {nullptr, nullptr}};
    lua_createtable(L, 0, 4);
    for (const luaL_Reg* r = kAudioApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "audio");

    static const luaL_Reg kSaveApi[] = {{"get", save_get}, {"set", save_set}, {"commit", save_commit}, {nullptr, nullptr}};
    lua_createtable(L, 0, 3);
    for (const luaL_Reg* r = kSaveApi; r->name; ++r) {
        lua_pushcfunction(L, r->func, r->name);
        lua_setfield(L, -2, r->name);
    }
    lua_setreadonly(L, -1, 1);
    lua_setglobal(L, "save");

    luaL_sandbox(L);  // standard library and `zaro` become read-only
    s->T = lua_newthread(L);
    s->thread_ref = lua_ref(L, -1);
    lua_pop(L, 1);
    luaL_sandboxthread(s->T);  // script globals live in a table of their own
    lua_newtable(s->T);
    s->queue_ref = lua_ref(s->T, -1);
    lua_pop(s->T, 1);

    if (!run_file(s, diags)) {
        destroy_script(s);
        return nullptr;
    }
    return s;
}

void destroy_script(Script* s) {
    if (!s) return;
    if (s->L) lua_close(s->L);
    free(s->timers);
    free(s->handlers);
    free(s->tween_done);
    delete s;
}

// Every hook is followed by the events it (and the engine) queued.
bool call_start(Script* s, DiagList* diags) { return call_hook(s, "on_start", 0, diags) && drain_events(s, diags); }

bool call_tick(Script* s, uint64_t tick, DiagList* diags) {
    s->tick = tick;
    s->events_this_tick = 0;
    if (!fire_timers(s, diags) || !drain_events(s, diags)) return false;
    lua_pushnumber(s->T, (double)tick);
    return call_hook(s, "on_tick", 1, diags) && drain_events(s, diags);
}

bool call_ui(Script* s, const char* action, const char* screen, const char* node, DiagList* diags) {
    lua_pushstring(s->T, action);
    lua_pushstring(s->T, screen);
    lua_pushstring(s->T, node);
    return call_hook(s, "on_ui", 3, diags) && drain_events(s, diags);
}

bool call_back(Script* s, const char* screen, bool* claimed, DiagList* diags) {
    lua_pushstring(s->T, screen);
    return call_hook(s, "on_back", 1, diags, claimed) && drain_events(s, diags);
}

bool call_screen(Script* s, const char* event, const char* screen, DiagList* diags) {
    lua_pushstring(s->T, event);
    lua_pushstring(s->T, screen);
    return call_hook(s, "on_screen", 2, diags) && drain_events(s, diags);
}

bool finish_tweens(Script* s, const uint32_t* handles, uint32_t count, DiagList* diags) {
    bool ok = true;
    for (uint32_t i = 0; i < count && ok; ++i) {
        for (uint32_t j = 0; j < s->tween_done_count; ++j) {
            if (s->tween_done[j].handle != handles[i]) continue;
            const int ref = s->tween_done[j].ref;
            s->tween_done[j] = s->tween_done[--s->tween_done_count];
            lua_pushnumber(s->T, (double)handles[i]);
            ok = call_ref(s, ref, 1, diags);
            lua_unref(s->T, ref);
            break;
        }
    }
    return drain_events(s, diags) && ok;
}

bool emit_event(Script* s, const char* name, const char* const* keys, const double* values, uint32_t count) {
    if (!name[0] || strlen(name) >= kEventNameCapacity) return false;
    lua_createtable(s->T, 0, (int)count);
    for (uint32_t i = 0; i < count; ++i) {
        lua_pushnumber(s->T, values[i]);
        lua_setfield(s->T, -2, keys[i]);
    }
    enqueue(s->T, s, name);
    return true;
}

bool reload(Script* s, DiagList* diags) { return run_file(s, diags); }

const char* script_path(const Script* s) { return s->path; }

}  // namespace zaro::script
