// The navigation policy (D-021) without a GPU or fonts: lifecycle, back in every state, claims,
// close, home, abort-all, reuse by key, and input blocking between stacked screens.
// Run from the repository root (fixtures in tests/data/screens/nav/).
#include "core/diag.h"
#include "ui/context.h"
#include "ui/navigator.h"
#include "ui/screen.h"
#include "ui/widgets.h"

#include <stdio.h>
#include <string.h>

using namespace zaro;

namespace {

int g_failures = 0;
constexpr float kDt = 1.0f / 60.0f;

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct Fixture {
    ui::Context* context = ui::create_context();
    ui::Navigator* nav = ui::create_navigator(context);
    DiagList* diags = new DiagList();

    Fixture() {
        ui::add_root(nav, "tests/data/screens/nav/root.toml", diags);
        static const char* const kScreens[] = {"popup", "forced", "toast", "reward"};
        for (const char* name : kScreens) {
            char path[128];
            snprintf(path, sizeof(path), "tests/data/screens/nav/%s.toml", name);
            ui::add_catalog(nav, path, diags);
        }
        if (diags->count) diag_print_text(diags, stderr);
        step();
    }
    ~Fixture() {
        ui::destroy_navigator(nav);
        ui::destroy_context(context);
        delete diags;
    }
    void step(const ui::UiInput& in = ui::UiInput{}) { ui::update(nav, in, 800.0f, 600.0f, kDt); }
    void steps(int n) {
        for (int i = 0; i < n; ++i) step();
    }
    bool open(const char* name, const ui::OpenOptions& o = ui::OpenOptions{}) { return ui::open(nav, name, o, diags); }
    const char* state(const char* name) const {
        const int i = ui::find_entry(nav, name);
        return i < 0 ? "closed" : ui::entry_state_name(ui::entry_state(nav, (uint32_t)i));
    }
    bool has_event(ui::NavEventKind kind, const char* screen) const {
        for (uint32_t i = 0; i < ui::event_count(nav); ++i) {
            const ui::NavEvent* e = ui::event_at(nav, i);
            if (e->kind == kind && strcmp(e->screen, screen) == 0) return true;
        }
        return false;
    }
    uint32_t count(const char* name) const {
        uint32_t n = 0;
        for (uint32_t i = 0; i < ui::entry_count(nav); ++i) n += strcmp(ui::entry_document(nav, i)->name, name) == 0;
        return n;
    }
    // Presses and releases at a point over two frames; true if a button of `screen` clicked.
    bool tap(float x, float y, const char* screen) {
        ui::UiInput in;
        in.pointer_x = x;
        in.pointer_y = y;
        in.pointer_down = in.pointer_pressed = true;
        step(in);
        in.pointer_down = in.pointer_pressed = false;
        in.pointer_released = true;
        step(in);
        const int i = ui::find_entry(nav, screen);
        const ui::Screen* s = i < 0 ? nullptr : ui::entry_screen(nav, (uint32_t)i);
        return s && ui::event_count(s) == 1;
    }
};

void test_lifecycle() {
    Fixture f;
    expect(f.diags->count == 0 && ui::in_catalog(f.nav, "popup"), "fixtures load");
    expect(f.open("popup") && strcmp(f.state("popup"), "entering") == 0, "an opened screen starts entering");
    expect(f.has_event(ui::NavEventKind::kCreated, "popup"), "opening a screen reports it created");
    f.steps(7);
    expect(strcmp(f.state("popup"), "open") == 0 && f.has_event(ui::NavEventKind::kOpened, "popup"),
           "after its 0.1 s entrance the screen is open");
    ui::clear_events(f.nav);
    expect(ui::back(f.nav, false) == ui::BackResult::kClosing && strcmp(f.state("popup"), "closing") == 0,
           "back closes an open dismissible screen");
    expect(ui::back(f.nav, false) == ui::BackResult::kBackAtRoot, "a closing screen does not take back again");
    f.steps(7);
    expect(strcmp(f.state("popup"), "closed") == 0 && f.has_event(ui::NavEventKind::kClosed, "popup"),
           "the screen is gone after its exit");
    expect(f.has_event(ui::NavEventKind::kBackAtRoot, ""), "back with nothing open reports back at root");
    expect(ui::entry_count(f.nav) == 1, "the root stays");
    DiagList* d = f.diags;
    expect(!f.open("popop") && d->count && strstr(d->items[d->count - 1].message, "did you mean 'popup'"),
           "opening an unknown screen suggests the closest name");
}

void test_back_during_entrance() {
    Fixture f;
    f.open("popup");
    f.step();
    expect(ui::back(f.nav, false) == ui::BackResult::kSnapped && strcmp(f.state("popup"), "open") == 0,
           "back during a skippable entrance jumps to its end");
    f.open("reward");
    f.step();
    expect(ui::back(f.nav, false) == ui::BackResult::kDropped && strcmp(f.state("reward"), "entering") == 0,
           "back during an unskippable entrance is dropped");
    f.steps(6);
    expect(strcmp(f.state("reward"), "open") == 0, "a dropped press is not queued: the screen opens normally");
    ui::OpenOptions strict;
    strict.unskippable = true;
    f.open("popup", strict);
    f.step();
    expect(ui::back(f.nav, false) == ui::BackResult::kDropped, "the open call can make an entrance unskippable");
}

void test_waiting_and_forced() {
    Fixture f;
    ui::OpenOptions wait;
    wait.wait = true;
    f.open("popup", wait);
    f.steps(10);
    expect(strcmp(f.state("popup"), "opening") == 0, "a waiting screen stays opening until ready");
    expect(ui::back(f.nav, false) == ui::BackResult::kCancelled && strcmp(f.state("popup"), "closed") == 0 &&
               f.has_event(ui::NavEventKind::kCancelled, "popup"),
           "back cancels a dismissible screen that is still opening");
    f.open("popup", wait);
    expect(ui::ready(f.nav, "popup") && strcmp(f.state("popup"), "entering") == 0, "ready starts the entrance");

    f.open("forced");
    expect(strcmp(f.state("forced"), "open") == 0, "a screen without a transition opens at once");
    expect(ui::back(f.nav, false) == ui::BackResult::kConsumed && strcmp(f.state("forced"), "open") == 0,
           "a forced screen keeps the back press");
    ui::OpenOptions force_wait = wait;
    force_wait.force = true;
    f.open("toast", force_wait);
    expect(ui::back(f.nav, false) == ui::BackResult::kDropped, "back cannot cancel a forced screen that is opening");
    ui::abort_all(f.nav);
    expect(ui::entry_count(f.nav) == 1, "abort_all removes everything but the root, forced screens too");
}

void test_home_close_claim_and_keys() {
    Fixture f;
    f.open("popup");
    f.open("forced");
    f.open("toast");
    f.steps(10);
    ui::home(f.nav);
    f.steps(10);
    expect(strcmp(f.state("toast"), "closed") == 0 && strcmp(f.state("forced"), "open") == 0 &&
               strcmp(f.state("popup"), "open") == 0,
           "home closes down to the root and stops at a forced screen");
    expect(ui::close(f.nav, "forced") && strcmp(f.state("forced"), "closed") == 0,
           "close removes a named screen, forced or not");
    expect(ui::back(f.nav, true) == ui::BackResult::kClaimed && strcmp(f.state("popup"), "open") == 0,
           "a claimed back press closes nothing");
    ui::abort_all(f.nav);

    ui::OpenOptions key;
    key.key = "sword";
    f.open("popup", key);
    f.open("toast");
    f.open("popup", key);
    expect(f.count("popup") == 1 && ui::find_entry(f.nav, "popup") == (int)ui::entry_count(f.nav) - 1,
           "opening with the key of an open instance brings it to the top");
    f.open("popup");
    expect(f.count("popup") == 2, "without a key every open is a new instance");
    f.steps(10);
    ui::close(f.nav, "popup");
    f.open("popup");
    expect(f.count("popup") == 3, "a screen reopened while closing is a new instance beside the old one");
}

void test_input_between_screens() {
    Fixture f;
    expect(f.tap(100.0f, 25.0f, "root"), "the root takes input when nothing is above it");
    f.open("toast");
    f.steps(10);
    expect(f.tap(100.0f, 25.0f, "root"), "a screen without a modal leaves the root its input");
    f.open("popup");
    expect(!f.tap(100.0f, 25.0f, "root"), "an entering modal screen blocks the root");
    expect(!f.tap(400.0f, 300.0f, "popup"), "an entering screen takes no clicks itself");
    f.steps(10);
    expect(f.tap(400.0f, 300.0f, "popup"), "an open screen takes clicks");
    expect(ui::focus_owner(f.nav) == ui::find_entry(f.nav, "popup"), "the open top screen owns back");
    ui::back(f.nav, false);
    expect(!f.tap(400.0f, 300.0f, "popup"), "a closing screen takes no clicks");
    f.steps(10);
    expect(f.tap(100.0f, 25.0f, "root"), "the root has its input back once the modal is gone");
}

}  // namespace

int main() {
    ui::register_ui_components();
    test_lifecycle();
    test_back_during_entrance();
    test_waiting_and_forced();
    test_home_close_claim_and_keys();
    test_input_between_screens();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    puts("ui navigation: all checks passed");
    return 0;
}
