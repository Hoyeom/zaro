#include "ui/navigator.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace zaro::ui {

namespace {

constexpr uint32_t kMaxEntries = 32;
constexpr uint32_t kMaxCatalog = 64;
constexpr uint32_t kMaxNavEvents = 64;
constexpr float kPopScale = 0.92f;  // a "pop" screen grows from this scale while it fades in

}  // namespace

struct Entry {
    scene::Scene* doc;
    Screen* screen;
    char key[64];
    EntryState state;
    float t;  // transition progress: 0 hidden, 1 fully shown
    bool root;
    bool dismissible;
    bool skippable;
};

struct CatalogEntry {
    char name[scene::kNameCapacity];
    char path[scene::kPathCapacity];
};

struct Navigator {
    Context* context = nullptr;
    Entry entries[kMaxEntries];
    uint32_t count = 0;
    CatalogEntry catalog[kMaxCatalog];
    uint32_t catalog_count = 0;
    NavEvent events[kMaxNavEvents];
    uint32_t event_count = 0;
    float width = 0.0f, height = 0.0f;
    double time = 0.0;  // drives the spinner
    bool captured = false;
};

namespace {

void push_event(Navigator* nav, NavEventKind kind, const char* screen) {
    if (nav->event_count == kMaxNavEvents) return;
    NavEvent& e = nav->events[nav->event_count++];
    e.kind = kind;
    snprintf(e.screen, sizeof(e.screen), "%s", screen);
}

// Releases an entry's screen and document and removes it from the stack. Exactly once per entry.
void remove_entry(Navigator* nav, uint32_t i, NavEventKind event) {
    Entry& e = nav->entries[i];
    push_event(nav, event, e.doc->name);
    destroy_screen(e.screen);
    scene::free_scene(e.doc);
    for (uint32_t k = i + 1; k < nav->count; ++k) nav->entries[k - 1] = nav->entries[k];
    nav->count--;
}

float transition_time(const Entry& e) {
    return e.doc->transition == scene::kTransitionNone ? 0.0f : e.doc->transition_time;
}

void become_open(Navigator* nav, Entry& e) {
    e.state = EntryState::kOpen;
    e.t = 1.0f;
    push_event(nav, NavEventKind::kOpened, e.doc->name);
}

void start_entrance(Navigator* nav, Entry& e) {
    e.t = 0.0f;
    if (transition_time(e) <= 0.0f) become_open(nav, e);
    else e.state = EntryState::kEntering;
}

// Starts the exit, or removes the entry at once when it has no exit to show.
void start_closing(Navigator* nav, uint32_t i) {
    Entry& e = nav->entries[i];
    if (e.state == EntryState::kOpening) {
        remove_entry(nav, i, NavEventKind::kCancelled);
        return;
    }
    if (transition_time(e) <= 0.0f) {
        remove_entry(nav, i, NavEventKind::kClosed);
        return;
    }
    e.state = EntryState::kClosing;
}

int topmost_live(const Navigator* nav) {
    for (uint32_t i = nav->count; i-- > 0;) {
        const Entry& e = nav->entries[i];
        if (!e.root && e.state != EntryState::kClosing) return (int)i;
    }
    return -1;
}

float smoothstep(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

uint32_t scale_alpha(uint32_t color, float k) {
    const uint32_t a = (uint32_t)((float)(color >> 24) * k + 0.5f);
    return (color & 0x00ffffffu) | (a > 255u ? 255u : a) << 24;
}

// Fades (and for "pop" scales around the target centre) the quads drawn since `first`.
void apply_transition(Navigator* nav, DrawList* list, uint32_t first, const Entry& e) {
    const float k = smoothstep(e.t);
    const float s = e.doc->transition == scene::kTransitionPop ? kPopScale + (1.0f - kPopScale) * k : 1.0f;
    const float cx = nav->width * 0.5f, cy = nav->height * 0.5f;
    for (uint32_t i = first; i < list->count; ++i) {
        Quad& q = list->quads[i];
        q.color = scale_alpha(q.color, k);
        q.border_color = scale_alpha(q.border_color, k);
        if (s == 1.0f) continue;
        for (int c = 0; c < 4; c += 2) {
            q.rect[c] = cx + (q.rect[c] - cx) * s;
            q.rect[c + 1] = cy + (q.rect[c + 1] - cy) * s;
            q.clip[c] = cx + (q.clip[c] - cx) * s;
            q.clip[c + 1] = cy + (q.clip[c + 1] - cy) * s;
        }
        q.radius *= s;
        q.border *= s;
    }
}

// Eight dots on a ring, brightest at the leading one, turning once a second.
void draw_spinner(Navigator* nav, DrawList* list) {
    const float scale = nav->height / 720.0f;
    const float r = 18.0f * scale, dot = 4.0f * scale;
    const float cx = nav->width * 0.5f, cy = nav->height * 0.5f;
    const int lead = (int)(nav->time * 8.0) % 8;
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * 0.785398f;
        const float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        const uint32_t alpha = 60u + (uint32_t)(((i - lead + 8) % 8) * 25);
        draw_rect(list, x - dot, y - dot, x + dot, y + dot, rgba(255, 255, 255, alpha), dot);
    }
}

Entry* push_entry(Navigator* nav, scene::Scene* doc, bool root) {
    if (nav->count == kMaxEntries) return nullptr;
    Entry& e = nav->entries[nav->count++];
    e = Entry{};
    e.doc = doc;
    e.screen = build_screen(nav->context, doc);
    e.root = root;
    e.dismissible = doc->dismissible;
    e.skippable = doc->skippable;
    e.state = EntryState::kOpen;
    e.t = 1.0f;
    return &e;
}

}  // namespace

Navigator* create_navigator(Context* context) {
    Navigator* nav = new Navigator();
    nav->context = context;
    return nav;
}

void destroy_navigator(Navigator* nav) {
    if (!nav) return;
    for (uint32_t i = 0; i < nav->count; ++i) {
        destroy_screen(nav->entries[i].screen);
        scene::free_scene(nav->entries[i].doc);
    }
    delete nav;
}

bool add_root(Navigator* nav, const char* path, DiagList* diags) {
    scene::Scene* doc = scene::load_screen(path, diags);
    if (!doc) return false;
    if (!push_entry(nav, doc, true)) {
        diag_add(diags, kErrSceneStructure, path, 0, 0, "more than %u screens", kMaxEntries);
        scene::free_scene(doc);
        return false;
    }
    return true;
}

bool add_catalog(Navigator* nav, const char* path, DiagList* diags) {
    scene::Scene* doc = scene::load_screen(path, diags);
    if (!doc) return false;
    bool ok = nav->catalog_count < kMaxCatalog;
    for (uint32_t i = 0; ok && i < nav->catalog_count; ++i) ok = strcmp(nav->catalog[i].name, doc->name) != 0;
    if (ok) {
        CatalogEntry& c = nav->catalog[nav->catalog_count++];
        snprintf(c.name, sizeof(c.name), "%s", doc->name);
        snprintf(c.path, sizeof(c.path), "%s", path);
    } else {
        diag_add(diags, kErrSceneDuplicate, path, 0, 0, "screen name '%s' is already in the scene's screens",
                 doc->name);
    }
    scene::free_scene(doc);
    return ok;
}

bool in_catalog(const Navigator* nav, const char* name) {
    for (uint32_t i = 0; i < nav->catalog_count; ++i) {
        if (strcmp(nav->catalog[i].name, name) == 0) return true;
    }
    return false;
}

uint32_t catalog_names(const Navigator* nav, const char** names, uint32_t max) {
    uint32_t n = 0;
    for (; n < nav->catalog_count && n < max; ++n) names[n] = nav->catalog[n].name;
    return n;
}

bool open(Navigator* nav, const char* name, const OpenOptions& o, DiagList* diags) {
    const CatalogEntry* c = nullptr;
    for (uint32_t i = 0; i < nav->catalog_count; ++i) {
        if (strcmp(nav->catalog[i].name, name) == 0) c = &nav->catalog[i];
    }
    if (!c) {
        const char* names[kMaxCatalog];
        const char* hint = reflect::suggest_name(name, names, catalog_names(nav, names, kMaxCatalog));
        diag_add(diags, kErrSceneStructure, "", 0, 0, "no screen named '%s' in the scene's screens%s%s%s", name,
                 hint ? " (did you mean '" : "", hint ? hint : "", hint ? "'?)" : "");
        return false;
    }
    // Reuse is the caller's choice: with a key, an open instance that has it comes to the top.
    if (o.key && o.key[0]) {
        for (uint32_t i = 0; i < nav->count; ++i) {
            const Entry& e = nav->entries[i];
            if (e.root || e.state == EntryState::kClosing || strcmp(e.doc->name, name) != 0 ||
                strcmp(e.key, o.key) != 0) {
                continue;
            }
            const Entry moved = e;
            for (uint32_t k = i + 1; k < nav->count; ++k) nav->entries[k - 1] = nav->entries[k];
            nav->entries[nav->count - 1] = moved;
            return true;
        }
    }
    scene::Scene* doc = scene::load_screen(c->path, diags);
    if (!doc) return false;
    Entry* e = push_entry(nav, doc, false);
    if (!e) {
        diag_add(diags, kErrSceneStructure, c->path, 0, 0, "more than %u screens open", kMaxEntries);
        scene::free_scene(doc);
        return false;
    }
    snprintf(e->key, sizeof(e->key), "%s", o.key ? o.key : "");
    push_event(nav, NavEventKind::kCreated, name);
    e->dismissible = e->dismissible && !o.force;
    e->skippable = e->skippable && !o.unskippable;
    if (o.wait) {
        e->state = EntryState::kOpening;
        e->t = 0.0f;
    } else {
        start_entrance(nav, *e);
    }
    return true;
}

bool ready(Navigator* nav, const char* name) {
    for (uint32_t i = nav->count; i-- > 0;) {
        Entry& e = nav->entries[i];
        if (e.state == EntryState::kOpening && strcmp(e.doc->name, name) == 0) {
            start_entrance(nav, e);
            return true;
        }
    }
    return false;
}

bool close(Navigator* nav, const char* name) {
    for (uint32_t i = nav->count; i-- > 0;) {
        const Entry& e = nav->entries[i];
        if (!e.root && e.state != EntryState::kClosing && strcmp(e.doc->name, name) == 0) {
            start_closing(nav, i);
            return true;
        }
    }
    return false;
}

bool close_screen(Navigator* nav, const Screen* screen) {
    for (uint32_t i = 0; i < nav->count; ++i) {
        const Entry& e = nav->entries[i];
        if (e.screen == screen && !e.root && e.state != EntryState::kClosing) {
            start_closing(nav, i);
            return true;
        }
    }
    return false;
}

BackResult back(Navigator* nav, bool claimed) {
    if (claimed) return BackResult::kClaimed;
    const int top = topmost_live(nav);
    if (top < 0) {
        push_event(nav, NavEventKind::kBackAtRoot, "");
        return BackResult::kBackAtRoot;
    }
    Entry& e = nav->entries[top];
    switch (e.state) {
        case EntryState::kOpening:
            if (!e.dismissible) return BackResult::kDropped;
            remove_entry(nav, (uint32_t)top, NavEventKind::kCancelled);
            return BackResult::kCancelled;
        case EntryState::kEntering:
            if (!e.dismissible || !e.skippable) return BackResult::kDropped;
            become_open(nav, e);
            return BackResult::kSnapped;
        case EntryState::kOpen:
            if (!e.dismissible) return BackResult::kConsumed;
            start_closing(nav, (uint32_t)top);
            return BackResult::kClosing;
        case EntryState::kClosing: break;
    }
    return BackResult::kDropped;
}

void home(Navigator* nav) {
    for (int top = topmost_live(nav); top >= 0; top = topmost_live(nav)) {
        if (!nav->entries[top].dismissible) return;
        start_closing(nav, (uint32_t)top);
    }
}

void abort_all(Navigator* nav) {
    for (uint32_t i = nav->count; i-- > 0;) {
        if (!nav->entries[i].root) remove_entry(nav, i, NavEventKind::kClosed);
    }
}

void update(Navigator* nav, const UiInput& input, float width, float height, float dt) {
    nav->width = width;
    nav->height = height;
    nav->time += dt;
    for (uint32_t i = 0; i < nav->count;) {
        Entry& e = nav->entries[i];
        const float duration = transition_time(e);
        if (e.state == EntryState::kEntering) {
            e.t += duration > 0.0f ? dt / duration : 1.0f;
            if (e.t >= 1.0f) become_open(nav, e);
        } else if (e.state == EntryState::kClosing) {
            e.t -= duration > 0.0f ? dt / duration : 1.0f;
            if (e.t <= 0.0f) {
                remove_entry(nav, i, NavEventKind::kClosed);
                continue;
            }
        }
        ++i;
    }
    UiInput in = input;
    in.nav_back = false;  // back goes through back(), never through a screen
    nav->captured = false;
    for (uint32_t i = nav->count; i-- > 0;) {
        const Entry& e = nav->entries[i];
        const bool takes_input = e.state == EntryState::kOpen;
        Screen* s = e.screen;
        update_screen(s, takes_input ? in : UiInput{}, width, height, screen_scale(s, width, height), dt);
        if (e.state == EntryState::kOpening) continue;  // nothing of it is shown yet
        // An open modal takes all input; a screen that holds the pointer or the focus hides that
        // input from the screens below.
        if (has_open_modal(s)) {
            nav->captured = true;
            in = UiInput{};
            continue;
        }
        if (takes_input && pointer_captured(s)) {
            nav->captured = true;
            in.pointer_down = in.pointer_pressed = in.pointer_released = false;
            in.pointer_x = in.pointer_y = -1.0f;
            in.wheel_y = 0.0f;
        }
        if (takes_input && focused_node(s) != kNoNode) {
            in.nav_up = in.nav_down = in.nav_left = in.nav_right = in.nav_confirm = false;
        }
    }
}

void draw(Navigator* nav, DrawList* list) {
    for (uint32_t i = 0; i < nav->count; ++i) {
        const Entry& e = nav->entries[i];
        if (e.state == EntryState::kOpening) {
            draw_spinner(nav, list);
            continue;
        }
        const uint32_t first = list->count;
        draw_screen(e.screen, list);
        if (e.state == EntryState::kEntering || e.state == EntryState::kClosing) apply_transition(nav, list, first, e);
    }
}

uint32_t entry_count(const Navigator* nav) { return nav->count; }
Screen* entry_screen(const Navigator* nav, uint32_t i) { return i < nav->count ? nav->entries[i].screen : nullptr; }
scene::Scene* entry_document(const Navigator* nav, uint32_t i) { return i < nav->count ? nav->entries[i].doc : nullptr; }
EntryState entry_state(const Navigator* nav, uint32_t i) { return nav->entries[i].state; }
bool entry_is_root(const Navigator* nav, uint32_t i) { return nav->entries[i].root; }

int find_entry(const Navigator* nav, const char* name) {
    int closing = -1;
    for (uint32_t i = nav->count; i-- > 0;) {
        if (strcmp(nav->entries[i].doc->name, name) != 0) continue;
        if (nav->entries[i].state != EntryState::kClosing) return (int)i;
        if (closing < 0) closing = (int)i;
    }
    return closing;
}

int focus_owner(const Navigator* nav) {
    for (uint32_t i = nav->count; i-- > 0;) {
        const Entry& e = nav->entries[i];
        if (e.state == EntryState::kOpen) return (int)i;
        if (e.state != EntryState::kOpening && has_open_modal(e.screen)) return -1;
    }
    return -1;
}

bool input_captured(const Navigator* nav) { return nav->captured; }

uint32_t event_count(const Navigator* nav) { return nav->event_count; }
const NavEvent* event_at(const Navigator* nav, uint32_t i) { return i < nav->event_count ? &nav->events[i] : nullptr; }
void clear_events(Navigator* nav) { nav->event_count = 0; }

const char* entry_state_name(EntryState state) {
    switch (state) {
        case EntryState::kOpening: return "opening";
        case EntryState::kEntering: return "entering";
        case EntryState::kOpen: return "open";
        case EntryState::kClosing: return "closing";
    }
    return "closed";
}

}  // namespace zaro::ui
