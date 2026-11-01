/**
 * button_demo.c - Button styles, sizes, and icon-button showcase.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"
#include "spv_loader.h"
#include "vk_context.h"
#include "vk_draw.h"

typedef struct ButtonShowcase {
    Widget base;
    size_t clicks;
} ButtonShowcase;

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

static Style make_style(const uint8_t background[4],
                        const uint8_t text[4], float radius)
{
    Style style = {
        .corner_radius = radius,
        .text_size = 14.0f,
        .text_color = {32, 33, 36, 255},
    };
    memcpy(style.bg, background, 4);
    memcpy(style.text_color, text, 4);
    return style;
}

static void count_click(Button *button, void *userdata)
{
    (void)button;
    ++((ButtonShowcase *)userdata)->clicks;
}

static void showcase_layout(Widget *widget)
{
    static const Rect button_bounds[] = {
        {36.0f, 142.0f, 112.0f, 28.0f},
        {168.0f, 138.0f, 132.0f, 36.0f},
        {320.0f, 134.0f, 150.0f, 44.0f},
        {490.0f, 138.0f, 136.0f, 36.0f},
        {36.0f, 268.0f, 36.0f, 36.0f},
        {92.0f, 264.0f, 44.0f, 44.0f},
        {170.0f, 268.0f, 120.0f, 36.0f},
    };
    for (size_t i = 0; i < widget->child_count &&
                       i < sizeof(button_bounds) / sizeof(button_bounds[0]);
         ++i) {
        widget->children[i]->bounds = button_bounds[i];
    }
}

static void showcase_draw(Widget *widget, struct VkContext *context)
{
    (void)context;
    ButtonShowcase *showcase = (ButtonShowcase *)widget;
    static const uint8_t background[4] = {250, 250, 252, 255};
    static const uint8_t heading[4] = {33, 37, 43, 255};
    static const uint8_t description[4] = {104, 109, 118, 255};
    vk_draw_rounded_rect(g_draw, widget->bounds.x, widget->bounds.y,
                         widget->bounds.w, widget->bounds.h,
                         0.0f, 0.5f, background);
    (void)vk_draw_text(g_draw, "Button styles and sizes",
                       widget->bounds.x + 36.0f, widget->bounds.y + 30.0f,
                       22.0f, heading);
    (void)vk_draw_text(g_draw,
                       "Text buttons, filled buttons, and icon buttons",
                       widget->bounds.x + 36.0f, widget->bounds.y + 68.0f,
                       14.0f, description);
    (void)vk_draw_text(g_draw, "Small                 Medium"
                       "                   Large"
                       "                    Pill",
                       widget->bounds.x + 36.0f, widget->bounds.y + 112.0f,
                       12.0f, description);
    (void)vk_draw_text(g_draw, "Icon buttons                    Large text button",
                       widget->bounds.x + 36.0f, widget->bounds.y + 228.0f,
                       12.0f, description);
    char click_text[64];
    snprintf(click_text, sizeof(click_text), "Button clicks: %zu",
             showcase->clicks);
    (void)vk_draw_text(g_draw, click_text, widget->bounds.x + 36.0f,
                       widget->bounds.y + 340.0f, 13.0f, description);
    for (size_t i = 0; i < widget->child_count; ++i) {
        if (widget->children[i]->draw) {
            widget->children[i]->draw(widget->children[i], context);
        }
    }
}

static void showcase_event(Widget *widget, const void *event)
{
    for (size_t i = 0; i < widget->child_count; ++i) {
        if (widget->children[i]->handle_event) {
            widget->children[i]->handle_event(widget->children[i], event);
        }
    }
}

static Button *make_button(ButtonShowcase *showcase, Rect bounds,
                           const char *label, Style style,
                           const uint8_t hover[4], const uint8_t pressed[4])
{
    Button *button = button_create(bounds, style, label);
    if (!button) return NULL;
    button_set_interaction_colors(button, hover, pressed);
    button_set_click_handler(button, count_click, showcase);
    if (!widget_add_child(&showcase->base, (Widget *)button)) {
        widget_destroy((Widget *)button);
        return NULL;
    }
    return button;
}

static bool create_showcase(ButtonShowcase **out, const VkDrawIcon *file_icon,
                            const VkDrawIcon *close_icon,
                            float width, float height)
{
    ButtonShowcase *showcase = calloc(1, sizeof(*showcase));
    if (!showcase) return false;
    showcase->base.bounds = (Rect){0.0f, 0.0f, width, height};
    showcase->base.style = style_default();
    showcase->base.draw = showcase_draw;
    showcase->base.handle_event = showcase_event;
    showcase->base.destroy = widget_destroy_noop;
    showcase->base.layout = showcase_layout;
    static const uint8_t clear[4] = {0, 0, 0, 0};
    static const uint8_t text[4] = {32, 33, 36, 255};
    static const uint8_t primary[4] = {36, 99, 235, 255};
    static const uint8_t primary_hover[4] = {29, 78, 216, 255};
    static const uint8_t primary_pressed[4] = {30, 64, 175, 255};
    static const uint8_t secondary[4] = {232, 234, 238, 255};
    static const uint8_t secondary_hover[4] = {218, 221, 226, 255};
    static const uint8_t secondary_pressed[4] = {203, 207, 214, 255};
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t transparent_hover[4] = {232, 234, 238, 255};
    static const uint8_t transparent_pressed[4] = {218, 221, 226, 255};

    Style text_style = make_style(clear, text, 6.0f);
    Style secondary_style = make_style(secondary, text, 7.0f);
    Style primary_style = make_style(primary, white, 7.0f);
    Style pill_style = make_style(secondary, text, 18.0f);
    if (!make_button(showcase, (Rect){0}, "Text", text_style,
                     transparent_hover, transparent_pressed) ||
        !make_button(showcase, (Rect){0}, "Secondary", secondary_style,
                     secondary_hover, secondary_pressed) ||
        !make_button(showcase, (Rect){0}, "Primary", primary_style,
                     primary_hover, primary_pressed) ||
        !make_button(showcase, (Rect){0}, "Pill", pill_style,
                     secondary_hover, secondary_pressed)) {
        widget_destroy((Widget *)showcase);
        return false;
    }
    Button *file_button = button_create_icon(
        (Rect){0}, make_style(clear, text, 8.0f), file_icon);
    Button *close_button = button_create_icon(
        (Rect){0}, make_style(clear, text, 22.0f), close_icon);
    if (!file_button || !close_button) {
        widget_destroy((Widget *)file_button);
        widget_destroy((Widget *)close_button);
        widget_destroy((Widget *)showcase);
        return false;
    }
    button_set_icon_size(file_button, 18.0f);
    button_set_icon_size(close_button, 20.0f);
    button_set_interaction_colors(file_button, transparent_hover,
                                  transparent_pressed);
    button_set_interaction_colors(close_button, transparent_hover,
                                  transparent_pressed);
    button_set_click_handler(file_button, count_click, showcase);
    button_set_click_handler(close_button, count_click, showcase);
    if (!widget_add_child(&showcase->base, (Widget *)file_button)) {
        widget_destroy((Widget *)file_button);
        widget_destroy((Widget *)close_button);
        widget_destroy((Widget *)showcase);
        return false;
    }
    if (!widget_add_child(&showcase->base, (Widget *)close_button)) {
        widget_destroy((Widget *)close_button);
        widget_destroy((Widget *)showcase);
        return false;
    }
    if (!make_button(showcase, (Rect){0}, "Save file",
                     make_style(primary, white, 7.0f),
                     primary_hover, primary_pressed)) {
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
        "cgui - Buttons", 800, 440,
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
    ButtonShowcase *showcase = NULL;

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
    for (size_t i = 0; i < 9; ++i) {
        free(words[i]);
        words[i] = NULL;
    }
    VkDrawIcon *file_icon =
        vk_draw_load_icon(&draw, "assets/icons/file-text.svg", 24);
    VkDrawIcon *close_icon =
        vk_draw_load_icon(&draw, "assets/icons/lucide-x.svg", 24);
    if (!file_icon || !close_icon ||
        !create_showcase(&showcase, file_icon, close_icon,
                         (float)context.swapchain_extent.width,
                         (float)context.swapchain_extent.height)) {
        fprintf(stderr, "Failed to construct Button showcase\n");
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
    for (size_t i = 0; i < 9; ++i) free(words[i]);
    widget_destroy((Widget *)showcase);
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
