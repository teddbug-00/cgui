/**
 * text.c – HarfBuzz shaping and FreeType grayscale rasterization.
 */

#include "text.h"

#include <hb-ft.h>
#include <hb.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#if defined(CGUI_HAVE_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGUI_TEXT_MAX_PIXELS 4096u
#define CGUI_TEXT_HORIZONTAL_PADDING 2

struct CguiTextEngine {
    FT_Library library;
    FT_Face face;
};

typedef struct TextLine {
    const char *text;
    int length;
    int min_advance;
    int max_advance;
} TextLine;

typedef struct GlyphPlacement {
    hb_codepoint_t glyph;
    hb_position_t x;
    hb_position_t y;
} GlyphPlacement;

static bool open_font(CguiTextEngine *engine, const char *path)
{
    if (FT_New_Face(engine->library, path, 0, &engine->face) == 0) {
        return true;
    }
    return false;
}

static bool open_system_font(CguiTextEngine *engine)
{
#ifdef __APPLE__
    static const char *const candidates[] = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSRounded.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        NULL,
    };
    for (size_t i = 0; candidates[i]; ++i) {
        if (open_font(engine, candidates[i])) {
            return true;
        }
    }
#elif defined(_WIN32)
    static const char *const candidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        NULL,
    };
    for (size_t i = 0; candidates[i]; ++i) {
        if (open_font(engine, candidates[i])) {
            return true;
        }
    }
#elif defined(CGUI_HAVE_FONTCONFIG)
    if (FcInit()) {
        FcPattern *pattern = FcPatternCreate();
        if (pattern) {
            FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)"Inter");
            FcConfigSubstitute(NULL, pattern, FcMatchPattern);
            FcDefaultSubstitute(pattern);

            FcResult result;
            FcPattern *match = FcFontMatch(NULL, pattern, &result);
            FcPatternDestroy(pattern);
            if (match) {
                FcChar8 *font_path = NULL;
                bool found = FcPatternGetString(match, FC_FILE, 0, &font_path) == FcResultMatch &&
                             font_path && open_font(engine, (const char *)font_path);
                FcPatternDestroy(match);
                if (found) {
                    return true;
                }
            }
        }
    }

    static const char *const candidates[] = {
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL,
    };
    for (size_t i = 0; candidates[i]; ++i) {
        if (open_font(engine, candidates[i])) {
            return true;
        }
    }
#endif
    return false;
}

CguiTextEngine *cgui_text_create(void)
{
    CguiTextEngine *engine = calloc(1, sizeof(*engine));
    if (!engine) {
        fprintf(stderr, "cgui text: out of memory creating text engine\n");
        return NULL;
    }

    if (FT_Init_FreeType(&engine->library) != 0) {
        fprintf(stderr, "cgui text: FreeType initialization failed\n");
        free(engine);
        return NULL;
    }

    const char *configured_path = getenv("CGUI_FONT");
    bool font_opened = configured_path && configured_path[0]
        ? open_font(engine, configured_path)
        : open_system_font(engine);
    if (!font_opened) {
        fprintf(stderr,
                "cgui text: could not load a system sans-serif font; "
                "set CGUI_FONT to a readable .ttf or .otf file\n");
        FT_Done_FreeType(engine->library);
        free(engine);
        return NULL;
    }

    return engine;
}

void cgui_text_destroy(CguiTextEngine *engine)
{
    if (!engine) {
        return;
    }
    if (engine->face) {
        FT_Done_Face(engine->face);
    }
    if (engine->library) {
        FT_Done_FreeType(engine->library);
    }
    free(engine);
}

static bool shape_line(hb_font_t *font, const char *text, int length,
                       hb_buffer_t **buffer_out, GlyphPlacement **placements_out,
                       unsigned int *count_out, int *min_advance, int *max_advance)
{
    hb_buffer_t *buffer = hb_buffer_create();
    if (!buffer || !hb_buffer_allocation_successful(buffer)) {
        if (buffer) {
            hb_buffer_destroy(buffer);
        }
        return false;
    }

    hb_buffer_add_utf8(buffer, text, length, 0, length);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);

    unsigned int count = 0;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, NULL);
    GlyphPlacement *placements = count ? calloc(count, sizeof(*placements)) : NULL;
    if (count && !placements) {
        hb_buffer_destroy(buffer);
        return false;
    }

    int64_t pen_x = 0;
    int64_t pen_y = 0;
    int64_t min_x = 0;
    int64_t max_x = 0;
    for (unsigned int i = 0; i < count; ++i) {
        placements[i] = (GlyphPlacement) {
            .glyph = infos[i].codepoint,
            .x = (hb_position_t)pen_x + positions[i].x_offset,
            .y = (hb_position_t)pen_y + positions[i].y_offset,
        };
        pen_x += positions[i].x_advance;
        pen_y += positions[i].y_advance;
        if (pen_x < min_x) min_x = pen_x;
        if (pen_x > max_x) max_x = pen_x;
    }

    *min_advance = (int)floor((double)min_x / 64.0);
    *max_advance = (int)ceil((double)max_x / 64.0);
    *buffer_out = buffer;
    *placements_out = placements;
    *count_out = count;
    return true;
}

static uint8_t bitmap_coverage(const FT_Bitmap *bitmap, unsigned int x, unsigned int y)
{
    int pitch = bitmap->pitch;
    const uint8_t *row = pitch >= 0
        ? bitmap->buffer + (size_t)y * (size_t)pitch
        : bitmap->buffer + (size_t)(bitmap->rows - 1u - y) * (size_t)(-pitch);

    switch (bitmap->pixel_mode) {
    case FT_PIXEL_MODE_GRAY:
        if (bitmap->num_grays > 1 && bitmap->num_grays != 256) {
            return (uint8_t)((unsigned int)row[x] * 255u / (bitmap->num_grays - 1u));
        }
        return row[x];
    case FT_PIXEL_MODE_MONO:
        return (row[x >> 3] & (0x80u >> (x & 7u))) ? 255u : 0u;
    case FT_PIXEL_MODE_BGRA:
        return row[x * 4u + 3u];
    default:
        return 0;
    }
}

static void rasterize_line(CguiTextEngine *engine, hb_font_t *font,
                           const TextLine *line, unsigned int line_index,
                           int line_min_advance, int ascender, int line_height,
                           unsigned int output_width, uint8_t *output)
{
    hb_buffer_t *buffer = NULL;
    GlyphPlacement *placements = NULL;
    unsigned int count = 0;
    int ignored_min, ignored_max;
    if (!shape_line(font, line->text, line->length, &buffer, &placements,
                    &count, &ignored_min, &ignored_max)) {
        return;
    }

    for (unsigned int i = 0; i < count; ++i) {
        if (FT_Load_Glyph(engine->face, placements[i].glyph, FT_LOAD_DEFAULT) != 0 ||
            FT_Render_Glyph(engine->face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }

        const FT_GlyphSlot glyph = engine->face->glyph;
        const FT_Bitmap *bitmap = &glyph->bitmap;
        if (!bitmap->buffer || bitmap->width == 0 || bitmap->rows == 0) {
            continue;
        }

        int pen_x = (int)floor((double)placements[i].x / 64.0);
        int pen_y = (int)floor((double)placements[i].y / 64.0);
        int dst_x = CGUI_TEXT_HORIZONTAL_PADDING + pen_x - line_min_advance + glyph->bitmap_left;
        int line_top = (int)line_index * line_height;
        int dst_y = line_top + ascender - pen_y - glyph->bitmap_top;

        for (unsigned int gy = 0; gy < bitmap->rows; ++gy) {
            int y = dst_y + (int)gy;
            if (y < line_top || y >= line_top + line_height) {
                continue;
            }
            for (unsigned int gx = 0; gx < bitmap->width; ++gx) {
                int x = dst_x + (int)gx;
                if (x < 0 || x >= (int)output_width) {
                    continue;
                }
                size_t index = (size_t)y * output_width + (size_t)x;
                uint8_t coverage = bitmap_coverage(bitmap, gx, gy);
                if (coverage > output[index]) {
                    output[index] = coverage;
                }
            }
        }
    }

    free(placements);
    hb_buffer_destroy(buffer);
}

bool cgui_text_render(CguiTextEngine *engine, const char *utf8,
                     uint32_t pixel_size, CguiTextBitmap *out)
{
    if (!engine || !utf8 || !out || pixel_size == 0 || pixel_size > 512u) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    size_t text_length = strlen(utf8);
    if (text_length > 16384u || text_length > (size_t)INT32_MAX) {
        fprintf(stderr, "cgui text: text is too long to render\n");
        return false;
    }
    if (FT_Set_Pixel_Sizes(engine->face, 0, pixel_size) != 0) {
        fprintf(stderr, "cgui text: font does not support %u px\n", pixel_size);
        return false;
    }

    hb_font_t *font = hb_ft_font_create_referenced(engine->face);
    if (!font) {
        fprintf(stderr, "cgui text: HarfBuzz font creation failed\n");
        return false;
    }

    size_t line_count = 1;
    for (size_t i = 0; i < text_length; ++i) {
        if (utf8[i] == '\n') {
            ++line_count;
        }
    }
    if (line_count > CGUI_TEXT_MAX_PIXELS) {
        hb_font_destroy(font);
        return false;
    }

    TextLine *lines = calloc(line_count, sizeof(*lines));
    if (!lines) {
        hb_font_destroy(font);
        return false;
    }

    size_t line_index = 0;
    const char *start = utf8;
    const char *end = utf8;
    int max_width = 0;
    bool ok = true;
    for (;;) {
        if (*end == '\n' || *end == '\0') {
            size_t length = (size_t)(end - start);
            if (length > (size_t)INT32_MAX) {
                ok = false;
                break;
            }
            lines[line_index].text = start;
            lines[line_index].length = (int)length;

            hb_buffer_t *buffer = NULL;
            GlyphPlacement *placements = NULL;
            unsigned int count = 0;
            if (!shape_line(font, start, (int)length, &buffer, &placements,
                            &count, &lines[line_index].min_advance,
                            &lines[line_index].max_advance)) {
                ok = false;
                break;
            }
            free(placements);
            hb_buffer_destroy(buffer);
            int line_width = lines[line_index].max_advance - lines[line_index].min_advance;
            if (line_width > max_width) {
                max_width = line_width;
            }
            ++line_index;

            if (*end == '\0') {
                break;
            }
            start = end + 1;
        }
        ++end;
    }

    int ascender = (int)ceil((double)engine->face->size->metrics.ascender / 64.0);
    int line_height = (int)ceil((double)engine->face->size->metrics.height / 64.0);
    if (line_height < (int)pixel_size) {
        line_height = (int)pixel_size;
    }
    uint64_t width = (uint64_t)max_width + 2u * CGUI_TEXT_HORIZONTAL_PADDING;
    uint64_t height = (uint64_t)line_count * (uint64_t)line_height;
    if (!ok || width == 0 || width > CGUI_TEXT_MAX_PIXELS ||
        height == 0 || height > CGUI_TEXT_MAX_PIXELS) {
        if (ok) {
            fprintf(stderr, "cgui text: rendered text exceeds %u x %u px limit\n",
                    CGUI_TEXT_MAX_PIXELS, CGUI_TEXT_MAX_PIXELS);
        }
        free(lines);
        hb_font_destroy(font);
        return false;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    out->alpha = calloc(pixel_count, 1);
    if (!out->alpha) {
        free(lines);
        hb_font_destroy(font);
        return false;
    }
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;

    for (size_t i = 0; i < line_count; ++i) {
        rasterize_line(engine, font, &lines[i], (unsigned int)i,
                       lines[i].min_advance, ascender, line_height,
                       out->width, out->alpha);
    }

    free(lines);
    hb_font_destroy(font);
    return true;
}

void cgui_text_bitmap_destroy(CguiTextBitmap *bitmap)
{
    if (!bitmap) {
        return;
    }
    free(bitmap->alpha);
    memset(bitmap, 0, sizeof(*bitmap));
}
