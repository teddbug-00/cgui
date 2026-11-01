/**
 * grid_demo.c - Responsive fixed and flexible grid showcase.
 */

#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include "grid.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct GridTile {
    Widget base;
    const char *title;
    const char *detail;
    uint8_t color[4];
} GridTile;

static VkDraw *g_draw;

static bool load_shader(const char *name, uint32_t **words, size_t *size)
{
    char path[128];
    snprintf(path, sizeof(path), "build/%s.spv", name);
    if (spv_load_file(path, words, size)) return true;
    snprintf(path, sizeof(path), "build-release/%s.spv", name);
    if (spv_load_file(path, words, size)) return true;
    fprintf(stderr, "Cannot load shader %s; build the project first\n", name);
    return false;
}

static bool load_shaders(VkDrawShaders *shaders,
                         uint32_t **words, size_t *sizes)
{
    static const char *names[] = {
        "rounded_rect.vert", "rounded_rect.frag", "blur.vert", "blur.frag",
        "blit.vert", "blit.frag", "icon.frag", "text.vert", "text.frag",
    };
    for (size_t i = 0; i < 9; ++i) {
        if (!load_shader(names[i], &words[i], &sizes[i])) return false;
    }
    *shaders = (VkDrawShaders) {
        .rr_vert = words[0], .rr_vert_n = sizes[0],
        .rr_frag = words[1], .rr_frag_n = sizes[1],
        .blur_vert = words[2], .blur_vert_n = sizes[2],
        .blur_frag = words[3], .blur_frag_n = sizes[3],
        .blit_vert = words[4], .blit_vert_n = sizes[4],
        .blit_frag = words[5], .blit_frag_n = sizes[5],
        .icon_frag = words[6], .icon_frag_n = sizes[6],
        .txt_vert = words[7], .txt_vert_n = sizes[7],
        .txt_frag = words[8], .txt_frag_n = sizes[8],
    };
    return true;
}

static void tile_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    GridTile *tile = (GridTile *)widget;
    static const uint8_t title_color[4] = {34, 39, 48, 255};
    static const uint8_t detail_color[4] = {84, 89, 98, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         10.0f, 0.5f, tile->color);
    (void)vk_draw_text(g_draw, tile->title, widget->bounds.x + 14.0f,
                       widget->bounds.y + 13.0f, 16.0f, title_color);
    (void)vk_draw_text(g_draw, tile->detail, widget->bounds.x + 14.0f,
                       widget->bounds.y + 40.0f, 12.0f, detail_color);
    char dimensions[48];
    snprintf(dimensions, sizeof(dimensions), "%.0f x %.0f px",
             widget->bounds.w, widget->bounds.h);
    (void)vk_draw_text(g_draw, dimensions, widget->bounds.x + 14.0f,
                       widget->bounds.y + 62.0f, 11.0f, detail_color);
}

static GridTile *tile_create(const char *title, const char *detail,
                             const uint8_t color[4])
{
    GridTile *tile = calloc(1, sizeof(*tile));
    if (!tile) return NULL;
    tile->base.style = style_default();
    tile->base.style.shadow_color[3] = 0;
    tile->base.draw = tile_draw;
    tile->base.handle_event = widget_event_noop;
    tile->base.destroy = widget_destroy_noop;
    tile->title = title;
    tile->detail = detail;
    for (size_t i = 0; i < 4; ++i) tile->color[i] = color[i];
    return tile;
}

static bool add_tile(GridLayout *grid, const char *title, const char *detail,
                     const uint8_t color[4], size_t row, size_t column,
                     size_t row_span, size_t column_span,
                     GridChildStyle style)
{
    GridTile *tile = tile_create(title, detail, color);
    if (!tile) return false;
    if (!grid_layout_add_child(grid, (Widget *)tile, row, column,
                               row_span, column_span) ||
        !grid_layout_set_child_style(grid, (Widget *)tile, style)) {
        if (!tile->base.parent) widget_destroy((Widget *)tile);
        return false;
    }
    return true;
}

static bool build_grid(GridLayout **out, float width, float height)
{
    static const uint8_t colors[][4] = {
        {225, 237, 252, 255}, {224, 232, 247, 255},
        {224, 241, 231, 255}, {248, 232, 218, 255},
        {237, 229, 248, 255}, {242, 238, 222, 255},
    };
    const GridTrack columns[] = {
        grid_track_fixed(180.0f),
        grid_track_flexible(1.0f, 120.0f, 280.0f),
        grid_track_flexible(2.0f, 160.0f, 0.0f),
    };
    const GridTrack rows[] = {
        grid_track_fixed(120.0f),
        grid_track_flexible(1.0f, 120.0f, 0.0f),
        grid_track_flexible(2.0f, 160.0f, 0.0f),
    };
    GridStyle style = {
        .row_gap = 16.0f,
        .column_gap = 16.0f,
        .padding = 28.0f,
    };
    GridLayout *grid = grid_layout_create(
        (Rect){0.0f, 0.0f, width, height}, style,
        columns, 3, rows, 3);
    if (!grid) return false;

    GridChildStyle default_style = grid_child_style_default();
    GridChildStyle centered = default_style;
    centered.horizontal_align = LAYOUT_ALIGN_CENTER;
    centered.vertical_align = LAYOUT_ALIGN_CENTER;
    centered.margin = (LayoutInsets){8.0f, 12.0f, 8.0f, 12.0f};
    GridChildStyle end_aligned = default_style;
    end_aligned.horizontal_align = LAYOUT_ALIGN_END;
    end_aligned.vertical_align = LAYOUT_ALIGN_END;
    end_aligned.margin = (LayoutInsets){12.0f, 16.0f, 16.0f, 8.0f};

    if (!add_tile(grid, "Fixed track", "180 px column", colors[0],
                  0, 0, 1, 1, default_style) ||
        !add_tile(grid, "Flexible", "weight 1 | max 280 px", colors[1],
                  0, 1, 1, 1, centered) ||
        !add_tile(grid, "Weighted", "weight 2 | fills remaining", colors[2],
                  0, 2, 1, 1, default_style) ||
        !add_tile(grid, "Spanning cell", "spans two columns", colors[3],
                  1, 0, 1, 2, centered) ||
        !add_tile(grid, "Flexible row", "fills available cell", colors[4],
                  1, 2, 1, 1, default_style) ||
        !add_tile(grid, "Span rows + columns", "weighted tracks reflow on resize",
                  colors[5], 2, 0, 1, 3, end_aligned)) {
        widget_destroy(grid_layout_widget(grid));
        return false;
    }
    *out = grid;
    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - Grid layout", 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    VkContext context = {0};
    VkDraw draw = {0};
    uint32_t *words[9] = {0};
    size_t sizes[9] = {0};
    bool context_created = false;
    bool draw_created = false;
    bool running = true;
    bool resized = false;
    int result = EXIT_FAILURE;
    GridLayout *grid = NULL;

    if (!vk_context_create(&context, window)) goto cleanup;
    context_created = true;
    VkDrawShaders shaders = {0};
    if (!load_shaders(&shaders, words, sizes) ||
        !vk_draw_create(&draw, &context, &shaders)) {
        goto cleanup;
    }
    draw_created = true;
    g_draw = &draw;
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    if (!build_grid(&grid, (float)context.swapchain_extent.width,
                    (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct grid showcase\n");
        goto cleanup;
    }

    result = EXIT_SUCCESS;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                resized = true;
            }
        }
        if (resized) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            if (width > 0 && height > 0) {
                if (!vk_context_recreate_swapchain(&context) ||
                    !vk_draw_resize(&draw)) {
                    result = EXIT_FAILURE;
                    break;
                }
                widget_set_bounds(grid_layout_widget(grid),
                    (Rect){0.0f, 0.0f,
                           (float)context.swapchain_extent.width,
                           (float)context.swapchain_extent.height});
                resized = false;
            }
        }
        if (!vk_context_begin_frame(&context)) {
            resized = true;
            continue;
        }
        Widget *root = grid_layout_widget(grid);
        if (root->draw) root->draw(root, &context);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    g_draw = NULL;
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy(grid_layout_widget(grid));
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
