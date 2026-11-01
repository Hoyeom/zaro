#include "scene/tween.h"

#include "core/detmath.h"
#include "scene/scene.h"
#include "scene/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::scene {

using reflect::ComponentDesc;
using reflect::FieldDesc;
using reflect::FieldType;

const char* const kEaseNames[] = {
    "linear",
    "in_quad", "out_quad", "in_out_quad",
    "in_cubic", "out_cubic", "in_out_cubic",
    "in_quart", "out_quart", "in_out_quart",
    "in_sine", "out_sine", "in_out_sine",
    "in_expo", "out_expo", "in_out_expo",
    "in_back", "out_back", "in_out_back",
    "in_elastic", "out_elastic", "in_out_elastic",
    "in_bounce", "out_bounce", "in_out_bounce",
    nullptr};
static_assert(sizeof(kEaseNames) / sizeof(kEaseNames[0]) == kEaseCount + 1, "one name per Ease");

const char* const kLoopNames[] = {"none", "repeat", "pingpong", nullptr};

// A running tween. Outside the anonymous namespace, as Scene points to it.
struct Tween {
    TweenSpec spec;
    uint32_t handle;
    uint32_t entity_id;  // 0: a screen node
    char screen[kNameCapacity];
    char node[kNameCapacity];
    uint64_t start_tick;
    float from[4];
    bool started;  // `from` is known
    bool removed;
};

namespace {

double sin_deg(double degrees) {
    double s = 0.0, c = 0.0;
    det_sincos_deg(degrees, &s, &c);
    return s;
}

double cos_deg(double degrees) {
    double s = 0.0, c = 0.0;
    det_sincos_deg(degrees, &s, &c);
    return c;
}

double out_bounce(double p) {
    const double n = 7.5625, d = 2.75;
    if (p < 1.0 / d) return n * p * p;
    if (p < 2.0 / d) {
        p -= 1.5 / d;
        return n * p * p + 0.75;
    }
    if (p < 2.5 / d) {
        p -= 2.25 / d;
        return n * p * p + 0.9375;
    }
    p -= 2.625 / d;
    return n * p * p + 0.984375;
}

}  // namespace

double ease(uint8_t curve, double p) {
    const double c1 = 1.70158, c2 = c1 * 1.525, c3 = c1 + 1.0;
    const double q = 1.0 - p;
    switch (curve) {
        case kEaseInQuad: return p * p;
        case kEaseOutQuad: return 1.0 - q * q;
        case kEaseInOutQuad: return p < 0.5 ? 2.0 * p * p : 1.0 - 2.0 * q * q;
        case kEaseInCubic: return p * p * p;
        case kEaseOutCubic: return 1.0 - q * q * q;
        case kEaseInOutCubic: return p < 0.5 ? 4.0 * p * p * p : 1.0 - 4.0 * q * q * q;
        case kEaseInQuart: return p * p * p * p;
        case kEaseOutQuart: return 1.0 - q * q * q * q;
        case kEaseInOutQuart: return p < 0.5 ? 8.0 * p * p * p * p : 1.0 - 8.0 * q * q * q * q;
        case kEaseInSine: return 1.0 - cos_deg(p * 90.0);
        case kEaseOutSine: return sin_deg(p * 90.0);
        case kEaseInOutSine: return (1.0 - cos_deg(p * 180.0)) * 0.5;
        case kEaseInExpo: return p <= 0.0 ? 0.0 : det_exp2(10.0 * p - 10.0);
        case kEaseOutExpo: return p >= 1.0 ? 1.0 : 1.0 - det_exp2(-10.0 * p);
        case kEaseInOutExpo:
            if (p <= 0.0 || p >= 1.0) return p <= 0.0 ? 0.0 : 1.0;
            return p < 0.5 ? det_exp2(20.0 * p - 10.0) * 0.5 : (2.0 - det_exp2(-20.0 * p + 10.0)) * 0.5;
        case kEaseInBack: return c3 * p * p * p - c1 * p * p;
        case kEaseOutBack: return 1.0 - (c3 * q * q * q - c1 * q * q);
        case kEaseInOutBack:
            return p < 0.5 ? (4.0 * p * p * ((c2 + 1.0) * 2.0 * p - c2)) * 0.5
                           : ((2.0 * p - 2.0) * (2.0 * p - 2.0) * ((c2 + 1.0) * (2.0 * p - 2.0) + c2) + 2.0) * 0.5;
        case kEaseInElastic:
            if (p <= 0.0 || p >= 1.0) return p <= 0.0 ? 0.0 : 1.0;
            return -det_exp2(10.0 * p - 10.0) * sin_deg((10.0 * p - 10.75) * 120.0);
        case kEaseOutElastic:
            if (p <= 0.0 || p >= 1.0) return p <= 0.0 ? 0.0 : 1.0;
            return det_exp2(-10.0 * p) * sin_deg((10.0 * p - 0.75) * 120.0) + 1.0;
        case kEaseInOutElastic:
            if (p <= 0.0 || p >= 1.0) return p <= 0.0 ? 0.0 : 1.0;
            return p < 0.5 ? -(det_exp2(20.0 * p - 10.0) * sin_deg((20.0 * p - 11.125) * 80.0)) * 0.5
                           : det_exp2(-20.0 * p + 10.0) * sin_deg((20.0 * p - 11.125) * 80.0) * 0.5 + 1.0;
        case kEaseInBounce: return 1.0 - out_bounce(q);
        case kEaseOutBounce: return out_bounce(p);
        case kEaseInOutBounce: return p < 0.5 ? (1.0 - out_bounce(1.0 - 2.0 * p)) * 0.5 : (1.0 + out_bounce(2.0 * p - 1.0)) * 0.5;
        default: return p;
    }
}

int find_ease(const char* name) {
    for (int i = 0; kEaseNames[i]; ++i) {
        if (strcmp(kEaseNames[i], name) == 0) return i;
    }
    return -1;
}

const char* tween_target_error(const ComponentDesc* c, const FieldDesc* f) {
    if (!reflect::is_visual(c)) {
        return "is gameplay state; tweens change only visual components (`\"visual\": true` in zaro schema) and UI "
               "widgets";
    }
    switch (f->type) {
        case FieldType::kF32:
        case FieldType::kVec2:
        case FieldType::kVec3:
        case FieldType::kVec4:
        case FieldType::kColor: return nullptr;
        default: return "is not a number, vector or colour field, so it cannot be tweened";
    }
}

uint32_t read_lanes(const FieldDesc* f, const void* data, float lanes[4]) {
    const uint8_t* p = (const uint8_t*)data + f->offset;
    switch (f->type) {
        case FieldType::kF32: lanes[0] = *(const float*)p; return 1;
        case FieldType::kVec2: memcpy(lanes, p, 8); return 2;
        case FieldType::kVec3: memcpy(lanes, p, 12); return 3;
        case FieldType::kVec4: memcpy(lanes, p, 16); return 4;
        case FieldType::kColor: {
            const uint32_t c = *(const uint32_t*)p;
            for (int i = 0; i < 4; ++i) lanes[i] = (float)((c >> (8 * i)) & 0xffu);
            return 4;
        }
        default: return 0;
    }
}

namespace {

void write_lanes(const FieldDesc* f, void* data, const float lanes[4]) {
    uint8_t* p = (uint8_t*)data + f->offset;
    switch (f->type) {
        case FieldType::kF32: *(float*)p = lanes[0]; break;
        case FieldType::kVec2: memcpy(p, lanes, 8); break;
        case FieldType::kVec3: memcpy(p, lanes, 12); break;
        case FieldType::kVec4: memcpy(p, lanes, 16); break;
        case FieldType::kColor: {
            uint32_t c = 0;
            for (int i = 0; i < 4; ++i) {
                const float v = lanes[i] < 0.0f ? 0.0f : (lanes[i] > 255.0f ? 255.0f : lanes[i]);
                c |= (uint32_t)floorf(v + 0.5f) << (8 * i);
            }
            *(uint32_t*)p = c;
            break;
        }
        default: break;
    }
}

bool same_target(const Tween& t, uint32_t entity_id, const char* screen, const char* node, const ComponentDesc* c,
                 const FieldDesc* f) {
    if (t.removed || t.spec.component != c || (f && t.spec.field != f) || t.entity_id != entity_id) return false;
    return entity_id || (strcmp(t.screen, screen) == 0 && strcmp(t.node, node) == 0);
}

// The component data a tween writes, or nullptr when its entity, screen or node is gone.
void* target_data(Scene* s, const Tween& t, FindScreenFn find_screen, void* user) {
    const Entity* e = nullptr;
    if (t.entity_id) {
        e = entity_by_id(s, t.entity_id);
    } else if (find_screen) {
        Scene* doc = find_screen(user, t.screen);
        e = doc ? find_entity(doc, t.node) : nullptr;
    }
    return e ? (void*)get_component(e, t.spec.component) : nullptr;
}

}  // namespace

void start_tween(Scene* s, const TweenSpec& spec, uint32_t entity_id, const char* screen, const char* node,
                 uint32_t handle, uint64_t tick) {
    stop_tweens(s, entity_id, screen, node, spec.component, spec.field);
    if (s->tween_count == s->tween_capacity) {
        s->tween_capacity = s->tween_capacity ? s->tween_capacity * 2 : 16;
        s->tweens = (Tween*)realloc(s->tweens, sizeof(Tween) * s->tween_capacity);
    }
    Tween& t = s->tweens[s->tween_count++];
    memset(&t, 0, sizeof(t));
    t.spec = spec;
    t.handle = handle;
    t.entity_id = entity_id;
    if (!entity_id) {
        snprintf(t.screen, sizeof(t.screen), "%s", screen);
        snprintf(t.node, sizeof(t.node), "%s", node);
    }
    t.start_tick = tick;
}

bool cancel_tween(Scene* s, uint32_t handle) {
    for (uint32_t i = 0; handle && i < s->tween_count; ++i) {
        if (!s->tweens[i].removed && s->tweens[i].handle == handle) {
            s->tweens[i].removed = true;
            return true;
        }
    }
    return false;
}

void stop_tweens(Scene* s, uint32_t entity_id, const char* screen, const char* node, const ComponentDesc* c,
                 const FieldDesc* f) {
    for (uint32_t i = 0; i < s->tween_count; ++i) {
        if (same_target(s->tweens[i], entity_id, screen, node, c, f)) s->tweens[i].removed = true;
    }
}

uint32_t step_tweens(Scene* s, uint64_t tick, FindScreenFn find_screen, void* user, uint32_t* done, uint32_t max_done) {
    uint32_t ndone = 0;
    for (uint32_t i = 0; i < s->tween_count; ++i) {
        Tween& t = s->tweens[i];
        if (t.removed) continue;
        void* data = target_data(s, t, find_screen, user);
        if (!data) {
            t.removed = true;
            continue;
        }
        const double elapsed = (double)(tick - t.start_tick) / 60.0 - t.spec.delay;
        if (elapsed < 0.0) continue;
        float lanes[4];
        const uint32_t n = read_lanes(t.spec.field, data, lanes);
        if (!t.started) {
            memcpy(t.from, t.spec.has_from ? t.spec.from : lanes, sizeof(t.from));
            t.started = true;
        }
        const double runs = elapsed / t.spec.time;
        const double whole = floor(runs);
        double p = runs - whole;
        bool finished = false;
        if (t.spec.loop == kLoopNone) {
            finished = runs >= 1.0;
            if (finished) p = 1.0;
        } else if (t.spec.count && whole >= (double)t.spec.count) {
            finished = true;
            p = t.spec.loop == kLoopPingPong && (t.spec.count % 2 == 0) ? 0.0 : 1.0;
        } else if (t.spec.loop == kLoopPingPong && fmod(whole, 2.0) == 1.0) {
            p = 1.0 - p;
        }
        const double k = ease(t.spec.ease, p);
        for (uint32_t l = 0; l < n; ++l) lanes[l] = (float)(t.from[l] + (t.spec.to[l] - t.from[l]) * k);
        write_lanes(t.spec.field, data, lanes);
        if (finished) {
            t.removed = true;
            if (t.handle && ndone < max_done) done[ndone++] = t.handle;
        }
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s->tween_count; ++i) {
        if (!s->tweens[i].removed) s->tweens[kept++] = s->tweens[i];
    }
    s->tween_count = kept;
    return ndone;
}

void free_tweens(Scene* s) {
    free(s->tweens);
    s->tweens = nullptr;
    s->tween_count = s->tween_capacity = 0;
}

}  // namespace zaro::scene
