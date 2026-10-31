/**
 * glyph_atlas.h – GPU glyph texture atlas with shelf packing.
 *
 * Architecture:
 *   - Single 2048×2048 VK_FORMAT_R8_UNORM device-local texture on the GPU.
 *   - Glyphs are packed using the shelf (strip) algorithm: rows grow downward,
 *     each row is as tall as the tallest glyph in it.
 *   - FreeType rasterizes each unique (glyph_id, pixel_size, style_flags) triple
 *     exactly ONCE into a staging buffer, which is batched-uploaded to the atlas.
 *   - Callers receive UV coordinates into the atlas for each glyph.
 *   - HarfBuzz layout (pen positions) is computed per string but never touches the
 *     atlas — glyphs are already there.
 */

#ifndef CGUI_GLYPH_ATLAS_H
#define CGUI_GLYPH_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "vk_context.h"
#include "text.h"

/* Atlas dimensions */
#define CGUI_ATLAS_DIM    2048u
#define CGUI_ATLAS_MAX_GLYPHS 8192u

/* One rasterized glyph's metadata */
typedef struct CguiAtlasGlyph {
    /* Key */
    uint32_t glyph_id;      /* FreeType/HarfBuzz glyph index */
    uint32_t pixel_size;
    uint32_t style_flags;

    /* Atlas position (texels) */
    uint16_t atlas_x;
    uint16_t atlas_y;
    uint16_t atlas_w;
    uint16_t atlas_h;

    /* Rendering offsets (in pixels, from pen position) */
    int16_t  bearing_x;    /* glyph->bitmap_left */
    int16_t  bearing_y;    /* glyph->bitmap_top  */

    /* Advance width (HarfBuzz 26.6 fixed point / 64) */
    int32_t  advance_x;
} CguiAtlasGlyph;

/* Per-string shaped glyph quad for the GPU */
typedef struct CguiGlyphQuad {
    float x, y, w, h;          /* destination rect in screen pixels */
    float u0, v0, u1, v1;      /* UV coords into atlas */
} CguiGlyphQuad;

typedef struct CguiGlyphAtlas CguiGlyphAtlas;

/**
 * Create a glyph atlas backed by a Vulkan R8_UNORM texture.
 * @param ctx   Active VkContext (device + queues must be ready).
 * @param engine Text engine (FreeType + HarfBuzz, already created).
 */
CguiGlyphAtlas *cgui_atlas_create(VkContext *ctx, CguiTextEngine *engine);
void            cgui_atlas_destroy(CguiGlyphAtlas *atlas);

/**
 * Shape a UTF-8 string and return an array of screen-space quads.
 * Glyphs are looked up or rasterized on demand.
 *
 * @param atlas       Atlas instance.
 * @param utf8        UTF-8 text.
 * @param x, y        Top-left pen position in screen pixels.
 * @param pixel_size  Font size in pixels.
 * @param style_flags CGUI_TEXT_BOLD / CGUI_TEXT_ITALIC flags.
 * @param quads_out   Caller-provided buffer (or NULL to query count).
 * @param cap         Capacity of quads_out.
 * @param count_out   Number of quads written.
 * @param total_w_out Total string width in pixels (for alignment).
 * @param total_h_out Total string height in pixels.
 * @return false on hard failure.
 */
bool cgui_atlas_shape_quads(CguiGlyphAtlas *atlas,
                            const char *utf8, float x, float y,
                            uint32_t pixel_size, uint32_t style_flags,
                            CguiGlyphQuad *quads_out, uint32_t cap,
                            uint32_t *count_out,
                            float *total_w_out, float *total_h_out);

/** Flush any pending dirty regions to the GPU atlas texture. */
bool cgui_atlas_flush(CguiGlyphAtlas *atlas);

/** Return the Vulkan image view for the atlas (bind as sampled image). */
VkImageView cgui_atlas_image_view(const CguiGlyphAtlas *atlas);

/** Return the VkImage for layout transitions. */
VkImage cgui_atlas_image(const CguiGlyphAtlas *atlas);

#endif /* CGUI_GLYPH_ATLAS_H */
