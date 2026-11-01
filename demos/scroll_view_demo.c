/**
 * scroll_view_demo.c - Clipped scrolling content showcase.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "layout.h"
#include "scroll_view.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct DemoCard {
    Widget base;
    const char *title;
    const char *detail;
    uint8_t color[4];
} DemoCard;

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

static void card_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    DemoCard *card = (DemoCard *)widget;
    static const uint8_t title_color[4] = {34, 39, 48, 255};
    static const uint8_t detail_color[4] = {84, 89, 98, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         10.0f, 0.5f, card->color);
    (void)vk_draw_text(g_draw, card->title, widget->bounds.x + 16.0f,
                       widget->bounds.y + 13.0f, 16.0f, title_color);
    (void)vk_draw_text(g_draw, card->detail, widget->bounds.x + 16.0f,
                       widget->bounds.y + 40.0f, 12.0f, detail_color);
}

static DemoCard *card_create(const char *title, const char *detail,
                             const uint8_t color[4], float height)
{
    DemoCard *card = calloc(1, sizeof(*card));
    if (!card) return NULL;
    card->base.style = style_default();
    card->base.style.height = height;
    card->base.style.shadow_color[3] = 0;
    card->base.draw = card_draw;
    card->base.handle_event = widget_event_noop;
    card->base.destroy = widget_destroy_noop;
    card->title = title;
    card->detail = detail;
    memcpy(card->color, color, sizeof(card->color));
    return card;
}

static bool create_scene(LayoutContainer **root_out, ScrollView **view_out,
                         float width, float height)
{
    static const uint8_t header_color[4] = {225, 237, 252, 255};
    static const uint8_t colors[][4] = {
        {238, 240, 244, 255}, {232, 241, 247, 255},
        {239, 244, 235, 255}, {247, 239, 228, 255},
    };
    static const char *titles[] = {
        "Layout basics", "Typography", "Color and themes", "Buttons and states",
        "Forms", "Icons", "Panels", "Tree views", "Grid layouts",
        "Interaction", "Accessibility", "Responsive sizing",
        "Custom components", "Event handling", "Rendering", "More content",
        "Nested containers", "End of the scrollable content",
    };

    LayoutStyle root_style = layout_style_default(LAYOUT_VERTICAL);
    root_style.padding = 28.0f;
    root_style.spacing = 18.0f;
    LayoutContainer *root = layout_container_create(
        (Rect){0.0f, 0.0f, width, height}, root_style);
    if (!root) return false;

    DemoCard *header = card_create(
        "ScrollView", "Scroll either axis; drag its bar or use arrow keys.",
        header_color, 76.0f);
    if (!header || !layout_container_add_child(root, (Widget *)header)) {
        widget_destroy((Widget *)header);
        widget_destroy(layout_container_widget(root));
        return false;
    }

    ScrollView *view = scroll_view_create((Rect){0},
                                           scroll_view_style_default());
    if (!view) {
        widget_destroy(layout_container_widget(root));
        return false;
    }
    scroll_view_widget(view)->style.height = 620.0f;
    if (!layout_container_add_child(root, scroll_view_widget(view))) {
        widget_destroy(scroll_view_widget(view));
        widget_destroy(layout_container_widget(root));
        return false;
    }

    LayoutStyle content_style = layout_style_default(LAYOUT_VERTICAL);
    content_style.padding = 8.0f;
    content_style.spacing = 12.0f;
    LayoutContainer *content = layout_container_create((Rect){0},
                                                        content_style);
    if (!content || !scroll_view_set_content(
            view, layout_container_widget(content), 1528.0f)) {
        widget_destroy(layout_container_widget(content));
        widget_destroy(layout_container_widget(root));
        return false;
    }
    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        DemoCard *card = card_create(
            titles[i], "Resize the window or scroll to reveal more.",
            colors[i % 4], 72.0f);
        if (!card || !layout_container_add_child(content, (Widget *)card)) {
            widget_destroy((Widget *)card);
            widget_destroy(layout_container_widget(root));
            return false;
        }
    }
    if (!scroll_view_set_content_width(view, width + 320.0f)) {
        widget_destroy(layout_container_widget(root));
        return false;
    }
    *root_out = root;
    *view_out = view;
    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - ScrollView", 1280, 800,
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
    LayoutContainer *root = NULL;
    ScrollView *view = NULL;

    if (!vk_context_create(&context, window)) goto cleanup;
    context_created = true;
    VkDrawShaders shaders = {0};
    if (!load_shaders(&shaders, words, sizes) ||
        !vk_draw_create(&draw, &context, &shaders)) {
        goto cleanup;
    }
    draw_created = true;
    g_draw = &draw;
    scroll_view_set_draw_context(&draw);
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    if (!create_scene(&root, &view, (float)context.swapchain_extent.width,
                      (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct ScrollView showcase\n");
        goto cleanup;
    }

    result = EXIT_SUCCESS;
    Uint64 previous_ticks = SDL_GetTicksNS();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                resized = true;
            }
            Widget *root_widget = layout_container_widget(root);
            if (root_widget->handle_event) {
                root_widget->handle_event(root_widget, &event);
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
        }
        Uint64 now_ticks = SDL_GetTicksNS();
        float delta_seconds = (float)(now_ticks - previous_ticks) / 1.0e9f;
        previous_ticks = now_ticks;
        scroll_view_update(view, delta_seconds);
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
    scroll_view_set_draw_context(NULL);
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy(layout_container_widget(root));
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
