/**
 * text_demo.c – Text styles, sizes, and font-family showcase.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <SDL3/SDL.h>

#include "text.h"
#include "vk_context.h"
#include "vk_draw.h"
#include "spv_loader.h"

typedef struct TextSample {
    const char *description;
    const char *text;
    float size;
    uint32_t style;
} TextSample;

static const char *const families[] = {
    "sans-serif", "serif", "monospace",
};

static const char *const family_labels[] = {
    "Sans serif", "Serif", "Monospace",
};

static const TextSample samples[] = {
    { "Regular / 14 px", "AaBb 0123456789", 14.0f, CGUI_TEXT_REGULAR },
    { "Regular / 18 px", "AaBb 0123456789", 18.0f, CGUI_TEXT_REGULAR },
    { "Regular / 24 px", "AaBb 0123456789", 24.0f, CGUI_TEXT_REGULAR },
    { "Regular / 32 px", "AaBb 0123456789", 32.0f, CGUI_TEXT_REGULAR },
    { "Bold / 20 px", "AaBb 0123456789", 20.0f, CGUI_TEXT_BOLD },
    { "Italic / 20 px", "AaBb 0123456789", 20.0f, CGUI_TEXT_ITALIC },
    { "Bold italic / 20 px", "AaBb 0123456789", 20.0f, CGUI_TEXT_BOLD_ITALIC },
};

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

static void draw_showcase(VkDraw *draw, VkDrawFont *fonts[3])
{
    static const uint8_t heading_color[4] = { 44, 49, 58, 255 };
    static const uint8_t body_color[4] = { 55, 59, 67, 255 };
    const float label_x = 28.0f;
    const float column_x[] = { 235.0f, 590.0f, 945.0f };
    const float row_start = 166.0f;
    const float row_height = 88.0f;

    (void)vk_draw_text(draw, "Text: family, size, and style",
                       label_x, 30.0f, 24.0f, heading_color);
    (void)vk_draw_text(draw,
        "Font family is selected per text run; sizes are rasterized at native resolution.",
        label_x, 68.0f, 14.0f, body_color);

    for (size_t column = 0; column < 3; ++column) {
        (void)vk_draw_text(draw, family_labels[column], column_x[column],
                           119.0f, 18.0f, heading_color);
    }

    for (size_t row = 0; row < sizeof(samples) / sizeof(samples[0]); ++row) {
        float y = row_start + row * row_height;
        (void)vk_draw_text(draw, samples[row].description, label_x, y + 5.0f,
                           13.0f, body_color);
        for (size_t column = 0; column < 3; ++column) {
            (void)vk_draw_text_font_styled(
                draw, fonts[column], samples[row].text,
                column_x[column], y, samples[row].size,
                samples[row].style, body_color);
        }
    }
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    SDL_Window *window = SDL_CreateWindow(
        "cgui - Text styles and fonts", 1280, 800,
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
    VkDrawFont *fonts[3] = {0};
    bool context_created = false;
    bool draw_created = false;
    bool running = true;
    bool resized = false;
    int result = EXIT_FAILURE;

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
    for (size_t i = 0; i < 9; ++i) {
        free(shader_words[i]);
        shader_words[i] = NULL;
    }

    for (size_t i = 0; i < 3; ++i) {
        fonts[i] = vk_draw_load_font(&draw, families[i]);
        if (!fonts[i]) goto cleanup;
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
            if (width > 0 && height > 0 &&
                (!vk_context_recreate_swapchain(&context) ||
                 !vk_draw_resize(&draw))) {
                result = EXIT_FAILURE;
                break;
            }
            resized = false;
        }
        if (!vk_context_begin_frame(&context)) {
            resized = true;
            continue;
        }
        draw_showcase(&draw, fonts);
        if (!vk_context_end_frame(&context)) resized = true;
    }

cleanup:
    for (size_t i = 0; i < 9; ++i) free(shader_words[i]);
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
