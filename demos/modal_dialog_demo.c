/**
 * modal_dialog_demo.c - Modal confirmation dialog showcase.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "modal_dialog.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct DialogContent {
    Widget base;
} DialogContent;

typedef struct DialogShowcase {
    Widget base;
    Button *trigger;
    ModalDialog *dialog;
    const char *last_action;
} DialogShowcase;

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

static void content_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    static const uint8_t surface[4] = {247, 248, 250, 255};
    static const uint8_t text[4] = {74, 78, 86, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         6.0f, 0.5f, surface);
    (void)vk_draw_text(g_draw, "The file will be permanently removed.",
                       widget->bounds.x + 12.0f,
                       widget->bounds.y + 13.0f, 13.0f, text);
}

static void trigger_clicked(Button *button, void *userdata)
{
    (void)button;
    modal_dialog_open(((DialogShowcase *)userdata)->dialog);
}

static void dialog_action(ModalDialog *dialog, size_t index, void *userdata)
{
    (void)dialog;
    static const char *const actions[] = {"Canceled", "File deleted"};
    DialogShowcase *showcase = userdata;
    if (index < sizeof(actions) / sizeof(actions[0]))
        showcase->last_action = actions[index];
}

static void showcase_layout(Widget *widget)
{
    DialogShowcase *showcase = (DialogShowcase *)widget;
    showcase->trigger->base.bounds = (Rect) {
        widget->bounds.x + 36.0f, widget->bounds.y + 132.0f,
        180.0f, 40.0f,
    };
    modal_dialog_widget(showcase->dialog)->bounds = widget->bounds;
    widget_layout(modal_dialog_widget(showcase->dialog));
}

static void showcase_draw(Widget *widget, struct VkContext *context)
{
    static const uint8_t background[4] = {249, 250, 252, 255};
    static const uint8_t heading[4] = {37, 39, 43, 255};
    static const uint8_t secondary[4] = {103, 108, 117, 255};
    DialogShowcase *showcase = (DialogShowcase *)widget;
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         0.0f, 0.5f, background);
    (void)vk_draw_text(g_draw, "Modal Dialog",
                       widget->bounds.x + 36.0f, widget->bounds.y + 40.0f,
                       22.0f, heading);
    (void)vk_draw_text(g_draw,
        "The backdrop blocks the underlying view; Escape or click outside to dismiss.",
        widget->bounds.x + 36.0f, widget->bounds.y + 78.0f, 14.0f,
        secondary);
    if (showcase->trigger->base.draw)
        showcase->trigger->base.draw((Widget *)showcase->trigger, context);
    char status[96];
    snprintf(status, sizeof(status), "Last result: %s", showcase->last_action);
    (void)vk_draw_text(g_draw, status,
                       widget->bounds.x + 36.0f, widget->bounds.y + 198.0f,
                       14.0f, secondary);
    if (modal_dialog_is_open(showcase->dialog))
        modal_dialog_widget(showcase->dialog)->draw(
            modal_dialog_widget(showcase->dialog), context);
}

static void showcase_event(Widget *widget, const void *event)
{
    DialogShowcase *showcase = (DialogShowcase *)widget;
    if (modal_dialog_is_open(showcase->dialog)) {
        modal_dialog_widget(showcase->dialog)->handle_event(
            modal_dialog_widget(showcase->dialog), event);
    } else if (showcase->trigger->base.handle_event) {
        showcase->trigger->base.handle_event(
            (Widget *)showcase->trigger, event);
    }
}

static bool create_showcase(DialogShowcase **out, float width, float height)
{
    DialogShowcase *showcase = calloc(1, sizeof(*showcase));
    if (!showcase) return false;
    showcase->base.bounds = (Rect){0.0f, 0.0f, width, height};
    showcase->base.style = style_default();
    showcase->base.draw = showcase_draw;
    showcase->base.handle_event = showcase_event;
    showcase->base.destroy = widget_destroy_noop;
    showcase->base.layout = showcase_layout;
    showcase->last_action = "None";

    Style trigger_style = style_default();
    trigger_style.corner_radius = 7.0f;
    memcpy(trigger_style.bg, (uint8_t[]){255, 255, 255, 255}, 4);
    showcase->trigger = button_create((Rect){0}, trigger_style,
                                      "Delete a file...");
    showcase->dialog = modal_dialog_create(
        (Rect){0}, "Delete document?",
        "Are you sure you want to delete notes.txt?", NULL);
    DialogContent *content = calloc(1, sizeof(*content));
    if (!showcase->trigger || !showcase->dialog || !content) {
        widget_destroy((Widget *)showcase->trigger);
        widget_destroy(modal_dialog_widget(showcase->dialog));
        free(content);
        widget_destroy((Widget *)showcase);
        return false;
    }
    content->base.style = style_default();
    content->base.draw = content_draw;
    content->base.handle_event = widget_event_noop;
    content->base.destroy = widget_destroy_noop;
    if (!modal_dialog_set_content(showcase->dialog, (Widget *)content, 44.0f) ||
        !modal_dialog_add_action(showcase->dialog, "Cancel", false) ||
        !modal_dialog_add_action(showcase->dialog, "Delete", true)) {
        if (!content->base.parent) widget_destroy((Widget *)content);
        widget_destroy((Widget *)showcase->trigger);
        widget_destroy(modal_dialog_widget(showcase->dialog));
        widget_destroy((Widget *)showcase);
        return false;
    }
    button_set_click_handler(showcase->trigger, trigger_clicked, showcase);
    modal_dialog_set_action_handler(showcase->dialog, dialog_action, showcase);
    if (!widget_add_child(&showcase->base, (Widget *)showcase->trigger)) {
        widget_destroy((Widget *)showcase->trigger);
        widget_destroy(modal_dialog_widget(showcase->dialog));
        widget_destroy((Widget *)showcase);
        return false;
    }
    if (!widget_add_child(&showcase->base,
                          modal_dialog_widget(showcase->dialog))) {
        widget_destroy(modal_dialog_widget(showcase->dialog));
        widget_destroy((Widget *)showcase);
        return false;
    }
    showcase_layout(&showcase->base);
    *out = showcase;
    return true;
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - Modal Dialog", 800, 440,
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
    DialogShowcase *showcase = NULL;

    if (!vk_context_create(&context, window)) goto cleanup;
    context_created = true;
    VkDrawShaders shaders = {0};
    if (!load_shaders(&shaders, words, sizes) ||
        !vk_draw_create(&draw, &context, &shaders)) {
        goto cleanup;
    }
    draw_created = true;
    g_draw = &draw;
    button_set_draw_context(&draw);
    modal_dialog_set_draw_context(&draw);
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    if (!create_showcase(&showcase,
                         (float)context.swapchain_extent.width,
                         (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct Modal Dialog showcase\n");
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
            showcase->base.handle_event((Widget *)showcase, &event);
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
                widget_set_bounds((Widget *)showcase,
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
        showcase->base.draw((Widget *)showcase, &context);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    g_draw = NULL;
    button_set_draw_context(NULL);
    modal_dialog_set_draw_context(NULL);
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy((Widget *)showcase);
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
