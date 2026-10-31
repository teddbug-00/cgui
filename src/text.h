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

typedef enum CguiTextStyleFlags {
    CGUI_TEXT_REGULAR     = 0,
    CGUI_TEXT_BOLD        = 1 << 0,
    CGUI_TEXT_ITALIC      = 1 << 1,
    CGUI_TEXT_BOLD_ITALIC = (1 << 0) | (1 << 1),
} CguiTextStyleFlags;

/**
 * Create a text engine using the system sans-serif font. Set CGUI_FONT to a
 * font file path to use a specific font (for example, a licensed SF font).
 */
CguiTextEngine *cgui_text_create(void);
void cgui_text_destroy(CguiTextEngine *engine);

/** Shape UTF-8 text and rasterize it into a tightly packed 8-bit alpha mask. */
bool cgui_text_render(CguiTextEngine *engine, const char *utf8,
                     uint32_t pixel_size, CguiTextBitmap *out);

/**
 * Shape UTF-8 text and rasterize it with the specified style flags into an 8-bit alpha mask.
 * Automatically uses authentic bold/italic font faces when available in the system
 * font family, and gracefully falls back to synthetic emboldening and oblique slanting
 * when only a regular face is present.
 */
bool cgui_text_render_styled(CguiTextEngine *engine, const char *utf8,
                             uint32_t pixel_size, uint32_t style_flags,
                             CguiTextBitmap *out);
void cgui_text_bitmap_destroy(CguiTextBitmap *bitmap);

/* ------------------------------------------------------------------ */
/* Per-glyph atlas API                                                 */
/* ------------------------------------------------------------------ */

/**
 * Shaped glyph placement from HarfBuzz.
 * Positions are in pixels (already divided out of 26.6 fixed-point).
 */
typedef struct CguiShapedGlyph {
    uint32_t glyph_id;   /* FreeType glyph index (from HarfBuzz) */
    float    pen_x;      /* Screen-space pen X position (fractional px) */
    float    pen_y;      /* Screen-space pen Y position (fractional px) */
    float    advance_x;  /* Horizontal advance in pixels */
} CguiShapedGlyph;

/**
 * Shape a UTF-8 string and return HarfBuzz glyph placements.
 * The caller provides the starting pen position.
 * @param glyphs_out  Caller-allocated buffer. Pass NULL with cap=0 to get count.
 * @param cap         Capacity of glyphs_out.
 * @param count_out   Number of glyphs written.
 * @param total_w_out Total advance width in pixels (may be NULL).
 * @param ascender_out Font ascender in pixels (may be NULL).
 * @param line_h_out  Font line height in pixels (may be NULL).
 * @return false on error.
 */
bool cgui_text_shape(CguiTextEngine *engine,
                     const char *utf8, uint32_t pixel_size, uint32_t style_flags,
                     CguiShapedGlyph *glyphs_out, uint32_t cap,
                     uint32_t *count_out,
                     float *total_w_out,
                     float *ascender_out, float *line_h_out);

/**
 * Single-glyph rasterization result (R8 grayscale alpha).
 * The caller owns the data and must free it with cgui_text_bitmap_destroy().
 */
typedef struct CguiGlyphBitmap {
    uint8_t *alpha;      /* R8 grayscale, size = width * height */
    uint32_t width;
    uint32_t height;
    int32_t  bearing_x;  /* bitmap_left in pixels */
    int32_t  bearing_y;  /* bitmap_top  in pixels */
} CguiGlyphBitmap;

/**
 * Rasterize a single glyph from the font matching the given style.
 * Returns false if the glyph is a space or has no bitmap (whitespace).
 */
bool cgui_text_render_glyph(CguiTextEngine *engine,
                            uint32_t glyph_id,
                            uint32_t pixel_size, uint32_t style_flags,
                            CguiGlyphBitmap *out);

void cgui_glyph_bitmap_destroy(CguiGlyphBitmap *bm);

#endif /* CGUI_TEXT_H */
