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

int main(void)
{
    CguiTextEngine *engine = cgui_text_create();
    if (!engine) {
        return 1;
    }

    CguiTextBitmap latin = {0};
    CguiTextBitmap utf8 = {0};
    CguiTextBitmap multiline = {0};
    CguiTextBitmap empty = {0};
    int failed = 0;

    failed |= render_text(engine, "Hello, macOS", 16, &latin);
    failed |= render_text(engine, "café — 世界", 18, &utf8);
    failed |= render_text(engine, "First line\nSecond line", 16, &multiline);
    failed |= render_text(engine, "", 16, &empty);

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
    cgui_text_destroy(engine);
    return failed ? 1 : 0;
}
