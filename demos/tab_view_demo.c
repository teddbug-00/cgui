/**
 * tab_view_demo.c - Code-editor-style document tabs showcase.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spv_loader.h"
#include "tab_view.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct DemoDocument {
    Widget base;
    const char *name;
    const char *source;
    uint8_t color[4];
} DemoDocument;

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

static void document_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    DemoDocument *document = (DemoDocument *)widget;
    static const uint8_t title_color[4] = {35, 39, 46, 255};
    static const uint8_t body_color[4] = {75, 81, 91, 255};
    static const uint8_t gutter_color[4] = {133, 139, 148, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         0.0f, 0.5f, document->color);
    (void)vk_draw_text(g_draw, document->name, widget->bounds.x + 28.0f,
                       widget->bounds.y + 24.0f, 20.0f, title_color);
    (void)vk_draw_text(g_draw, "1   2   3   4", widget->bounds.x + 28.0f,
                       widget->bounds.y + 76.0f, 14.0f, gutter_color);
    (void)vk_draw_text(g_draw, document->source, widget->bounds.x + 116.0f,
                       widget->bounds.y + 76.0f, 16.0f, body_color);
}

static DemoDocument *document_create(const char *name, const char *source,
                                     const uint8_t color[4])
{
    DemoDocument *document = calloc(1, sizeof(*document));
    if (!document) return NULL;
    document->base.style = style_default();
    document->base.draw = document_draw;
    document->base.handle_event = widget_event_noop;
    document->base.destroy = widget_destroy_noop;
    document->name = name;
    document->source = source;
    memcpy(document->color, color, sizeof(document->color));
    return document;
}

static bool create_tabs(TabView **out, float width, float height)
{
    static const uint8_t colors[][4] = {
        {255, 255, 255, 255}, {250, 252, 255, 255}, {255, 252, 247, 255},
    };
    static const char *titles[] = {"main.c", "layout.c", "scroll_view.c"};
    static const char *sources[] = {
        "int main(void) {\n    return 0;\n}",
        "void layout_reflow(Widget *root) {\n    widget_layout(root);\n}",
        "void scroll_view_update(ScrollView *view) {\n    advance_momentum(view);\n}",
    };
    TabView *view = tab_view_create(
        (Rect){0.0f, 0.0f, width, height}, NULL);
    if (!view) return false;
    for (size_t i = 0; i < 3; ++i) {
        DemoDocument *document = document_create(titles[i], sources[i],
                                                  colors[i]);
        if (!document ||
            !tab_view_add_tab(view, titles[i], (Widget *)document)) {
            if (document && !document->base.parent)
                widget_destroy((Widget *)document);
            widget_destroy(tab_view_widget(view));
            return false;
        }
    }
    *out = view;
    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - TabView", 1100, 720,
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
    bool resized = false;
    bool running = true;
    int result = EXIT_FAILURE;
    TabView *view = NULL;

    if (!vk_context_create(&context, window)) goto cleanup;
    context_created = true;
    VkDrawShaders shaders = {0};
    if (!load_shaders(&shaders, words, sizes) ||
        !vk_draw_create(&draw, &context, &shaders)) {
        goto cleanup;
    }
    draw_created = true;
    g_draw = &draw;
    tab_view_set_draw_context(&draw);
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    if (!create_tabs(&view, (float)context.swapchain_extent.width,
                     (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct TabView showcase\n");
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
            tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
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
                widget_set_bounds(tab_view_widget(view),
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
        tab_view_widget(view)->draw(tab_view_widget(view), &context);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    g_draw = NULL;
    tab_view_set_draw_context(NULL);
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy(tab_view_widget(view));
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
