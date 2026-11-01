/**
 * popover_menu_demo.c - Popover menu interaction and styling showcase.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "popover_menu.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct MenuShowcase {
    Widget base;
    Button *trigger;
    PopoverMenu *menu;
    const char *last_action;
} MenuShowcase;

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

static void trigger_clicked(Button *button, void *userdata)
{
    (void)button;
    MenuShowcase *showcase = userdata;
    popover_menu_set_open(showcase->menu,
                          !popover_menu_is_open(showcase->menu));
}

static void menu_selected(PopoverMenu *menu, size_t index, void *userdata)
{
    (void)menu;
    static const char *const actions[] = {
        "New File", "Open...", "Save", "Save As...", "Close",
    };
    MenuShowcase *showcase = userdata;
    if (index < sizeof(actions) / sizeof(actions[0]))
        showcase->last_action = actions[index];
}

static void showcase_layout(Widget *widget)
{
    MenuShowcase *showcase = (MenuShowcase *)widget;
    showcase->trigger->base.bounds = (Rect) {
        widget->bounds.x + 32.0f, widget->bounds.y + 112.0f,
        150.0f, 38.0f,
    };
    Widget *menu_widget = popover_menu_widget(showcase->menu);
    menu_widget->bounds.x = widget->bounds.x + 32.0f;
    menu_widget->bounds.y = widget->bounds.y + 158.0f;
    menu_widget->bounds.w = 230.0f;
    widget_layout(popover_menu_widget(showcase->menu));
}

static void showcase_draw(Widget *widget, struct VkContext *context)
{
    static const uint8_t background[4] = {249, 250, 252, 255};
    static const uint8_t heading[4] = {37, 39, 43, 255};
    static const uint8_t description[4] = {103, 108, 117, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         0.0f, 0.5f, background);
    (void)vk_draw_text(g_draw, "Popover Menu",
                       widget->bounds.x + 32.0f, widget->bounds.y + 34.0f,
                       22.0f, heading);
    (void)vk_draw_text(g_draw,
        "Click the button, move through items, or use the arrow keys and Enter.",
        widget->bounds.x + 32.0f, widget->bounds.y + 72.0f, 14.0f,
        description);
    if (widget->child_count > 0 && widget->children[0]->draw)
        widget->children[0]->draw(widget->children[0], context);
    if (widget->child_count > 1 && widget->children[1]->draw)
        widget->children[1]->draw(widget->children[1], context);

    char status[96];
    snprintf(status, sizeof(status), "Last action: %s",
             ((MenuShowcase *)widget)->last_action);
    (void)vk_draw_text(g_draw, status,
                       widget->bounds.x + 32.0f, widget->bounds.y + 360.0f,
                       14.0f, description);
}

static void showcase_event(Widget *widget, const void *event)
{
    for (size_t i = 0; i < widget->child_count; ++i) {
        Widget *child = widget->children[i];
        if (child->handle_event) child->handle_event(child, event);
    }
}

static bool create_showcase(MenuShowcase **out, float width, float height)
{
    MenuShowcase *showcase = calloc(1, sizeof(*showcase));
    if (!showcase) return false;
    showcase->base.bounds = (Rect){0.0f, 0.0f, width, height};
    showcase->base.style = style_default();
    showcase->base.draw = showcase_draw;
    showcase->base.handle_event = showcase_event;
    showcase->base.destroy = widget_destroy_noop;
    showcase->last_action = "None";

    static const uint8_t hover[4] = {235, 238, 243, 255};
    static const uint8_t pressed[4] = {222, 226, 232, 255};
    Style button_style = style_default();
    memcpy(button_style.bg, (uint8_t[]){255, 255, 255, 255}, 4);
    button_style.border_width = 0.7f;
    showcase->trigger = button_create((Rect){0}, button_style, "Actions");
    showcase->menu = popover_menu_create((Rect){0}, NULL);
    if (!showcase->trigger || !showcase->menu) {
        widget_destroy((Widget *)showcase->trigger);
        widget_destroy(popover_menu_widget(showcase->menu));
        widget_destroy((Widget *)showcase);
        return false;
    }
    button_set_interaction_colors(showcase->trigger, hover, pressed);
    button_set_click_handler(showcase->trigger, trigger_clicked, showcase);
    popover_menu_set_select_handler(showcase->menu, menu_selected, showcase);
    static const struct {
        const char *label;
        bool enabled;
    } items[] = {
        {"New File", true},
        {"Open...", true},
        {"Save", true},
        {"Save As...", true},
        {"Close", false},
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        if (!popover_menu_add_item(showcase->menu,
                                   items[i].label, items[i].enabled)) {
            widget_destroy((Widget *)showcase->trigger);
            widget_destroy(popover_menu_widget(showcase->menu));
            widget_destroy((Widget *)showcase);
            return false;
        }
    }
    if (!widget_add_child(&showcase->base, (Widget *)showcase->trigger)) {
        widget_destroy((Widget *)showcase->trigger);
        widget_destroy((Widget *)showcase->menu);
        widget_destroy((Widget *)showcase);
        return false;
    }
    if (!widget_add_child(&showcase->base, popover_menu_widget(showcase->menu))) {
        widget_destroy((Widget *)showcase->menu);
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
        "cgui - Popover Menu", 800, 440,
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
    MenuShowcase *showcase = NULL;

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
    popover_menu_set_draw_context(&draw);
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    if (!create_showcase(&showcase,
                         (float)context.swapchain_extent.width,
                         (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct Popover Menu showcase\n");
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
                showcase_layout((Widget *)showcase);
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
    popover_menu_set_draw_context(NULL);
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy((Widget *)showcase);
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
