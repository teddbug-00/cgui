/**
 * text.c – HarfBuzz shaping and FreeType grayscale rasterization.
 */

#include "text.h"

#include <hb-ft.h>
#include <hb.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_MODULE_H

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
    FT_Face face_regular;
    FT_Face face_bold;
    FT_Face face_italic;
    FT_Face face_bold_italic;
    hb_font_t *hb_regular;
    hb_font_t *hb_bold;
    hb_font_t *hb_italic;
    hb_font_t *hb_bold_italic;
    bool has_bold_face;
    bool has_italic_face;
    bool has_bold_italic_face;
};

typedef struct GlyphPlacement {
    hb_codepoint_t glyph;
    hb_position_t x;
    hb_position_t y;
} GlyphPlacement;

typedef struct TextLine {
    const char *text;
    int length;
    GlyphPlacement *placements;
    unsigned int glyph_count;
    int min_advance;
    int max_advance;
} TextLine;

static uint8_t g_gamma_lut[256];
static bool g_gamma_initialized = false;

static void init_gamma_lut(void)
{
    if (g_gamma_initialized) {
        return;
    }
    for (int i = 0; i < 256; ++i) {
        double linear = (double)i / 255.0;
        /* Perceptual gamma correction (~1.35) matching display response to keep edges crisp */
        double corrected = pow(linear, 1.0 / 1.35);
        int val = (int)(corrected * 255.0 + 0.5);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        g_gamma_lut[i] = (uint8_t)val;
    }
    g_gamma_initialized = true;
}

static FT_Face load_face(FT_Library lib, const char *path, FT_Long face_index)
{
    FT_Face face = NULL;
    if (FT_New_Face(lib, path, face_index, &face) == 0) {
        return face;
    }
    return NULL;
}

#if defined(CGUI_HAVE_FONTCONFIG)
static FT_Face query_fontconfig_face(FT_Library lib, const char *family, int weight, int slant)
{
    FcPattern *pattern = FcPatternCreate();
    if (!pattern) {
        return NULL;
    }

    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(pattern, FC_WEIGHT, weight);
    FcPatternAddInteger(pattern, FC_SLANT, slant);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);
    if (!match) {
        return NULL;
    }

    FcChar8 *font_path = NULL;
    int face_index = 0;
    (void)FcPatternGetInteger(match, FC_INDEX, 0, &face_index);
    FT_Face face = NULL;
    if (FcPatternGetString(match, FC_FILE, 0, &font_path) == FcResultMatch && font_path) {
        face = load_face(lib, (const char *)font_path, (FT_Long)face_index);
    }
    FcPatternDestroy(match);
    return face;
}
#endif

static bool open_system_fonts(CguiTextEngine *engine)
{
#ifdef __APPLE__
    static const char *const candidates_reg[] = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSText.ttf",
        "/System/Library/Fonts/SFNSRounded.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        NULL,
    };
    for (size_t i = 0; candidates_reg[i]; ++i) {
        engine->face_regular = load_face(engine->library, candidates_reg[i], 0);
        if (engine->face_regular) break;
    }

    static const char *const candidates_bold[] = {
        "/System/Library/Fonts/SFNSBold.ttf",
        "/System/Library/Fonts/SFNSTextBold.ttf",
        NULL,
    };
    for (size_t i = 0; candidates_bold[i]; ++i) {
        engine->face_bold = load_face(engine->library, candidates_bold[i], 0);
        if (engine->face_bold) {
            engine->has_bold_face = true;
            break;
        }
    }

    static const char *const candidates_it[] = {
        "/System/Library/Fonts/SFNSItalic.ttf",
        "/System/Library/Fonts/SFNSTextItalic.ttf",
        NULL,
    };
    for (size_t i = 0; candidates_it[i]; ++i) {
        engine->face_italic = load_face(engine->library, candidates_it[i], 0);
        if (engine->face_italic) {
            engine->has_italic_face = true;
            break;
        }
    }
#elif defined(_WIN32)
    engine->face_regular = load_face(engine->library, "C:\\Windows\\Fonts\\segoeui.ttf", 0);
    if (!engine->face_regular) {
        engine->face_regular = load_face(engine->library, "C:\\Windows\\Fonts\\arial.ttf", 0);
    }
    engine->face_bold = load_face(engine->library, "C:\\Windows\\Fonts\\segoeuib.ttf", 0);
    if (engine->face_bold) engine->has_bold_face = true;
    engine->face_italic = load_face(engine->library, "C:\\Windows\\Fonts\\segoeuii.ttf", 0);
    if (engine->face_italic) engine->has_italic_face = true;
    engine->face_bold_italic = load_face(engine->library, "C:\\Windows\\Fonts\\segoeuiz.ttf", 0);
    if (engine->face_bold_italic) engine->has_bold_italic_face = true;
#elif defined(CGUI_HAVE_FONTCONFIG)
    if (FcInit()) {
        engine->face_regular = query_fontconfig_face(engine->library, "Inter", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
        if (!engine->face_regular) {
            engine->face_regular = query_fontconfig_face(engine->library, "sans-serif", FC_WEIGHT_REGULAR, FC_SLANT_ROMAN);
        }
        if (engine->face_regular) {
            engine->face_bold = query_fontconfig_face(engine->library, "Inter", FC_WEIGHT_BOLD, FC_SLANT_ROMAN);
            if (engine->face_bold) engine->has_bold_face = true;

            engine->face_italic = query_fontconfig_face(engine->library, "Inter", FC_WEIGHT_REGULAR, FC_SLANT_ITALIC);
            if (engine->face_italic) engine->has_italic_face = true;

            engine->face_bold_italic = query_fontconfig_face(engine->library, "Inter", FC_WEIGHT_BOLD, FC_SLANT_ITALIC);
            if (engine->face_bold_italic) engine->has_bold_italic_face = true;
        }
    }

    if (!engine->face_regular) {
        static const char *const candidates[] = {
            "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            NULL,
        };
        for (size_t i = 0; candidates[i]; ++i) {
            engine->face_regular = load_face(engine->library, candidates[i], 0);
            if (engine->face_regular) {
                break;
            }
        }
    }
#endif
    return engine->face_regular != NULL;
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

    /* Enable FreeType stem darkening for CFF, autofitter, and Type 1 engines (macOS-style) */
    FT_Bool no_darkening = 0;
    FT_Property_Set(engine->library, "cff", "no-stem-darkening", &no_darkening);
    FT_Property_Set(engine->library, "autofitter", "no-stem-darkening", &no_darkening);
    FT_Property_Set(engine->library, "type1", "no-stem-darkening", &no_darkening);

    init_gamma_lut();

    const char *configured_path = getenv("CGUI_FONT");
    bool font_opened = false;
    if (configured_path && configured_path[0]) {
        engine->face_regular = load_face(engine->library, configured_path, 0);
        font_opened = (engine->face_regular != NULL);

        const char *conf_bold = getenv("CGUI_FONT_BOLD");
        if (conf_bold && conf_bold[0]) {
            engine->face_bold = load_face(engine->library, conf_bold, 0);
            if (engine->face_bold) engine->has_bold_face = true;
        }
        const char *conf_it = getenv("CGUI_FONT_ITALIC");
        if (conf_it && conf_it[0]) {
            engine->face_italic = load_face(engine->library, conf_it, 0);
            if (engine->face_italic) engine->has_italic_face = true;
        }
    } else {
        font_opened = open_system_fonts(engine);
    }

    if (!font_opened) {
        fprintf(stderr,
                "cgui text: could not load a system sans-serif font; "
                "set CGUI_FONT to a readable .ttf or .otf file\n");
        FT_Done_FreeType(engine->library);
        free(engine);
        return NULL;
    }

    /* Cache HarfBuzz font instances to avoid re-parsing font tables per render */
    if (engine->face_regular) {
        engine->hb_regular = hb_ft_font_create_referenced(engine->face_regular);
    }
    if (engine->face_bold) {
        engine->hb_bold = hb_ft_font_create_referenced(engine->face_bold);
    }
    if (engine->face_italic) {
        engine->hb_italic = hb_ft_font_create_referenced(engine->face_italic);
    }
    if (engine->face_bold_italic) {
        engine->hb_bold_italic = hb_ft_font_create_referenced(engine->face_bold_italic);
    }

    return engine;
}

void cgui_text_destroy(CguiTextEngine *engine)
{
    if (!engine) {
        return;
    }
    if (engine->hb_bold_italic) hb_font_destroy(engine->hb_bold_italic);
    if (engine->hb_italic)      hb_font_destroy(engine->hb_italic);
    if (engine->hb_bold)        hb_font_destroy(engine->hb_bold);
    if (engine->hb_regular)     hb_font_destroy(engine->hb_regular);

    if (engine->face_bold_italic && engine->face_bold_italic != engine->face_regular &&
        engine->face_bold_italic != engine->face_bold && engine->face_bold_italic != engine->face_italic) {
        FT_Done_Face(engine->face_bold_italic);
    }
    if (engine->face_italic && engine->face_italic != engine->face_regular &&
        engine->face_italic != engine->face_bold) {
        FT_Done_Face(engine->face_italic);
    }
    if (engine->face_bold && engine->face_bold != engine->face_regular) {
        FT_Done_Face(engine->face_bold);
    }
    if (engine->face_regular) {
        FT_Done_Face(engine->face_regular);
    }
    if (engine->library) {
        FT_Done_FreeType(engine->library);
    }
    free(engine);
}

static bool shape_line(hb_font_t *font, const char *text, int length,
                       GlyphPlacement **placements_out,
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
    GlyphPlacement *placements = count ? malloc(count * sizeof(*placements)) : NULL;
    if (count && !placements) {
        hb_buffer_destroy(buffer);
        return false;
    }

    int64_t pen_x = 0;
    int64_t pen_y = 0;
    int64_t min_x = 0;
    int64_t max_x = 0;
    for (unsigned int i = 0; i < count; ++i) {
        hb_position_t gx = (hb_position_t)pen_x + positions[i].x_offset;
        hb_position_t gy = (hb_position_t)pen_y + positions[i].y_offset;
        placements[i] = (GlyphPlacement) {
            .glyph = infos[i].codepoint,
            .x = gx,
            .y = gy,
        };
        if (gx < min_x) min_x = gx;
        pen_x += positions[i].x_advance;
        pen_y += positions[i].y_advance;
        if (pen_x < min_x) min_x = pen_x;
        if (pen_x > max_x) max_x = pen_x;
    }

    *min_advance = (int)floor((double)min_x / 64.0);
    *max_advance = (int)ceil((double)max_x / 64.0);
    *placements_out = placements;
    *count_out = count;
    hb_buffer_destroy(buffer);
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

static void rasterize_line(FT_Face face,
                           const TextLine *line, unsigned int line_index,
                           int line_min_advance, int ascender, int line_height,
                           uint32_t pixel_size, bool synth_bold, bool synth_italic,
                           unsigned int output_width, unsigned int output_height,
                           uint8_t *output)
{
    /* Light hinting at small sizes for vertical grid alignment; unhinted at large sizes */
    FT_Int32 load_flags = (pixel_size <= 24u)
        ? (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
        : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

    for (unsigned int i = 0; i < line->glyph_count; ++i) {
        if (FT_Load_Glyph(face, line->placements[i].glyph, load_flags | FT_LOAD_NO_BITMAP) != 0) {
            continue;
        }

        int pen_x = (int)floor((double)line->placements[i].x / 64.0);
        int pen_y = (int)floor((double)line->placements[i].y / 64.0);

        if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
            /* 1. Synthetic italic (oblique 12° shear) when no true italic face is available */
            if (synth_italic) {
                FT_GlyphSlot_Oblique(face->glyph);
            }

            /* 2. Synthetic bold or macOS-style stem dilation */
            if (synth_bold) {
                FT_GlyphSlot_Embolden(face->glyph);
            } else {
                /* Subtle stem dilation at small sizes to prevent washed-out text */
                if (pixel_size <= 12u) {
                    FT_Outline_EmboldenXY(&face->glyph->outline, 6, 2);
                } else if (pixel_size <= 16u) {
                    FT_Outline_EmboldenXY(&face->glyph->outline, 4, 2);
                } else if (pixel_size <= 20u) {
                    FT_Outline_EmboldenXY(&face->glyph->outline, 2, 0);
                }
            }

            /* 3. Subpixel fractional translation for accurate letter spacing */
            FT_Pos subpixel_x = (FT_Pos)(line->placements[i].x - (pen_x * 64));
            if (subpixel_x != 0) {
                FT_Outline_Translate(&face->glyph->outline, subpixel_x, 0);
            }
        }

        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }

        const FT_GlyphSlot glyph = face->glyph;
        const FT_Bitmap *bitmap = &glyph->bitmap;
        if (!bitmap->buffer || bitmap->width == 0 || bitmap->rows == 0) {
            continue;
        }

        int dst_x = CGUI_TEXT_HORIZONTAL_PADDING + pen_x - line_min_advance + glyph->bitmap_left;
        int line_top = (int)line_index * line_height;
        int dst_y = line_top + ascender - pen_y - glyph->bitmap_top;

        if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
            int pitch = bitmap->pitch;
            const uint8_t *src_base = pitch >= 0
                ? bitmap->buffer
                : bitmap->buffer + (size_t)(bitmap->rows - 1u) * (size_t)(-pitch);

            for (unsigned int gy = 0; gy < bitmap->rows; ++gy) {
                int y = dst_y + (int)gy;
                if (y < 0 || y >= (int)output_height) continue;

                const uint8_t *src_row = src_base + (size_t)gy * (size_t)pitch;
                uint8_t *dst_row = output + (size_t)y * output_width;

                for (unsigned int gx = 0; gx < bitmap->width; ++gx) {
                    int x = dst_x + (int)gx;
                    if (x < 0 || x >= (int)output_width) continue;

                    uint8_t raw = src_row[gx];
                    if (raw == 0) continue;

                    uint8_t coverage = g_gamma_lut[raw];
                    uint8_t existing = dst_row[x];
                    if (existing == 0) {
                        dst_row[x] = coverage;
                    } else {
                        dst_row[x] = (uint8_t)(existing + coverage - ((existing * coverage) / 255u));
                    }
                }
            }
        } else {
            for (unsigned int gy = 0; gy < bitmap->rows; ++gy) {
                int y = dst_y + (int)gy;
                if (y < 0 || y >= (int)output_height) continue;

                for (unsigned int gx = 0; gx < bitmap->width; ++gx) {
                    int x = dst_x + (int)gx;
                    if (x < 0 || x >= (int)output_width) continue;

                    uint8_t raw = bitmap_coverage(bitmap, gx, gy);
                    if (raw == 0) continue;

                    uint8_t coverage = g_gamma_lut[raw];
                    size_t index = (size_t)y * output_width + (size_t)x;
                    uint8_t existing = output[index];
                    if (existing == 0) {
                        output[index] = coverage;
                    } else {
                        output[index] = (uint8_t)(existing + coverage - ((existing * coverage) / 255u));
                    }
                }
            }
        }
    }
}

bool cgui_text_render_styled(CguiTextEngine *engine, const char *utf8,
                             uint32_t pixel_size, uint32_t style_flags,
                             CguiTextBitmap *out)
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

    /* Choose appropriate font face and determine if synthetic bold/italic is needed */
    FT_Face face = engine->face_regular;
    bool synth_bold = false;
    bool synth_italic = false;

    bool want_bold = (style_flags & CGUI_TEXT_BOLD) != 0;
    bool want_italic = (style_flags & CGUI_TEXT_ITALIC) != 0;

    if (want_bold && want_italic) {
        if (engine->has_bold_italic_face && engine->face_bold_italic) {
            face = engine->face_bold_italic;
        } else if (engine->has_bold_face && engine->face_bold) {
            face = engine->face_bold;
            synth_italic = true;
        } else if (engine->has_italic_face && engine->face_italic) {
            face = engine->face_italic;
            synth_bold = true;
        } else {
            face = engine->face_regular;
            synth_bold = true;
            synth_italic = true;
        }
    } else if (want_bold) {
        if (engine->has_bold_face && engine->face_bold) {
            face = engine->face_bold;
        } else {
            face = engine->face_regular;
            synth_bold = true;
        }
    } else if (want_italic) {
        if (engine->has_italic_face && engine->face_italic) {
            face = engine->face_italic;
        } else {
            face = engine->face_regular;
            synth_italic = true;
        }
    }

    if (!face) {
        face = engine->face_regular;
    }
    if (!face) {
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0) {
        fprintf(stderr, "cgui text: font does not support %u px\n", pixel_size);
        return false;
    }

    FT_Int32 load_flags = (pixel_size <= 24u)
        ? (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
        : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

    /* Use cached HarfBuzz font instance */
    hb_font_t *font = engine->hb_regular;
    if (face == engine->face_bold && engine->hb_bold) {
        font = engine->hb_bold;
    } else if (face == engine->face_italic && engine->hb_italic) {
        font = engine->hb_italic;
    } else if (face == engine->face_bold_italic && engine->hb_bold_italic) {
        font = engine->hb_bold_italic;
    }
    if (!font) {
        font = engine->hb_regular;
    }
    if (font) {
        hb_ft_font_changed(font);
        hb_ft_font_set_load_flags(font, load_flags);
    }

    size_t line_count = 1;
    for (size_t i = 0; i < text_length; ++i) {
        if (utf8[i] == '\n') {
            ++line_count;
        }
    }
    if (line_count > CGUI_TEXT_MAX_PIXELS) {
        return false;
    }

    TextLine *lines = calloc(line_count, sizeof(*lines));
    if (!lines) {
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

            if (!shape_line(font, start, (int)length,
                            &lines[line_index].placements,
                            &lines[line_index].glyph_count,
                            &lines[line_index].min_advance,
                            &lines[line_index].max_advance)) {
                ok = false;
                break;
            }
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

    int ascender = (int)ceil((double)face->size->metrics.ascender / 64.0);
    int line_height = (int)ceil((double)face->size->metrics.height / 64.0);
    if (line_height < (int)pixel_size) {
        line_height = (int)pixel_size;
    }

    /* Use true font bounding box so tall accented glyphs and descenders are never clipped */
    if (face->units_per_EM > 0) {
        double em_scale = (double)face->size->metrics.y_ppem / (double)face->units_per_EM;
        int font_yMax = (int)ceil((double)face->bbox.yMax * em_scale);
        int font_yMin = (int)floor((double)face->bbox.yMin * em_scale);
        if (font_yMax > ascender) {
            ascender = font_yMax;
        }
        int font_span = font_yMax - font_yMin;
        if (font_span > line_height) {
            line_height = font_span;
        }
    }

    /* Extra padding for synthetic italic slant and synthetic bold expansion */
    int extra_pad = (synth_italic ? (int)ceil(0.25 * (double)pixel_size) : 0) +
                    (synth_bold ? (int)ceil(0.10 * (double)pixel_size) : 0);
    uint64_t width = (uint64_t)max_width + 2u * (CGUI_TEXT_HORIZONTAL_PADDING + (uint64_t)extra_pad);
    uint64_t height = (uint64_t)line_count * (uint64_t)line_height;
    if (!ok || width == 0 || width > CGUI_TEXT_MAX_PIXELS ||
        height == 0 || height > CGUI_TEXT_MAX_PIXELS) {
        if (ok) {
            fprintf(stderr, "cgui text: rendered text exceeds %u x %u px limit\n",
                    CGUI_TEXT_MAX_PIXELS, CGUI_TEXT_MAX_PIXELS);
        }
        for (size_t i = 0; i < line_index; ++i) {
            free(lines[i].placements);
        }
        free(lines);
        return false;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    out->alpha = calloc(pixel_count, 1);
    if (!out->alpha) {
        for (size_t i = 0; i < line_index; ++i) {
            free(lines[i].placements);
        }
        free(lines);
        return false;
    }
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;

    for (size_t i = 0; i < line_count; ++i) {
        rasterize_line(face, &lines[i], (unsigned int)i,
                       lines[i].min_advance, ascender, line_height,
                       pixel_size, synth_bold, synth_italic,
                       out->width, out->height, out->alpha);
    }

    for (size_t i = 0; i < line_count; ++i) {
        free(lines[i].placements);
    }
    free(lines);
    return true;
}

bool cgui_text_render(CguiTextEngine *engine, const char *utf8,
                     uint32_t pixel_size, CguiTextBitmap *out)
{
    return cgui_text_render_styled(engine, utf8, pixel_size, CGUI_TEXT_REGULAR, out);
}

void cgui_text_bitmap_destroy(CguiTextBitmap *bitmap)
{
    if (!bitmap) {
        return;
    }
    free(bitmap->alpha);
    memset(bitmap, 0, sizeof(*bitmap));
}

/* ------------------------------------------------------------------ */
/* Internal face/font selection helper                                 */
/* ------------------------------------------------------------------ */

static void select_face(CguiTextEngine *engine, uint32_t style_flags,
                        FT_Face *face_out, hb_font_t **hb_out,
                        bool *synth_bold_out, bool *synth_italic_out)
{
    FT_Face face       = engine->face_regular;
    hb_font_t *hb      = engine->hb_regular;
    bool synth_bold    = false;
    bool synth_italic  = false;

    bool want_bold   = (style_flags & CGUI_TEXT_BOLD) != 0;
    bool want_italic = (style_flags & CGUI_TEXT_ITALIC) != 0;

    if (want_bold && want_italic) {
        if (engine->has_bold_italic_face && engine->face_bold_italic) {
            face = engine->face_bold_italic; hb = engine->hb_bold_italic;
        } else if (engine->has_bold_face && engine->face_bold) {
            face = engine->face_bold; hb = engine->hb_bold; synth_italic = true;
        } else if (engine->has_italic_face && engine->face_italic) {
            face = engine->face_italic; hb = engine->hb_italic; synth_bold = true;
        } else {
            synth_bold = true; synth_italic = true;
        }
    } else if (want_bold) {
        if (engine->has_bold_face && engine->face_bold) {
            face = engine->face_bold; hb = engine->hb_bold;
        } else {
            synth_bold = true;
        }
    } else if (want_italic) {
        if (engine->has_italic_face && engine->face_italic) {
            face = engine->face_italic; hb = engine->hb_italic;
        } else {
            synth_italic = true;
        }
    }

    if (!face) face = engine->face_regular;
    if (!hb)   hb   = engine->hb_regular;

    *face_out       = face;
    *hb_out         = hb;
    *synth_bold_out  = synth_bold;
    *synth_italic_out = synth_italic;
}

/* ------------------------------------------------------------------ */
/* cgui_text_shape – HarfBuzz layout (no FreeType rasterization)      */
/* ------------------------------------------------------------------ */

bool cgui_text_shape(CguiTextEngine *engine,
                     const char *utf8, uint32_t pixel_size, uint32_t style_flags,
                     CguiShapedGlyph *glyphs_out, uint32_t cap,
                     uint32_t *count_out,
                     float *total_w_out,
                     float *ascender_out, float *line_h_out)
{
    if (!engine || !utf8 || pixel_size == 0 || pixel_size > 512u) return false;

    FT_Face    face;
    hb_font_t *hb;
    bool synth_bold, synth_italic;
    select_face(engine, style_flags, &face, &hb, &synth_bold, &synth_italic);
    if (!face) return false;

    if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0) return false;

    FT_Int32 load_flags = (pixel_size <= 24u)
        ? (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT)
        : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING);

    if (hb) {
        hb_ft_font_changed(hb);
        hb_ft_font_set_load_flags(hb, load_flags);
    } else {
        return false;
    }

    hb_buffer_t *buf = hb_buffer_create();
    if (!buf || !hb_buffer_allocation_successful(buf)) {
        if (buf) hb_buffer_destroy(buf);
        return false;
    }

    int byte_len = (int)strlen(utf8);
    hb_buffer_add_utf8(buf, utf8, byte_len, 0, byte_len);
    hb_buffer_guess_segment_properties(buf);
    hb_shape(hb, buf, NULL, 0);

    unsigned int n = 0;
    const hb_glyph_info_t     *infos = hb_buffer_get_glyph_infos(buf, &n);
    const hb_glyph_position_t *pos   = hb_buffer_get_glyph_positions(buf, NULL);

    if (count_out) *count_out = n;

    float pen_x = 0.0f;
    float pen_y = 0.0f;

    for (unsigned int i = 0; i < n; ++i) {
        float gx = pen_x + (float)pos[i].x_offset / 64.0f;
        float gy = pen_y + (float)pos[i].y_offset / 64.0f;
        float adv = (float)pos[i].x_advance / 64.0f;

        if (glyphs_out && i < cap) {
            glyphs_out[i].glyph_id  = infos[i].codepoint;
            glyphs_out[i].pen_x     = gx;
            glyphs_out[i].pen_y     = gy;
            glyphs_out[i].advance_x = adv;
        }
        pen_x += adv;
    }

    if (total_w_out) *total_w_out = pen_x;

    if (ascender_out || line_h_out) {
        float asc = ceilf((float)face->size->metrics.ascender / 64.0f);
        float lh  = ceilf((float)face->size->metrics.height   / 64.0f);
        if (lh < (float)pixel_size) lh = (float)pixel_size;
        if (ascender_out) *ascender_out = asc;
        if (line_h_out)   *line_h_out   = lh;
    }

    hb_buffer_destroy(buf);
    return true;
}

/* ------------------------------------------------------------------ */
/* cgui_text_render_glyph – single glyph FreeType rasterization       */
/* ------------------------------------------------------------------ */

bool cgui_text_render_glyph(CguiTextEngine *engine,
                            uint32_t glyph_id,
                            uint32_t pixel_size, uint32_t style_flags,
                            CguiGlyphBitmap *out)
{
    if (!engine || !out || pixel_size == 0 || pixel_size > 512u) return false;

    FT_Face    face;
    hb_font_t *hb;
    bool synth_bold, synth_italic;
    select_face(engine, style_flags, &face, &hb, &synth_bold, &synth_italic);
    if (!face) return false;

    if (FT_Set_Pixel_Sizes(face, 0, pixel_size) != 0) return false;

    FT_Int32 load_flags = (pixel_size <= 24u)
        ? (FT_LOAD_DEFAULT | FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP)
        : (FT_LOAD_DEFAULT | FT_LOAD_NO_HINTING   | FT_LOAD_NO_BITMAP);

    if (FT_Load_Glyph(face, (FT_UInt)glyph_id, load_flags) != 0) return false;

    FT_GlyphSlot gs = face->glyph;

    if (gs->format == FT_GLYPH_FORMAT_OUTLINE) {
        if (synth_italic) FT_GlyphSlot_Oblique(gs);
        if (synth_bold) {
            FT_GlyphSlot_Embolden(gs);
        } else {
            if      (pixel_size <= 12u) FT_Outline_EmboldenXY(&gs->outline, 6, 2);
            else if (pixel_size <= 16u) FT_Outline_EmboldenXY(&gs->outline, 4, 2);
            else if (pixel_size <= 20u) FT_Outline_EmboldenXY(&gs->outline, 2, 0);
        }
    }

    if (FT_Render_Glyph(gs, FT_RENDER_MODE_NORMAL) != 0) return false;

    const FT_Bitmap *bm = &gs->bitmap;
    if (!bm->buffer || bm->width == 0 || bm->rows == 0) {
        /* Whitespace glyph — valid, no bitmap */
        memset(out, 0, sizeof(*out));
        out->bearing_x = gs->bitmap_left;
        out->bearing_y = gs->bitmap_top;
        return false; /* caller skips invisible glyphs */
    }

    uint32_t w = bm->width;
    uint32_t h = bm->rows;
    uint8_t *alpha = malloc((size_t)w * h);
    if (!alpha) return false;

    /* Copy with gamma correction */
    if (bm->pixel_mode == FT_PIXEL_MODE_GRAY) {
        int pitch = bm->pitch;
        const uint8_t *src_base = (pitch >= 0)
            ? bm->buffer
            : bm->buffer + (size_t)(bm->rows - 1u) * (size_t)(-pitch);

        for (uint32_t gy = 0; gy < h; ++gy) {
            const uint8_t *src = src_base + (size_t)gy * (size_t)(pitch < 0 ? -pitch : pitch);
            uint8_t *dst = alpha + (size_t)gy * w;
            for (uint32_t gx = 0; gx < w; ++gx) {
                dst[gx] = g_gamma_lut[src[gx]];
            }
        }
    } else {
        /* Mono or other: use coverage helper */
        for (uint32_t gy = 0; gy < h; ++gy) {
            for (uint32_t gx = 0; gx < w; ++gx) {
                uint8_t raw = bitmap_coverage(bm, gx, gy);
                alpha[(size_t)gy * w + gx] = g_gamma_lut[raw];
            }
        }
    }

    out->alpha     = alpha;
    out->width     = w;
    out->height    = h;
    out->bearing_x = gs->bitmap_left;
    out->bearing_y = gs->bitmap_top;
    return true;
}

void cgui_glyph_bitmap_destroy(CguiGlyphBitmap *bm)
{
    if (!bm) return;
    free(bm->alpha);
    memset(bm, 0, sizeof(*bm));
}
