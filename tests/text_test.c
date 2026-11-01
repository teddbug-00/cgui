#include "text.h"

#include <stdio.h>
#include <string.h>

static size_t count_ink(const CguiTextBitmap *bitmap)
{
    size_t count = 0;
    size_t pixels = (size_t)bitmap->width * bitmap->height;
    for (size_t i = 0; i < pixels; ++i) {
        count += bitmap->alpha[i] != 0;
    }
    return count;
}

static int has_antialiased_edge(const CguiTextBitmap *bitmap)
{
    size_t pixels = (size_t)bitmap->width * bitmap->height;
    for (size_t i = 0; i < pixels; ++i) {
        if (bitmap->alpha[i] > 0 && bitmap->alpha[i] < 255) {
            return 1;
        }
    }
    return 0;
}

static int render_text(CguiTextEngine *engine, const char *text,
                       uint32_t size, CguiTextBitmap *bitmap)
{
    if (!cgui_text_render(engine, text, size, bitmap)) {
        fprintf(stderr, "failed to render test text\n");
        return 1;
    }
    if (bitmap->width == 0 || bitmap->height == 0 || !bitmap->alpha) {
        fprintf(stderr, "text renderer returned an empty bitmap\n");
        cgui_text_bitmap_destroy(bitmap);
        return 1;
    }
    return 0;
}

static int render_text_styled(CguiTextEngine *engine, const char *text,
                              uint32_t size, uint32_t style, CguiTextBitmap *bitmap)
{
    if (!cgui_text_render_styled(engine, text, size, style, bitmap)) {
        fprintf(stderr, "failed to render styled test text\n");
        return 1;
    }
    if (bitmap->width == 0 || bitmap->height == 0 || !bitmap->alpha) {
        fprintf(stderr, "styled text renderer returned an empty bitmap\n");
        cgui_text_bitmap_destroy(bitmap);
        return 1;
    }
    return 0;
}

int main(void)
{
    CguiTextEngine *engine = cgui_text_create();
    if (!engine) {
        return 1;
    }
    CguiTextEngine *monospace = cgui_text_create_family("monospace");
    if (!monospace) {
        fprintf(stderr, "failed to create monospace font family\n");
        cgui_text_destroy(engine);
        return 1;
    }

    CguiTextBitmap latin = {0};
    CguiTextBitmap utf8 = {0};
    CguiTextBitmap multiline = {0};
    CguiTextBitmap empty = {0};
    CguiTextBitmap small_text = {0};
    CguiTextBitmap large_text = {0};
    CguiTextBitmap bold_text = {0};
    CguiTextBitmap italic_text = {0};
    CguiTextBitmap bold_italic_text = {0};
    int failed = 0;

    failed |= render_text(engine, "Hello, macOS", 16, &latin);
    failed |= render_text(engine, "café — 世界", 18, &utf8);
    failed |= render_text(engine, "First line\nSecond line", 16, &multiline);
    failed |= render_text(engine, "", 16, &empty);

    /* Test small font size (e.g. 10px) with stem dilation */
    failed |= render_text(engine, "macOS Crisp 10px", 10, &small_text);

    /* Test large font size (e.g. 128px) with accents and descenders */
    failed |= render_text(engine, "Ångström, gjpqy 128px", 128, &large_text);

    /* Test bold, italic, and bold-italic rendering */
    failed |= render_text_styled(engine, "Typography", 16, CGUI_TEXT_BOLD, &bold_text);
    failed |= render_text_styled(engine, "Typography", 16, CGUI_TEXT_ITALIC, &italic_text);
    failed |= render_text_styled(engine, "Typography", 16, CGUI_TEXT_BOLD_ITALIC, &bold_italic_text);

    CguiTextBitmap regular_typo = {0};
    failed |= render_text_styled(engine, "Typography", 16, CGUI_TEXT_REGULAR, &regular_typo);
    CguiTextBitmap mono_text = {0};
    failed |= render_text(monospace, "fixed width", 18, &mono_text);

    if (!failed && (count_ink(&latin) == 0 || count_ink(&utf8) == 0 ||
                    !has_antialiased_edge(&latin))) {
        fprintf(stderr, "UTF-8 text did not produce antialiased glyph coverage\n");
        failed = 1;
    }
    if (!failed && multiline.height < latin.height * 2u) {
        fprintf(stderr, "multiline text did not allocate two line boxes\n");
        failed = 1;
    }
    if (!failed && count_ink(&empty) != 0) {
        fprintf(stderr, "empty text unexpectedly contains glyph pixels\n");
        failed = 1;
    }
    if (!failed && count_ink(&small_text) == 0) {
        fprintf(stderr, "small 10px text produced no ink\n");
        failed = 1;
    }
    if (!failed && (count_ink(&large_text) == 0 || large_text.height < 128)) {
        fprintf(stderr, "large 128px text produced invalid ink or dimensions\n");
        failed = 1;
    }
    if (!failed && count_ink(&bold_text) <= count_ink(&regular_typo)) {
        fprintf(stderr, "bold text does not have heavier ink than regular text\n");
        failed = 1;
    }
    if (!failed && count_ink(&italic_text) == 0) {
        fprintf(stderr, "italic text produced no ink\n");
        failed = 1;
    }
    if (!failed && count_ink(&bold_italic_text) == 0) {
        fprintf(stderr, "bold-italic text produced no ink\n");
        failed = 1;
    }
    if (!failed && count_ink(&mono_text) == 0) {
        fprintf(stderr, "monospace font family produced no ink\n");
        failed = 1;
    }

    CguiTextBitmap invalid = {0};
    if (cgui_text_render(engine, "invalid size", 0, &invalid)) {
        fprintf(stderr, "zero-sized text was accepted\n");
        cgui_text_bitmap_destroy(&invalid);
        failed = 1;
    }

    cgui_text_bitmap_destroy(&latin);
    cgui_text_bitmap_destroy(&utf8);
    cgui_text_bitmap_destroy(&multiline);
    cgui_text_bitmap_destroy(&empty);
    cgui_text_bitmap_destroy(&small_text);
    cgui_text_bitmap_destroy(&large_text);
    cgui_text_bitmap_destroy(&bold_text);
    cgui_text_bitmap_destroy(&italic_text);
    cgui_text_bitmap_destroy(&bold_italic_text);
    cgui_text_bitmap_destroy(&regular_typo);
    cgui_text_bitmap_destroy(&mono_text);
    cgui_text_destroy(monospace);
    cgui_text_destroy(engine);
    return failed ? 1 : 0;
}
