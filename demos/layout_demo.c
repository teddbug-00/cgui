/**
 * layout_demo.c – Responsive HBox and VBox layout showcase.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "layout.h"
#include "vk_context.h"
#include "vk_draw.h"
#include "spv_loader.h"

typedef struct DemoTile {
    Widget base;
    const char *title;
    const char *description;
    uint8_t color[4];
} DemoTile;

static VkDraw *g_draw;

static bool load_spv_candidates(const char **paths, uint32_t **words,
                                size_t *count)
{
    for (int i = 0; paths[i]; ++i) {
        if (spv_load_file(paths[i], words, count)) return true;
    }
    return false;
}

static bool load_shaders(VkDrawShaders *shaders, uint32_t **words,
                         size_t *sizes)
{
    const char *rr_v[] = {
        "build/rounded_rect.vert.spv", "build-release/rounded_rect.vert.spv",
        "rounded_rect.vert.spv", NULL
    };
    const char *rr_f[] = {
        "build/rounded_rect.frag.spv", "build-release/rounded_rect.frag.spv",
        "rounded_rect.frag.spv", NULL
    };
    const char *bl_v[] = {
        "build/blur.vert.spv", "build-release/blur.vert.spv",
        "blur.vert.spv", NULL
    };
    const char *bl_f[] = {
        "build/blur.frag.spv", "build-release/blur.frag.spv",
        "blur.frag.spv", NULL
    };
    const char *bt_v[] = {
        "build/blit.vert.spv", "build-release/blit.vert.spv",
        "blit.vert.spv", NULL
    };
    const char *bt_f[] = {
        "build/blit.frag.spv", "build-release/blit.frag.spv",
        "blit.frag.spv", NULL
    };
    const char *ic_f[] = {
        "build/icon.frag.spv", "build-release/icon.frag.spv",
        "icon.frag.spv", NULL
    };
    const char *tx_v[] = {
        "build/text.vert.spv", "build-release/text.vert.spv",
        "text.vert.spv", NULL
    };
    const char *tx_f[] = {
        "build/text.frag.spv", "build-release/text.frag.spv",
        "text.frag.spv", NULL
    };
    const char **paths[] = { rr_v, rr_f, bl_v, bl_f, bt_v, bt_f, ic_f, tx_v, tx_f };
    for (size_t i = 0; i < 9; ++i) {
        if (!load_spv_candidates(paths[i], &words[i], &sizes[i])) {
            fprintf(stderr, "Missing SPIR-V shaders; build the project first\n");
            return false;
        }
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
    DemoTile *tile = (DemoTile *)widget;
    static const uint8_t title_color[4] = { 34, 39, 48, 255 };
    static const uint8_t detail_color[4] = { 84, 89, 98, 255 };
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         10.0f, 0.5f, tile->color);
    (void)vk_draw_text(g_draw, tile->title,
                       widget->bounds.x + 14.0f, widget->bounds.y + 13.0f,
                       16.0f, title_color);
    (void)vk_draw_text(g_draw, tile->description,
                       widget->bounds.x + 14.0f, widget->bounds.y + 38.0f,
                       12.0f, detail_color);

    char allocated[48];
    snprintf(allocated, sizeof(allocated), "%.0f x %.0f px",
             widget->bounds.w, widget->bounds.h);
    (void)vk_draw_text(g_draw, allocated,
                       widget->bounds.x + 14.0f, widget->bounds.y + 58.0f,
                       11.0f, detail_color);
}

static DemoTile *tile_create(const char *title, const char *description,
                             const uint8_t color[4],
                             float requested_width, float requested_height)
{
    DemoTile *tile = calloc(1, sizeof(*tile));
    if (!tile) return NULL;
    tile->base.style = style_default();
    tile->base.style.width = requested_width;
    tile->base.style.height = requested_height;
    tile->base.style.shadow_color[3] = 0;
    tile->base.draw = tile_draw;
    tile->base.handle_event = widget_event_noop;
    tile->base.destroy = widget_destroy_noop;
    tile->title = title;
    tile->description = description;
    memcpy(tile->color, color, sizeof(tile->color));
    return tile;
}

static bool add_tile(LayoutContainer *layout, const char *title,
                     const char *description, const uint8_t color[4],
                     float requested_width, float requested_height)
{
    DemoTile *tile = tile_create(title, description, color,
                                 requested_width, requested_height);
    if (!tile) return false;
    if (!layout_container_add_child(layout, (Widget *)tile)) {
        widget_destroy((Widget *)tile);
        return false;
    }
    return true;
}

static bool create_showcase(LayoutContainer *root, float width, float height)
{
    static const uint8_t panel_color[4] = { 238, 240, 244, 255 };
    static const uint8_t fixed_color[4] = { 224, 232, 247, 255 };
    static const uint8_t flexible_color[4] = { 224, 241, 231, 255 };
    static const uint8_t fixed_alt_color[4] = { 248, 232, 218, 255 };

    Widget *root_widget = layout_container_widget(root);
    widget_set_bounds(root_widget, (Rect){0.0f, 0.0f, width, height});

    static const uint8_t header_color[4] = { 225, 237, 252, 255 };
    if (!add_tile(root, "Layout containers",
                  "Resize the window: child bounds and flexible space update automatically.",
                  header_color, 0.0f, 78.0f)) {
        return false;
    }

    LayoutContainer *body = layout_container_create((Rect){0},
        layout_style_default(LAYOUT_HORIZONTAL));
    if (!body) return false;
    LayoutStyle body_style = layout_style_default(LAYOUT_HORIZONTAL);
    body_style.spacing = 24.0f;
    if (!layout_container_set_style(body, &body_style) ||
        !layout_container_add_child(root, layout_container_widget(body))) {
        widget_destroy(layout_container_widget(body));
        return false;
    }

    LayoutContainer *horizontal_column = layout_container_create((Rect){0},
        layout_style_default(LAYOUT_VERTICAL));
    LayoutContainer *vertical_column = layout_container_create((Rect){0},
        layout_style_default(LAYOUT_VERTICAL));
    if (!horizontal_column || !vertical_column) {
        widget_destroy(layout_container_widget(horizontal_column));
        widget_destroy(layout_container_widget(vertical_column));
        return false;
    }
    if (!layout_container_add_child(body,
                                    layout_container_widget(horizontal_column)) ||
        !layout_container_add_child(body,
                                    layout_container_widget(vertical_column))) {
        if (!layout_container_widget(horizontal_column)->parent)
            widget_destroy(layout_container_widget(horizontal_column));
        if (!layout_container_widget(vertical_column)->parent)
            widget_destroy(layout_container_widget(vertical_column));
        return false;
    }

    LayoutStyle column_style = layout_style_default(LAYOUT_VERTICAL);
    column_style.spacing = 16.0f;
    if (!layout_container_set_style(horizontal_column, &column_style) ||
        !layout_container_set_style(vertical_column, &column_style)) {
        return false;
    }

    if (!add_tile(horizontal_column, "HBox / horizontal",
                  "Padding 20 px  |  Spacing 16 px  |  Fixed + weighted flex",
                  panel_color, 0.0f, 76.0f)) {
        return false;
    }

    LayoutStyle hbox_style = layout_style_default(LAYOUT_HORIZONTAL);
    hbox_style.padding = 20.0f;
    hbox_style.spacing = 16.0f;
    hbox_style.cross_align = LAYOUT_ALIGN_CENTER;
    LayoutContainer *hbox = layout_container_create((Rect){0}, hbox_style);
    if (!hbox || !layout_container_add_child(horizontal_column,
                                              layout_container_widget(hbox))) {
        widget_destroy(layout_container_widget(hbox));
        return false;
    }
    DemoTile *fixed_start = tile_create("Fixed", "width hint: 92 px",
                                        fixed_color, 92.0f, 0.0f);
    DemoTile *flex_one = tile_create("Flex 1", "weight 1 | align start",
                                     flexible_color, 0.0f, 0.0f);
    DemoTile *flex_two = tile_create("Flex 2", "weight 2 | align end",
                                     fixed_alt_color, 0.0f, 0.0f);
    DemoTile *fixed_end = tile_create("Fixed", "width hint: 92 px",
                                      fixed_color, 92.0f, 0.0f);
    if (!fixed_start || !flex_one || !flex_two || !fixed_end) {
        widget_destroy((Widget *)fixed_start);
        widget_destroy((Widget *)flex_one);
        widget_destroy((Widget *)flex_two);
        widget_destroy((Widget *)fixed_end);
        return false;
    }
    flex_one->base.style.height = 84.0f;
    flex_two->base.style.height = 120.0f;
    LayoutChildStyle flex_one_style = layout_child_style_default();
    flex_one_style.flex_weight = 1.0f;
    flex_one_style.min_size = 80.0f;
    flex_one_style.max_size = 160.0f;
    flex_one_style.margin = (LayoutInsets) {
        .top = 12.0f, .right = 6.0f, .bottom = 8.0f, .left = 6.0f,
    };
    flex_one_style.cross_align = LAYOUT_ALIGN_START;
    LayoutChildStyle flex_two_style = layout_child_style_default();
    flex_two_style.flex_weight = 2.0f;
    flex_two_style.min_size = 100.0f;
    flex_two_style.margin = (LayoutInsets) {
        .top = 6.0f, .right = 8.0f, .bottom = 14.0f, .left = 8.0f,
    };
    flex_two_style.cross_align = LAYOUT_ALIGN_END;
    if (!layout_container_add_child(hbox, (Widget *)fixed_start) ||
        !layout_container_add_child(hbox, (Widget *)flex_one) ||
        !layout_container_add_child(hbox, (Widget *)flex_two) ||
        !layout_container_add_child(hbox, (Widget *)fixed_end) ||
        !layout_container_set_child_style(hbox, (Widget *)flex_one,
                                          flex_one_style) ||
        !layout_container_set_child_style(hbox, (Widget *)flex_two,
                                          flex_two_style)) {
        if (!fixed_start->base.parent) widget_destroy((Widget *)fixed_start);
        if (!flex_one->base.parent) widget_destroy((Widget *)flex_one);
        if (!flex_two->base.parent) widget_destroy((Widget *)flex_two);
        if (!fixed_end->base.parent) widget_destroy((Widget *)fixed_end);
        return false;
    }

    if (!add_tile(vertical_column, "VBox / vertical",
                  "Padding 20 px  |  Spacing 16 px  |  Fixed + flexible + fixed",
                  panel_color, 0.0f, 76.0f)) {
        return false;
    }
    LayoutStyle vbox_style = layout_style_default(LAYOUT_VERTICAL);
    vbox_style.padding = 20.0f;
    vbox_style.spacing = 16.0f;
    vbox_style.cross_align = LAYOUT_ALIGN_CENTER;
    LayoutContainer *vbox = layout_container_create((Rect){0}, vbox_style);
    if (!vbox || !layout_container_add_child(vertical_column,
                                              layout_container_widget(vbox))) {
        widget_destroy(layout_container_widget(vbox));
        return false;
    }
    if (!add_tile(vbox, "Fixed", "height hint: 96 px",
                  fixed_color, 0.0f, 96.0f) ||
        !add_tile(vbox, "Flexible", "takes remaining height",
                  flexible_color, 0.0f, 0.0f) ||
        !add_tile(vbox, "Fixed", "height hint: 96 px",
                  fixed_alt_color, 0.0f, 96.0f)) {
        return false;
    }

    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - HBox and VBox layout", 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    VkContext context = {0};
    VkDraw draw = {0};
    uint32_t *shader_words[9] = {0};
    size_t shader_sizes[9] = {0};
    bool context_created = false;
    bool draw_created = false;
    bool running = true;
    bool resized = false;
    int result = EXIT_FAILURE;
    LayoutStyle root_style = layout_style_default(LAYOUT_VERTICAL);
    root_style.padding = 28.0f;
    root_style.spacing = 20.0f;
    LayoutContainer *root = layout_container_create((Rect){0}, root_style);

    if (!root) {
        fprintf(stderr, "Failed to create root layout\n");
        goto cleanup;
    }
    if (!vk_context_create(&context, window)) {
        fprintf(stderr, "Vulkan context failed\n");
        goto cleanup;
    }
    context_created = true;

    VkDrawShaders shaders = {0};
    if (!load_shaders(&shaders, shader_words, shader_sizes)) goto cleanup;
    if (!vk_draw_create(&draw, &context, &shaders)) {
        fprintf(stderr, "UI draw pipeline failed\n");
        goto cleanup;
    }
    draw_created = true;
    g_draw = &draw;
    for (size_t i = 0; i < 9; ++i) {
        free(shader_words[i]);
        shader_words[i] = NULL;
    }

    if (!create_showcase(root, (float)context.swapchain_extent.width,
                         (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct layout showcase\n");
        goto cleanup;
    }

    result = EXIT_SUCCESS;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                resized = true;
                break;
            default:
                break;
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
                widget_set_bounds(layout_container_widget(root),
                    (Rect){0.0f, 0.0f,
                           (float)context.swapchain_extent.width,
                           (float)context.swapchain_extent.height});
                resized = false;
            }
            if (resized) {
                SDL_Delay(16);
                continue;
            }
        }
        if (!vk_context_begin_frame(&context)) {
            resized = true;
            continue;
        }
        Widget *root_widget = layout_container_widget(root);
        if (root_widget->draw) root_widget->draw(root_widget, &context);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    g_draw = NULL;
    for (size_t i = 0; i < 9; ++i) free(shader_words[i]);
    widget_destroy(layout_container_widget(root));
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
