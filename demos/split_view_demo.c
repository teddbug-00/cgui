/**
 * split_view_demo.c - Editor workspace using nested draggable split panes.
 */

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "split_view.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct DemoPane {
    Widget base;
    const char *title;
    const char *body;
    uint8_t background[4];
} DemoPane;

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

static void pane_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    DemoPane *pane = (DemoPane *)widget;
    static const uint8_t title_color[4] = {48, 52, 59, 255};
    static const uint8_t body_color[4] = {91, 96, 105, 255};
    static const uint8_t divider_color[4] = {223, 225, 229, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         0.0f, 0.5f, pane->background);
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, 34.0f,
                         0.0f, 0.5f, (uint8_t[]){245, 246, 248, 255});
    vk_draw_rounded_rect(g_draw, widget->bounds.x,
                         widget->bounds.y + 33.0f,
                         widget->bounds.w, 1.0f, 0.5f, 0.5f,
                         divider_color);
    (void)vk_draw_text(g_draw, pane->title, widget->bounds.x + 14.0f,
                       widget->bounds.y + 10.0f, 13.0f, title_color);
    float y = widget->bounds.y + 54.0f;
    const char *line = pane->body;
    while (line && *line && y < widget->bounds.y + widget->bounds.h - 18.0f) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char text[256];
        if (length >= sizeof(text)) length = sizeof(text) - 1;
        memcpy(text, line, length);
        text[length] = '\0';
        (void)vk_draw_text(g_draw, text, widget->bounds.x + 16.0f,
                           y, 13.0f, body_color);
        y += 23.0f;
        if (!end) break;
        line = end + 1;
    }
}

static DemoPane *pane_create(const char *title, const char *body,
                             const uint8_t background[4])
{
    DemoPane *pane = calloc(1, sizeof(*pane));
    if (!pane) return NULL;
    pane->base.style = style_default();
    pane->base.draw = pane_draw;
    pane->base.handle_event = widget_event_noop;
    pane->base.destroy = widget_destroy_noop;
    pane->title = title;
    pane->body = body;
    memcpy(pane->background, background, 4);
    return pane;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - SplitView", 1080, 700,
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
    bool editor_stack_attached = false;
    int result = EXIT_FAILURE;
    SplitView *workspace = NULL;
    SplitView *editor_stack = NULL;

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

    static const uint8_t sidebar_bg[4] = {252, 252, 253, 255};
    static const uint8_t editor_bg[4] = {255, 255, 255, 255};
    static const uint8_t output_bg[4] = {248, 249, 251, 255};
    DemoPane *files = pane_create(
        "EXPLORER",
        "cgui\n  src\n    split_view.c\n    tab_view.c\n    button.c\n"
        "  demos\n    split_view_demo.c\n  tests\n    split_view_test.c",
        sidebar_bg);
    DemoPane *editor = pane_create(
        "split_view.c",
        "#include \"split_view.h\"\n\n"
        "typedef struct SplitView {\n"
        "    Widget base;\n"
        "    float ratio;\n"
        "} SplitView;\n\n"
        "static void split_view_layout(Widget *self)\n"
        "{\n    /* Keep both panes within their minimum sizes. */\n}",
        editor_bg);
    DemoPane *output = pane_create(
        "OUTPUT",
        "Build succeeded.\nAll tests passed.\nDrag either divider to resize.",
        output_bg);

    SplitViewStyle workspace_style = split_view_style_default();
    workspace_style.ratio = 0.25f;
    workspace_style.first_min_size = 170.0f;
    workspace_style.second_min_size = 300.0f;
    SplitViewStyle editor_style = split_view_style_default();
    editor_style.orientation = SPLIT_VIEW_VERTICAL;
    editor_style.ratio = 0.72f;
    editor_style.first_min_size = 220.0f;
    editor_style.second_min_size = 110.0f;
    workspace = split_view_create(
        (Rect){0.0f, 38.0f, (float)context.swapchain_extent.width,
               fmaxf(0.0f, (float)context.swapchain_extent.height - 38.0f)},
        &workspace_style);
    editor_stack = split_view_create((Rect){0}, &editor_style);
    if (!files || !editor || !output || !workspace || !editor_stack ||
        !split_view_set_panes(editor_stack, (Widget *)editor,
                              (Widget *)output) ||
        !split_view_set_panes(workspace, (Widget *)files,
                              split_view_widget(editor_stack))) {
        fprintf(stderr, "Failed to construct SplitView workspace\n");
        if (files && !files->base.parent) widget_destroy((Widget *)files);
        if (editor && !editor->base.parent) widget_destroy((Widget *)editor);
        if (output && !output->base.parent) widget_destroy((Widget *)output);
        goto cleanup;
    }
    editor_stack_attached = true;

    result = EXIT_SUCCESS;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                resized = true;
            }
            split_view_widget(workspace)->handle_event(
                split_view_widget(workspace), &event);
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
                widget_set_bounds(split_view_widget(workspace),
                    (Rect){0.0f, 38.0f,
                           (float)context.swapchain_extent.width,
                           fmaxf(0.0f,
                               (float)context.swapchain_extent.height - 38.0f)});
                resized = false;
            }
        }
        if (!vk_context_begin_frame(&context)) {
            resized = true;
            continue;
        }
        static const uint8_t heading[4] = {48, 52, 59, 255};
        (void)vk_draw_text(&draw, "Workspace  /  drag the split dividers",
                           16.0f, 12.0f, 14.0f, heading);
        split_view_widget(workspace)->draw(split_view_widget(workspace),
                                            &context);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    g_draw = NULL;
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy(split_view_widget(workspace));
    if (editor_stack && !editor_stack_attached)
        widget_destroy(split_view_widget(editor_stack));
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
