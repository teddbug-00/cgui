/**
 * main.c – SDL3 + Vulkan UI demo (Panel + Button)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "gui.h"
#include "text.h"
#include "panel.h"
#include "button.h"
#include "label.h"
#include "toggle.h"
#include "vk_context.h"
#include "vk_draw.h"
#include "spv_loader.h"

typedef struct App {
    SDL_Window *window;
    VkContext   vk;
    VkDraw      draw;
    Widget     *root;
    bool        running;
    bool        framebuffer_resized;
} App;

static void root_draw(Widget *self, struct VkContext *vk)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) {
            child->draw(child, vk);
        }
    }
}

static void root_event(Widget *self, const void *ev)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->handle_event) {
            child->handle_event(child, ev);
        }
    }
}

static void on_button_click(Button *btn, void *userdata)
{
    (void)btn;
    (void)userdata;
    printf("Button clicked!\n");
}

static void on_toggle_change(Toggle *self, bool is_on, void *userdata)
{
    (void)self;
    (void)userdata;
    printf("Toggle switched: %s\n", is_on ? "ON" : "OFF");
}

static bool load_spv_candidates(const char **paths, uint32_t **words, size_t *count)
{
    for (int i = 0; paths[i]; ++i) {
        if (spv_load_file(paths[i], words, count)) {
            printf("Loaded %s\n", paths[i]);
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "cgui",
        1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    App app = {0};
    app.window  = window;
    app.running = true;

    if (!vk_context_create(&app.vk, window)) {
        fprintf(stderr, "Vulkan context failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    VkDrawShaders shaders = {0};
    const char *rr_v[] = { "build/rounded_rect.vert.spv", "rounded_rect.vert.spv", NULL };
    const char *rr_f[] = { "build/rounded_rect.frag.spv", "rounded_rect.frag.spv", NULL };
    const char *bl_v[] = { "build/blur.vert.spv", "blur.vert.spv", NULL };
    const char *bl_f[] = { "build/blur.frag.spv", "blur.frag.spv", NULL };
    const char *bt_v[] = { "build/blit.vert.spv", "blit.vert.spv", NULL };
    const char *bt_f[] = { "build/blit.frag.spv", "blit.frag.spv", NULL };

    uint32_t *w1=NULL,*w2=NULL,*w3=NULL,*w4=NULL,*w5=NULL,*w6=NULL;
    size_t n1=0,n2=0,n3=0,n4=0,n5=0,n6=0;
    if (!load_spv_candidates(rr_v, &w1, &n1) ||
        !load_spv_candidates(rr_f, &w2, &n2) ||
        !load_spv_candidates(bl_v, &w3, &n3) ||
        !load_spv_candidates(bl_f, &w4, &n4) ||
        !load_spv_candidates(bt_v, &w5, &n5) ||
        !load_spv_candidates(bt_f, &w6, &n6)) {
        fprintf(stderr, "Missing SPIR-V. Run: meson compile -C build\n");
        return 1;
    }
    shaders.rr_vert = w1; shaders.rr_vert_n = n1;
    shaders.rr_frag = w2; shaders.rr_frag_n = n2;
    shaders.blur_vert = w3; shaders.blur_vert_n = n3;
    shaders.blur_frag = w4; shaders.blur_frag_n = n4;
    shaders.blit_vert = w5; shaders.blit_vert_n = n5;
    shaders.blit_frag = w6; shaders.blit_frag_n = n6;

    if (!vk_draw_create(&app.draw, &app.vk, &shaders)) {
        fprintf(stderr, "UI draw pipeline failed\n");
        return 1;
    }
    free(w1); free(w2); free(w3); free(w4); free(w5); free(w6);

    panel_set_draw_context(&app.draw);
    button_set_draw_context(&app.draw);
    label_set_draw_context(&app.draw);
    toggle_set_draw_context(&app.draw);

    app.root = widget_create();
    if (!app.root) return 1;
    app.root->draw = root_draw;
    app.root->handle_event = root_event;

    /* ---- Panel card ---- */
    Style card_style = style_default();
    card_style.corner_radius   = 16.0f;
    card_style.shadow_offset_y = 10.0f;
    card_style.shadow_blur     = 28.0f;
    card_style.shadow_color[3] = 55;

    Rect card_box = { 440.0f, 200.0f, 400.0f, 360.0f };
    Panel *card = panel_create(card_box, card_style);
    if (card) {
        widget_add_child(app.root, (Widget *)card);
    }

    /* ---- Card Title (Bold Header) ---- */
    Style title_style = style_default();
    title_style.text_size     = 22.0f;
    title_style.text_style    = CGUI_TEXT_BOLD;
    title_style.text_color[0] = 28; title_style.text_color[1] = 28;
    title_style.text_color[2] = 30; title_style.text_color[3] = 255;
    title_style.bg[3]         = 0;
    title_style.border_width  = 0.0f;
    title_style.shadow_color[3] = 0;

    Rect title_box = { 470.0f, 235.0f, 340.0f, 32.0f };
    Label *title = label_create(title_box, title_style, "macOS Design");
    if (title) {
        widget_add_child(app.root, (Widget *)title);
    }

    /* ---- Card Description (Muted Subheading) ---- */
    Style desc_style = style_default();
    desc_style.text_size     = 14.0f;
    desc_style.text_style    = CGUI_TEXT_REGULAR;
    desc_style.text_color[0] = 120; desc_style.text_color[1] = 120;
    desc_style.text_color[2] = 128; desc_style.text_color[3] = 255;
    desc_style.bg[3]         = 0;
    desc_style.border_width  = 0.0f;
    desc_style.shadow_color[3] = 0;

    Rect desc_box = { 470.0f, 270.0f, 340.0f, 24.0f };
    Label *desc = label_create(desc_box, desc_style, "Clean Vulkan UI with crisp typography");
    if (desc) {
        widget_add_child(app.root, (Widget *)desc);
    }

    /* ---- Toggle Row (Option label + macOS switch) ---- */
    Style toggle_lbl_style = style_default();
    toggle_lbl_style.text_size     = 15.0f;
    toggle_lbl_style.text_style    = CGUI_TEXT_REGULAR;
    toggle_lbl_style.text_color[0] = 40; toggle_lbl_style.text_color[1] = 40;
    toggle_lbl_style.text_color[2] = 45; toggle_lbl_style.text_color[3] = 255;
    toggle_lbl_style.bg[3]         = 0;
    toggle_lbl_style.border_width  = 0.0f;
    toggle_lbl_style.shadow_color[3] = 0;

    Rect toggle_lbl_box = { 470.0f, 340.0f, 260.0f, 31.0f };
    Label *toggle_lbl = label_create(toggle_lbl_box, toggle_lbl_style, "Hardware Acceleration");
    if (toggle_lbl) {
        widget_add_child(app.root, (Widget *)toggle_lbl);
    }

    Rect toggle_box = { 759.0f, 340.0f, 51.0f, 31.0f };
    Toggle *toggle = toggle_create(toggle_box, style_default(), true);
    if (toggle) {
        toggle_set_change_handler(toggle, on_toggle_change, NULL);
        widget_add_child(app.root, (Widget *)toggle);
    }

    /* ---- Button on the card ---- */
    Style btn_style = style_default();
    btn_style.corner_radius   = 10.0f;
    btn_style.bg[0] = 0; btn_style.bg[1] = 122; btn_style.bg[2] = 255; btn_style.bg[3] = 255;
    btn_style.text_color[0] = 255; btn_style.text_color[1] = 255;
    btn_style.text_color[2] = 255; btn_style.text_color[3] = 255;
    btn_style.border_width    = 0.0f;
    btn_style.shadow_offset_y = 2.0f;
    btn_style.shadow_blur     = 8.0f;
    btn_style.shadow_color[3] = 40;

    /* Position relative to window (card is at 440,200 – place button inside) */
    Rect btn_box = { 470.0f, 440.0f, 340.0f, 44.0f };
    Button *btn = button_create(btn_box, btn_style, "Click me");
    if (btn) {
        uint8_t hover[4] = { 10, 132, 255, 255 };
        uint8_t press[4] = { 0, 100, 220, 255 };
        button_set_interaction_colors(btn, hover, press);
        button_set_click_handler(btn, on_button_click, NULL);
        widget_add_child(app.root, (Widget *)btn);
    }

    printf("cgui running – interactive switch and button active\n");

    Uint64 last_time = SDL_GetTicksNS();

    while (app.running) {
        Uint64 now = SDL_GetTicksNS();
        float dt = (float)(now - last_time) * 1e-9f;
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        if (toggle) {
            toggle_update(toggle, dt);
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                app.running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                app.framebuffer_resized = true;
                break;
            default:
                if (app.root && app.root->handle_event)
                    app.root->handle_event(app.root, &ev);
                break;
            }
        }

        if (app.framebuffer_resized) {
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (w > 0 && h > 0) {
                vk_context_recreate_swapchain(&app.vk);
                vk_draw_resize(&app.draw);
            }
            app.framebuffer_resized = false;
        }

        if (!vk_context_begin_frame(&app.vk)) {
            app.framebuffer_resized = true;
            continue;
        }

        if (app.root && app.root->draw)
            app.root->draw(app.root, &app.vk);

        if (!vk_context_end_frame(&app.vk))
            app.framebuffer_resized = true;
    }

    widget_destroy(app.root);
    vk_draw_destroy(&app.draw);
    vk_context_destroy(&app.vk);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
