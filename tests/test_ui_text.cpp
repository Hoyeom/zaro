// Text without a GPU: UTF-8 decoding, Korean and Latin glyphs in the bundled font, line breaking,
// and the glyph atlas (dirty regions, reuse, reset when full). argv[1] is the fonts directory.
#include "core/diag.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/text.h"

#include <stdio.h>
#include <string.h>

namespace {

int g_failures = 0;
char g_regular[512];

void expect(bool ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void test_utf8() {
    const char* p = "\xea\xb0\x80" "A\xed\x9e\xa3";  // 가 A 힣
    expect(zaro::ui::next_codepoint(&p) == 0xAC00, "decodes U+AC00");
    expect(zaro::ui::next_codepoint(&p) == 'A', "decodes ASCII");
    expect(zaro::ui::next_codepoint(&p) == 0xD7A3, "decodes U+D7A3");
    expect(zaro::ui::next_codepoint(&p) == 0, "stops at the end");
    const char* bad = "\xff" "a\xea\xb0" "b\xc0\x80";
    expect(zaro::ui::next_codepoint(&bad) == 0xFFFD, "invalid lead byte is U+FFFD");
    expect(zaro::ui::next_codepoint(&bad) == 'a', "decoding resumes after an invalid byte");
    expect(zaro::ui::next_codepoint(&bad) == 0xFFFD, "truncated sequence is U+FFFD");
    expect(zaro::ui::next_codepoint(&bad) == 'b', "the byte that ended a truncated sequence is kept");
    expect(zaro::ui::next_codepoint(&bad) == 0xFFFD, "overlong encoding is U+FFFD");
}

void test_font_coverage() {
    zaro::ui::FontFace* face = zaro::ui::font_load(g_regular);
    expect(face != nullptr, "bundled font loads");
    if (!face) return;
    const uint32_t samples[] = {'A', 'z', '0', '!', 0xAC00, 0xB098, 0xD7A3, 0x3131, 0x314F, 0x00E9};
    for (uint32_t cp : samples) {
        char what[64];
        snprintf(what, sizeof(what), "bundled font has U+%04X", cp);
        expect(zaro::ui::font_glyph(face, cp) != 0, what);
    }
    zaro::ui::font_free(face);
}

void test_load_errors(zaro::ui::TextSystem* t) {
    zaro::DiagList* diags = new zaro::DiagList();
    expect(zaro::ui::load_font(t, "no/such/font.otf", diags) == -1, "missing font returns -1");
    expect(diags->count == 1 && diags->items[0].id == zaro::kErrFontLoad, "missing font is ZE0700");
    expect(zaro::ui::load_font(t, g_regular, diags) == 0, "first font gets id 0");
    expect(zaro::ui::load_font(t, g_regular, diags) == 0, "loading the same path again reuses the id");
    expect(zaro::ui::font_count(t) == 1, "one font loaded");
    delete diags;
}

void test_measure(zaro::ui::TextSystem* t) {
    zaro::ui::TextStyle st;
    st.size = 32.0f;
    const zaro::ui::TextSize hello = zaro::ui::measure_text(t, "Hello", st);
    const zaro::ui::TextSize korean = zaro::ui::measure_text(t, "\xec\x95\x88\xeb\x85\x95", st);  // 안녕
    expect(hello.lines == 1 && hello.width > 40.0f && hello.width < 120.0f, "'Hello' is one line of plausible width");
    expect(korean.lines == 1 && korean.width > 50.0f && korean.width < 70.0f, "two Hangul syllables are ~2 em wide");
    expect(hello.height == zaro::ui::line_height(t, st), "one line is one line height high");
    expect(zaro::ui::measure_text(t, "", st).lines == 0, "empty text has no lines");
    expect(zaro::ui::measure_text(t, "a\nb", st).lines == 2, "newline starts a line");
    expect(zaro::ui::measure_text(t, "a\n", st).lines == 2, "trailing newline adds an empty line");
    const zaro::ui::TextSize spaced = zaro::ui::measure_text(t, "Hello   ", st);
    expect(spaced.width == hello.width, "trailing spaces do not count toward the width");
    const float single = zaro::ui::line_height(t, st);
    st.line_spacing = 1.5f;
    expect(zaro::ui::measure_text(t, "a\nb", st).height == 3.0f * single, "line spacing scales the line height");
}

void test_wrap(zaro::ui::TextSystem* t) {
    zaro::ui::TextStyle st;
    st.size = 20.0f;
    // 가나다 라마바 사아자: three words of three syllables.
    const char* words = "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4 \xeb\x9d\xbc\xeb\xa7\x88\xeb\xb0\x94 "
                        "\xec\x82\xac\xec\x95\x84\xec\x9e\x90";
    const float two_words = zaro::ui::measure_text(t, "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4 \xeb\x9d\xbc\xeb\xa7\x88\xeb\xb0\x94", st).width;
    st.max_width = two_words + 2.0f;
    zaro::ui::TextSize s = zaro::ui::measure_text(t, words, st);
    expect(s.lines == 2, "Korean wraps at the space after the second word");
    expect(s.width <= st.max_width, "wrapped lines fit the width");
    // A word longer than the width breaks between syllables.
    st.max_width = 50.0f;
    s = zaro::ui::measure_text(t, "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4\xeb\x9d\xbc\xeb\xa7\x88\xeb\xb0\x94", st);
    expect(s.lines == 3 && s.width <= 50.0f, "a long word breaks between syllables (2 per line at 20 px in 50 px)");
    // Never loops forever, even when one glyph is wider than the box.
    st.max_width = 1.0f;
    s = zaro::ui::measure_text(t, "ABC", st);
    expect(s.lines == 3, "one glyph per line when the box is narrower than a glyph");
    st.max_width = 0.0f;
    expect(zaro::ui::measure_text(t, words, st).lines == 1, "no wrapping without max_width");
}

void test_draw_and_atlas(zaro::ui::TextSystem* t) {
    zaro::ui::DrawList list;
    zaro::ui::reset_draw_list(&list, 800.0f, 600.0f);
    zaro::ui::take_atlas_update(t);
    zaro::ui::TextStyle st;
    st.size = 24.0f;
    zaro::ui::begin_frame(t);
    // 안녕 Hi: four visible glyphs, the space draws nothing.
    zaro::ui::draw_text(t, &list, 10.0f, 10.0f, "\xec\x95\x88\xeb\x85\x95 Hi", st);
    expect(list.count == 4, "one quad per visible glyph");
    bool inside = true;
    for (uint32_t i = 0; i < list.count; ++i) {
        const zaro::ui::Quad& q = list.quads[i];
        inside = inside && q.kind == zaro::ui::kQuadGlyph && q.rect[2] > q.rect[0] && q.uv[2] > q.uv[0] &&
                 q.rect[1] >= 10.0f && q.rect[3] <= 10.0f + zaro::ui::line_height(t, st);
    }
    expect(inside, "glyph quads lie inside the line box and have atlas coordinates");
    zaro::ui::AtlasUpdate u = zaro::ui::take_atlas_update(t);
    expect(u.x1 > u.x0 && u.y1 > u.y0, "new glyphs mark the atlas dirty");
    uint32_t ink = 0;
    for (uint32_t y = u.y0; y < u.y1; ++y) {
        for (uint32_t x = u.x0; x < u.x1; ++x) ink += u.pixels[y * u.size + x] > 128 ? 1 : 0;
    }
    expect(ink > 50, "rasterized glyphs have coverage");
    const uint32_t glyphs = zaro::ui::atlas_stats(t).glyphs;
    zaro::ui::reset_draw_list(&list, 800.0f, 600.0f);
    zaro::ui::draw_text(t, &list, 10.0f, 10.0f, "\xec\x95\x88\xeb\x85\x95 Hi", st);
    u = zaro::ui::take_atlas_update(t);
    expect(u.x1 <= u.x0 && zaro::ui::atlas_stats(t).glyphs == glyphs, "drawing the same text again reuses the glyphs");
    zaro::ui::free_draw_list(&list);
}

void test_atlas_reset() {
    zaro::DiagList* diags = new zaro::DiagList();
    zaro::ui::TextSystem* t = zaro::ui::create_text_system(128);
    zaro::ui::load_font(t, g_regular, diags);
    zaro::ui::DrawList list;
    zaro::ui::reset_draw_list(&list, 800.0f, 600.0f);
    zaro::ui::TextStyle st;
    st.size = 48.0f;
    // Eight 48 px syllables cannot all fit into 128 x 128.
    const char* text = "\xea\xb0\x80\xeb\x82\x98\xeb\x8b\xa4\xeb\x9d\xbc\xeb\xa7\x88\xeb\xb0\x94\xec\x82\xac\xec\x95\x84";
    zaro::ui::begin_frame(t);
    zaro::ui::draw_text(t, &list, 0.0f, 0.0f, text, st);
    expect(list.count > 0 && list.count < 8, "glyphs that do not fit are skipped for this frame");
    zaro::ui::begin_frame(t);
    expect(zaro::ui::atlas_stats(t).resets == 1 && zaro::ui::atlas_stats(t).glyphs == 0,
           "begin_frame clears a full atlas");
    const zaro::ui::AtlasUpdate u = zaro::ui::take_atlas_update(t);
    expect(u.x0 == 0 && u.y0 == 0 && u.x1 == 128 && u.y1 == 128, "a reset uploads the whole atlas");
    zaro::ui::reset_draw_list(&list, 800.0f, 600.0f);
    zaro::ui::draw_text(t, &list, 0.0f, 0.0f, "\xea\xb0\x80\xeb\x82\x98", st);
    expect(list.count == 2, "after the reset the next frame's glyphs fit");
    zaro::ui::free_draw_list(&list);
    zaro::ui::destroy_text_system(t);
    delete diags;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: test_ui_text <fonts dir with trailing slash>\n");
        return 2;
    }
    snprintf(g_regular, sizeof(g_regular), "%sNotoSansKR-Regular.otf", argv[1]);
    test_utf8();
    test_font_coverage();
    zaro::ui::TextSystem* t = zaro::ui::create_text_system(512);
    test_load_errors(t);
    test_measure(t);
    test_wrap(t);
    test_draw_and_atlas(t);
    zaro::ui::destroy_text_system(t);
    test_atlas_reset();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    puts("ui text: all checks passed");
    return 0;
}
