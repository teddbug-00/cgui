/**
 * main.c – SVG icon rendering showcase
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "vk_context.h"
#include "vk_draw.h"
#include "spv_loader.h"

typedef struct IconSample {
    const char *name;
    const char *file;
} IconSample;

static const IconSample icon_samples[] = {
    { "Star", "star.svg" },
    { "Clock", "clock-3.svg" },
    { "House", "house.svg" },
    { "Tag", "tag.svg" },
    { "Folder", "folder.svg" },
    { "File text", "file-text.svg" },
    { "Download", "download.svg" },
    { "Computer", "computer.svg" },
    { "Network", "network.svg" },
};

static const uint32_t icon_sizes[] = { 16, 24, 32, 48, 64 };

static bool load_spv_candidates(const char **paths, uint32_t **words,
                                size_t *count)
{
    for (int i = 0; paths[i]; ++i) {
        if (spv_load_file(paths[i], words, count)) {
            printf("Loaded %s\n", paths[i]);
            return true;
        }
    }
    return false;
}

static bool resolve_icon_path(const char *filename, char *path, size_t size)
{
    int written = snprintf(path, size, "assets/icons/%s", filename);
    if (written < 0 || (size_t)written >= size) return false;

    FILE *file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return true;
    }

    written = snprintf(path, size, "../assets/icons/%s", filename);
    if (written < 0 || (size_t)written >= size) return false;
    file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

static void draw_icon_gallery(VkDraw *draw, VkDrawIcon *icons[][5])
{
    static const uint8_t label_color[4] = { 48, 50, 56, 255 };
    static const uint8_t icon_tint[4] = { 47, 105, 176, 255 };
    const float label_x = 48.0f;
    const float columns_x = 200.0f;
    const float column_width = 205.0f;
    const float header_y = 35.0f;
    const float row_y = 108.0f;
    const float row_height = 72.0f;

    (void)vk_draw_text(draw, "SVG icons at native raster sizes",
                       label_x, header_y, 22.0f, label_color);
    for (size_t column = 0;
         column < sizeof(icon_sizes) / sizeof(icon_sizes[0]); ++column) {
        char size_label[16];
        snprintf(size_label, sizeof(size_label), "%u px", icon_sizes[column]);
        float center_x = columns_x + ((float)column + 0.5f) * column_width;
        (void)vk_draw_text(draw, size_label, center_x - 20.0f, 76.0f,
                           14.0f, label_color);
    }

    for (size_t row = 0;
         row < sizeof(icon_samples) / sizeof(icon_samples[0]); ++row) {
        float center_y = row_y + ((float)row + 0.5f) * row_height;
        (void)vk_draw_text(draw, icon_samples[row].name,
                           label_x, center_y - 8.0f, 15.0f, label_color);

        for (size_t column = 0;
             column < sizeof(icon_sizes) / sizeof(icon_sizes[0]); ++column) {
            float icon_size = (float)icon_sizes[column];
            float center_x = columns_x + ((float)column + 0.5f) * column_width;
            vk_draw_icon(draw, icons[row][column],
                         center_x - icon_size * 0.5f,
                         center_y - icon_size * 0.5f,
                         icon_size, icon_size, icon_tint);
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
        "cgui - SVG icon sizes", 1280, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    VkContext context = {0};
    VkDraw draw = {0};
    bool context_created = false;
    bool draw_created = false;
    bool running = true;
    bool framebuffer_resized = false;
    uint32_t *shader_words[9] = {0};
    size_t shader_sizes[9] = {0};
    VkDrawIcon *icons[sizeof(icon_samples) / sizeof(icon_samples[0])]
                     [sizeof(icon_sizes) / sizeof(icon_sizes[0])] = {0};
    int result = EXIT_FAILURE;

    if (!vk_context_create(&context, window)) {
        fprintf(stderr, "Vulkan context failed\n");
        goto cleanup;
    }
    context_created = true;

    VkDrawShaders shaders = {0};
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
    const char **shader_paths[] = {
        rr_v, rr_f, bl_v, bl_f, bt_v, bt_f, ic_f, tx_v, tx_f
    };
    for (size_t i = 0; i < 9; ++i) {
        if (!load_spv_candidates(shader_paths[i], &shader_words[i],
                                 &shader_sizes[i])) {
            fprintf(stderr, "Missing SPIR-V. Run: meson compile -C build\n");
            goto cleanup;
        }
    }

    shaders.rr_vert = shader_words[0];
    shaders.rr_vert_n = shader_sizes[0];
    shaders.rr_frag = shader_words[1];
    shaders.rr_frag_n = shader_sizes[1];
    shaders.blur_vert = shader_words[2];
    shaders.blur_vert_n = shader_sizes[2];
    shaders.blur_frag = shader_words[3];
    shaders.blur_frag_n = shader_sizes[3];
    shaders.blit_vert = shader_words[4];
    shaders.blit_vert_n = shader_sizes[4];
    shaders.blit_frag = shader_words[5];
    shaders.blit_frag_n = shader_sizes[5];
    shaders.icon_frag = shader_words[6];
    shaders.icon_frag_n = shader_sizes[6];
    shaders.txt_vert = shader_words[7];
    shaders.txt_vert_n = shader_sizes[7];
    shaders.txt_frag = shader_words[8];
    shaders.txt_frag_n = shader_sizes[8];

    if (!vk_draw_create(&draw, &context, &shaders)) {
        fprintf(stderr, "UI draw pipeline failed\n");
        goto cleanup;
    }
    draw_created = true;
    for (size_t i = 0; i < 9; ++i) {
        free(shader_words[i]);
        shader_words[i] = NULL;
    }

    for (size_t row = 0;
         row < sizeof(icon_samples) / sizeof(icon_samples[0]); ++row) {
        char path[256];
        if (!resolve_icon_path(icon_samples[row].file, path, sizeof(path))) {
            fprintf(stderr, "Cannot find icon asset: %s\n",
                    icon_samples[row].file);
            goto cleanup;
        }
        for (size_t column = 0;
             column < sizeof(icon_sizes) / sizeof(icon_sizes[0]); ++column) {
            icons[row][column] =
                vk_draw_load_icon(&draw, path, icon_sizes[column]);
            if (!icons[row][column]) goto cleanup;
        }
    }

    printf("Rendering SVG icons at 16, 24, 32, 48, and 64 pixels\n");
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
                framebuffer_resized = true;
                break;
            default:
                break;
            }
        }

        if (framebuffer_resized) {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            if (width > 0 && height > 0) {
                if (!vk_context_recreate_swapchain(&context) ||
                    !vk_draw_resize(&draw)) {
                    result = EXIT_FAILURE;
                    break;
                }
            }
            framebuffer_resized = false;
        }

        if (!vk_context_begin_frame(&context)) {
            framebuffer_resized = true;
            continue;
        }
        draw_icon_gallery(&draw, icons);
        if (!vk_context_end_frame(&context)) framebuffer_resized = true;
    }

cleanup:
    for (size_t i = 0; i < 9; ++i) free(shader_words[i]);
    if (draw_created) vk_draw_destroy(&draw);
    if (context_created) vk_context_destroy(&context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
