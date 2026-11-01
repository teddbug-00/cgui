#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>

#include "spv_loader.h"
#include "style.h"
#include "vk_context.h"
#include "vk_draw.h"

#define WARMUP_FRAMES 15
#define DEFAULT_FRAMES 120
#define MAX_FRAMES 2000
#define WORKLOAD_COUNT 8

typedef struct Sample {
    double record_ms;
    double frame_ms;
} Sample;

typedef enum WorkloadKind {
    WORKLOAD_RECTS,
    WORKLOAD_PARAGRAPH,
    WORKLOAD_LABELS,
    WORKLOAD_SHADOW_1,
    WORKLOAD_SHADOW_10,
    WORKLOAD_SHADOW_50,
    WORKLOAD_MIXED_UI,
    WORKLOAD_ICONS,
} WorkloadKind;

typedef struct Workload {
    const char *name;
    WorkloadKind kind;
} Workload;

static const Workload workloads[WORKLOAD_COUNT] = {
    { "Rounded rectangles (1,000/frame)", WORKLOAD_RECTS },
    { "Text paragraph (one draw)", WORKLOAD_PARAGRAPH },
    { "Text labels (100/frame)", WORKLOAD_LABELS },
    { "Gaussian shadows (1/frame)", WORKLOAD_SHADOW_1 },
    { "Gaussian shadows (10/frame)", WORKLOAD_SHADOW_10 },
    { "Gaussian shadows (50/frame)", WORKLOAD_SHADOW_50 },
    { "Mixed UI (25 shadows + 60 labels)", WORKLOAD_MIXED_UI },
    { "SVG icons (100/frame)", WORKLOAD_ICONS },
};

static VkDrawIcon *bench_icons[9];

static const char *paragraph =
    "The quick brown fox jumps over the lazy dog. "
    "Vulkan UI rendering combines text shaping, glyph atlas sampling, "
    "rounded geometry, and soft Gaussian shadows in a deterministic frame.";

static uint64_t time_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static bool load_shader(const char *name, uint32_t **words, size_t *word_count)
{
    const char *roots[] = { "build", "build-release", ".", "../build",
                            "../build-release", NULL };
    char path[256];
    for (size_t i = 0; roots[i]; ++i) {
        int written = snprintf(path, sizeof(path), "%s/%s.spv", roots[i], name);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            continue;
        }
        if (spv_load_file(path, words, word_count)) {
            return true;
        }
    }
    fprintf(stderr, "Unable to load shader %s.spv; build the project first\n", name);
    return false;
}

static bool load_draw_shaders(VkDrawShaders *shaders, uint32_t *storage[9],
                              size_t sizes[9])
{
    const char *names[] = {
        "rounded_rect.vert", "rounded_rect.frag",
        "blur.vert", "blur.frag",
        "blit.vert", "blit.frag", "icon.frag",
        "text.vert", "text.frag",
    };
    for (size_t i = 0; i < 9; ++i) {
        if (!load_shader(names[i], &storage[i], &sizes[i])) {
            for (size_t j = 0; j < i; ++j) {
                free(storage[j]);
                storage[j] = NULL;
            }
            return false;
        }
    }

    *shaders = (VkDrawShaders) {
        .rr_vert = storage[0], .rr_vert_n = sizes[0],
        .rr_frag = storage[1], .rr_frag_n = sizes[1],
        .blur_vert = storage[2], .blur_vert_n = sizes[2],
        .blit_vert = storage[4], .blit_vert_n = sizes[4],
        .blur_frag = storage[3], .blur_frag_n = sizes[3],
        .blit_frag = storage[5], .blit_frag_n = sizes[5],
        .icon_frag = storage[6], .icon_frag_n = sizes[6],
        .txt_vert = storage[7], .txt_vert_n = sizes[7],
        .txt_frag = storage[8], .txt_frag_n = sizes[8],
    };
    return true;
}

static void draw_shadow_grid(VkDraw *draw, uint32_t count)
{
    Style style = style_default();
    style.corner_radius = 8.0f;
    style.shadow_blur = 14.0f;
    style.shadow_offset_y = 4.0f;
    style.shadow_color[3] = 90;

    const uint32_t columns = 10;
    for (uint32_t i = 0; i < count; ++i) {
        float x = 20.0f + (float)(i % columns) * 112.0f;
        float y = 20.0f + (float)(i / columns) * 84.0f;
        vk_draw_style_box(draw, x, y, 92.0f, 60.0f, &style);
    }
}

static bool record_workload(VkDraw *draw, WorkloadKind kind)
{
    static const uint8_t color[] = { 52, 112, 190, 255 };
    switch (kind) {
    case WORKLOAD_RECTS:
        for (uint32_t i = 0; i < 1000; ++i) {
            float x = (float)((i * 37U) % 1200U);
            float y = (float)((i * 61U) % 720U);
            vk_draw_rounded_rect(draw, x, y, 34.0f, 20.0f, 4.0f, 1.0f, color);
        }
        break;
    case WORKLOAD_PARAGRAPH:
        return vk_draw_text(draw, paragraph, 24.0f, 24.0f, 18.0f, color);
    case WORKLOAD_LABELS:
        for (uint32_t i = 0; i < 100; ++i) {
            float x = 20.0f + (float)(i % 10U) * 120.0f;
            float y = 20.0f + (float)(i / 10U) * 64.0f;
            if (!vk_draw_text(draw, "Settings and controls", x, y, 15.0f, color)) {
                return false;
            }
        }
        break;
    case WORKLOAD_SHADOW_1:
        draw_shadow_grid(draw, 1);
        break;
    case WORKLOAD_SHADOW_10:
        draw_shadow_grid(draw, 10);
        break;
    case WORKLOAD_SHADOW_50:
        draw_shadow_grid(draw, 50);
        break;
    case WORKLOAD_MIXED_UI:
        draw_shadow_grid(draw, 25);
        for (uint32_t i = 0; i < 60; ++i) {
            float x = 24.0f + (float)(i % 10U) * 120.0f;
            float y = 24.0f + (float)(i / 10U) * 80.0f;
            if (!vk_draw_text(draw, "System preferences", x, y, 15.0f, color)) {
                return false;
            }
        }
        break;
    case WORKLOAD_ICONS:
        for (uint32_t i = 0; i < 100; ++i) {
            float x = 20.0f + (float)(i % 10U) * 112.0f;
            float y = 20.0f + (float)(i / 10U) * 64.0f;
            vk_draw_icon(draw, bench_icons[i % 9U], x, y, 24.0f, 24.0f,
                         (const uint8_t[]){ 65, 110, 180, 255 });
        }
        break;
    }
    return true;
}

static VkQueryPool create_timestamp_pool(VkContext *ctx, uint32_t query_count)
{
    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = query_count,
    };
    VkQueryPool pool = VK_NULL_HANDLE;
    if (vkCreateQueryPool(ctx->device, &info, NULL, &pool) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan timestamp query pool\n");
    }
    return pool;
}

static bool reset_timestamp_pool(VkContext *ctx, VkQueryPool pool,
                                 uint32_t query_count)
{
    VkCommandBuffer cmd = vk_context_begin_one_shot(ctx);
    if (cmd == VK_NULL_HANDLE) return false;
    vkCmdResetQueryPool(cmd, pool, 0, query_count);
    return vk_context_end_one_shot(ctx, cmd);
}

static void draw_frame(VkContext *ctx, VkDraw *draw, WorkloadKind kind,
                       VkQueryPool pool, uint32_t query_index,
                       bool timestamp_supported, Sample *sample)
{
    uint64_t frame_start = time_ns();
    if (!vk_context_begin_frame(ctx)) {
        fprintf(stderr, "Failed to acquire Vulkan frame\n");
        exit(EXIT_FAILURE);
    }

    uint64_t record_start = time_ns();
    if (timestamp_supported) {
        vkCmdWriteTimestamp(ctx->active_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            pool, query_index);
    }
    bool batch_started = vk_draw_begin_shadow_batch(draw);
    bool collected = batch_started && record_workload(draw, kind);
    if (batch_started && !collected) {
        fprintf(stderr, "Text rendering failed during benchmark workload\n");
        exit(EXIT_FAILURE);
    }
    if (collected) (void)vk_draw_prepare_shadow_batch(draw);
    if (!record_workload(draw, kind)) {
        fprintf(stderr, "Text rendering failed during benchmark workload\n");
        exit(EXIT_FAILURE);
    }
    if (timestamp_supported) {
        vkCmdWriteTimestamp(ctx->active_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                            pool, query_index + 1U);
    }
    if (batch_started) vk_draw_end_shadow_batch(draw);
    uint64_t record_end = time_ns();

    if (!vk_context_end_frame(ctx)) {
        fprintf(stderr, "Failed to submit or present Vulkan frame\n");
        exit(EXIT_FAILURE);
    }

    uint64_t frame_end = time_ns();
    if (sample) {
        sample->record_ms = (double)(record_end - record_start) / 1e6;
        sample->frame_ms = (double)(frame_end - frame_start) / 1e6;
    }
}

static int compare_double(const void *a, const void *b)
{
    double left = *(const double *)a;
    double right = *(const double *)b;
    return (left > right) - (left < right);
}

static void print_distribution(const char *label, double *values, uint32_t count)
{
    qsort(values, count, sizeof(*values), compare_double);
    double sum = 0.0;
    for (uint32_t i = 0; i < count; ++i) sum += values[i];
    uint32_t p50 = (count - 1U) / 2U;
    uint32_t p95 = (uint32_t)ceil((double)count * 0.95) - 1U;
    printf("    %-18s avg %8.3f ms | p50 %8.3f ms | p95 %8.3f ms\n",
           label, sum / (double)count, values[p50], values[p95]);
}

static bool get_timestamp_valid_bits(VkContext *ctx, uint32_t *valid_bits)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx->physical_device, &count, families);
    bool supported = ctx->graphics_family < count &&
                     families[ctx->graphics_family].timestampValidBits > 0;
    if (supported) *valid_bits = families[ctx->graphics_family].timestampValidBits;
    free(families);
    return supported;
}

static bool parse_frame_count(int argc, char **argv, uint32_t *frames)
{
    *frames = DEFAULT_FRAMES;
    if (argc == 1) return true;
    if (argc != 3 || strcmp(argv[1], "--frames") != 0) {
        fprintf(stderr, "Usage: %s [--frames N]\n", argv[0]);
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(argv[2], &end, 10);
    if (!argv[2][0] || !end || *end || value < 10 || value > MAX_FRAMES) {
        fprintf(stderr, "--frames must be between 10 and %d\n", MAX_FRAMES);
        return false;
    }
    *frames = (uint32_t)value;
    return true;
}

int main(int argc, char **argv)
{
    uint32_t frames = 0;
    if (!parse_frame_count(argc, argv, &frames)) return EXIT_FAILURE;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Window *window = SDL_CreateWindow("cgui Vulkan benchmark",
                                           1280, 800, SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    VkContext ctx = {0};
    VkDraw draw = {0};
    VkQueryPool query_pool = VK_NULL_HANDLE;
    Sample *samples = NULL;
    uint64_t *query_results = NULL;
    bool ctx_created = false;
    bool draw_created = false;
    int status = EXIT_FAILURE;
    uint32_t query_valid_bits = 0;
    bool timestamp_supported = false;
    uint32_t query_count = WORKLOAD_COUNT * frames * 2U;
    uint32_t *shader_words[9] = {0};
    size_t shader_sizes[9] = {0};
    VkDrawShaders shaders = {0};

    if (!vk_context_create(&ctx, window)) {
        fprintf(stderr, "Failed to initialize Vulkan context for rendering benchmark\n");
        goto cleanup;
    }
    ctx_created = true;

    if (!load_draw_shaders(&shaders, shader_words, shader_sizes)) goto cleanup;
    if (!vk_draw_create(&draw, &ctx, &shaders)) {
        fprintf(stderr, "Failed to create Vulkan drawing pipelines\n");
        goto cleanup;
    }
    draw_created = true;
    for (size_t i = 0; i < 9; ++i) {
        free(shader_words[i]);
        shader_words[i] = NULL;
    }
    const char *icon_paths[9] = {
        "assets/icons/star.svg", "assets/icons/clock-3.svg",
        "assets/icons/house.svg", "assets/icons/tag.svg",
        "assets/icons/folder.svg", "assets/icons/file-text.svg",
        "assets/icons/download.svg", "assets/icons/computer.svg",
        "assets/icons/network.svg",
    };
    for (size_t i = 0; i < 9; ++i) {
        bench_icons[i] = vk_draw_load_icon(&draw, icon_paths[i], 24);
        if (!bench_icons[i]) goto cleanup;
    }

    timestamp_supported = get_timestamp_valid_bits(&ctx, &query_valid_bits);
    if (timestamp_supported) {
        query_pool = create_timestamp_pool(&ctx, query_count);
        if (query_pool == VK_NULL_HANDLE ||
            !reset_timestamp_pool(&ctx, query_pool, query_count)) {
            goto cleanup;
        }
    } else {
        fprintf(stderr, "GPU timestamp queries are unsupported; reporting CPU timings only\n");
    }

    samples = calloc((size_t)frames, sizeof(*samples));
    if (!samples) {
        fprintf(stderr, "Out of memory allocating benchmark samples\n");
        goto cleanup;
    }
    if (timestamp_supported) {
        query_results = calloc((size_t)query_count, sizeof(*query_results));
        if (!query_results) {
            fprintf(stderr, "Out of memory allocating timestamp results\n");
            goto cleanup;
        }
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(ctx.physical_device, &properties);
    printf("\nVulkan rendering benchmark: %s, %ux%u, %u measured frames/workload\n",
           properties.deviceName, ctx.swapchain_extent.width,
           ctx.swapchain_extent.height, frames);
    printf("Warmup: %d frames/workload. Frame time includes acquire/present; "
           "command time excludes them.\n", WARMUP_FRAMES);
    if (timestamp_supported) {
        printf("GPU time uses Vulkan timestamp queries (period %.3f ns).\n",
               (double)properties.limits.timestampPeriod);
    }
    printf("Keep the window visible and avoid moving/resizing it during measurement.\n");

    for (uint32_t w = 0; w < WORKLOAD_COUNT; ++w) {
        for (uint32_t i = 0; i < WARMUP_FRAMES; ++i) {
            draw_frame(&ctx, &draw, workloads[w].kind, query_pool, 0,
                       false, NULL);
        }

        for (uint32_t i = 0; i < frames; ++i) {
            uint32_t query_index = (w * frames + i) * 2U;
            draw_frame(&ctx, &draw, workloads[w].kind, query_pool, query_index,
                       timestamp_supported, &samples[i]);
        }

        printf("\n%s\n", workloads[w].name);
        double *record_times = malloc((size_t)frames * sizeof(*record_times));
        double *frame_times = malloc((size_t)frames * sizeof(*frame_times));
        if (!record_times || !frame_times) {
            free(record_times);
            free(frame_times);
            fprintf(stderr, "Out of memory sorting benchmark results\n");
            goto cleanup;
        }
        for (uint32_t i = 0; i < frames; ++i) {
            record_times[i] = samples[i].record_ms;
            frame_times[i] = samples[i].frame_ms;
        }
        print_distribution("CPU command record", record_times, frames);
        print_distribution("CPU frame wall time", frame_times, frames);
        free(record_times);
        free(frame_times);
    }

    if (timestamp_supported) {
        if (vkDeviceWaitIdle(ctx.device) != VK_SUCCESS) {
            fprintf(stderr, "Failed waiting for Vulkan benchmark work to complete\n");
            goto cleanup;
        }
        if (vkGetQueryPoolResults(ctx.device, query_pool, 0, query_count,
                                  (size_t)query_count * sizeof(*query_results),
                                  query_results, sizeof(*query_results),
                                  VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) {
            fprintf(stderr, "Failed to read Vulkan timestamp results\n");
            goto cleanup;
        }

        uint64_t timestamp_mask = query_valid_bits >= 64
            ? UINT64_MAX
            : (UINT64_C(1) << query_valid_bits) - 1U;
        for (uint32_t w = 0; w < WORKLOAD_COUNT; ++w) {
            double *gpu_times = malloc((size_t)frames * sizeof(*gpu_times));
            if (!gpu_times) {
                fprintf(stderr, "Out of memory sorting GPU results\n");
                goto cleanup;
            }
            for (uint32_t i = 0; i < frames; ++i) {
                uint32_t query_index = (w * frames + i) * 2U;
                uint64_t start = query_results[query_index];
                uint64_t end = query_results[query_index + 1U];
                uint64_t ticks = (end - start) & timestamp_mask;
                gpu_times[i] = (double)ticks *
                               (double)properties.limits.timestampPeriod / 1e6;
            }
            printf("\n%s GPU execution\n", workloads[w].name);
            print_distribution("GPU timestamp", gpu_times, frames);
            free(gpu_times);
        }
    }

    status = EXIT_SUCCESS;

cleanup:
    for (size_t i = 0; i < 9; ++i) free(shader_words[i]);
    free(query_results);
    free(samples);
    if (query_pool && ctx.device) vkDestroyQueryPool(ctx.device, query_pool, NULL);
    if (draw_created) vk_draw_destroy(&draw);
    if (ctx_created) vk_context_destroy(&ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
