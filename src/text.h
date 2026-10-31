/**
 * text.h – CPU text shaping and antialiased glyph rasterization.
 */

#ifndef CGUI_TEXT_H
#define CGUI_TEXT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct CguiTextEngine CguiTextEngine;

typedef struct CguiTextBitmap {
    uint8_t *alpha;
    uint32_t width;
    uint32_t height;
} CguiTextBitmap;

/**
 * Create a text engine using the system sans-serif font. Set CGUI_FONT to a
 * font file path to use a specific font (for example, a licensed SF font).
 */
CguiTextEngine *cgui_text_create(void);
void cgui_text_destroy(CguiTextEngine *engine);

/** Shape UTF-8 text and rasterize it into a tightly packed 8-bit alpha mask. */
bool cgui_text_render(CguiTextEngine *engine, const char *utf8,
                     uint32_t pixel_size, CguiTextBitmap *out);
void cgui_text_bitmap_destroy(CguiTextBitmap *bitmap);

#endif /* CGUI_TEXT_H */
