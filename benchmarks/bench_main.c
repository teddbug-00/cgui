/**
 * bench_main.c – Performance benchmarking suite for cgui (Release mode)
 *
 * Measures:
 *   1. Text shaping & FreeType/HarfBuzz rasterization throughput
 *   2. Text styling variations (Regular, Bold, Italic)
 *   3. Widget hierarchy creation & destruction
 *   4. Event dispatch throughput across widgets
 *   5. Toggle animation update throughput (thousands of active switches)
 *   6. Layout hit-testing & point-in-rect math
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include <SDL3/SDL.h>

#include "gui.h"
#include "style.h"
#include "text.h"
#include "panel.h"
#include "button.h"
#include "label.h"
#include "toggle.h"
#include "slider.h"

/* ------------------------------------------------------------------ */
/* High-resolution timer helpers                                       */
/* ------------------------------------------------------------------ */

static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_bench_header(const char *category)
{
    printf("\n\033[1;36m=== %s ===\033[0m\n", category);
    printf("%-38s | %10s | %10s | %14s | %14s\n",
           "Benchmark", "Iters", "Total Time", "Latency / Op", "Throughput");
    printf("---------------------------------------+------------+------------+----------------+---------------\n");
}

static void print_bench_result(const char *name, uint64_t iters, uint64_t elapsed_ns)
{
    double total_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_op = (double)elapsed_ns / (double)iters;
    double ops_per_sec = (double)iters / ((double)elapsed_ns / 1000000000.0);

    char lat_str[32];
    if (ns_per_op < 1000.0) {
        snprintf(lat_str, sizeof(lat_str), "%6.1f ns", ns_per_op);
    } else if (ns_per_op < 1000000.0) {
        snprintf(lat_str, sizeof(lat_str), "%6.2f µs", ns_per_op / 1000.0);
    } else {
        snprintf(lat_str, sizeof(lat_str), "%6.2f ms", ns_per_op / 1000000.0);
    }

    char thr_str[32];
    if (ops_per_sec >= 1000000.0) {
        snprintf(thr_str, sizeof(thr_str), "%6.2f M ops/s", ops_per_sec / 1000000.0);
    } else if (ops_per_sec >= 1000.0) {
        snprintf(thr_str, sizeof(thr_str), "%6.1f k ops/s", ops_per_sec / 1000.0);
    } else {
        snprintf(thr_str, sizeof(thr_str), "%6.1f ops/s", ops_per_sec);
    }

    printf("%-38s | %10lu | %7.2f ms | %14s | %14s\n",
           name, (unsigned long)iters, total_ms, lat_str, thr_str);
}

/* ------------------------------------------------------------------ */
/* 1. Text Shaping & Rasterization Benchmarks                          */
/* ------------------------------------------------------------------ */

static void bench_text_rendering(CguiTextEngine *engine)
{
    print_bench_header("Text Shaping & Rasterization (HarfBuzz + FreeType)");

    const char *short_str = "Settings";
    const char *med_str   = "Clean Vulkan UI with crisp macOS typography";
    const char *long_str  = "The quick brown fox jumps over the lazy dog. 1234567890! FreeType stem darkening and subpixel alignment.";

    /* Short label (Regular) */
    {
        uint64_t iters = 2000;
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            CguiTextBitmap bm = {0};
            cgui_text_render_styled(engine, short_str, 16, CGUI_TEXT_REGULAR, &bm);
            cgui_text_bitmap_destroy(&bm);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Rasterize Short Label (8 chars)", iters, elapsed);
    }

    /* Short label (Bold) */
    {
        uint64_t iters = 2000;
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            CguiTextBitmap bm = {0};
            cgui_text_render_styled(engine, short_str, 16, CGUI_TEXT_BOLD, &bm);
            cgui_text_bitmap_destroy(&bm);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Rasterize Short Label Bold (8 chars)", iters, elapsed);
    }

    /* Medium sentence (43 chars) */
    {
        uint64_t iters = 1000;
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            CguiTextBitmap bm = {0};
            cgui_text_render_styled(engine, med_str, 14, CGUI_TEXT_REGULAR, &bm);
            cgui_text_bitmap_destroy(&bm);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Rasterize Medium Sentence (43 chars)", iters, elapsed);
    }

    /* Long paragraph (103 chars) */
    {
        uint64_t iters = 500;
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            CguiTextBitmap bm = {0};
            cgui_text_render_styled(engine, long_str, 14, CGUI_TEXT_REGULAR, &bm);
            cgui_text_bitmap_destroy(&bm);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Rasterize Long Text (103 chars)", iters, elapsed);
    }

    /* Large font rasterization (48 px) */
    {
        uint64_t iters = 500;
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            CguiTextBitmap bm = {0};
            cgui_text_render_styled(engine, short_str, 48, CGUI_TEXT_BOLD, &bm);
            cgui_text_bitmap_destroy(&bm);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Rasterize Large Title (48px Bold)", iters, elapsed);
    }
}

/* ------------------------------------------------------------------ */
/* 2. Widget Scene Graph & Layout Benchmarks                          */
/* ------------------------------------------------------------------ */

static void bench_widget_lifecycle(void)
{
    print_bench_header("Widget Creation, Hierarchy & Destruction");

    /* Create and destroy single widgets */
    {
        uint64_t iters = 100000;
        Style s = style_default();
        Rect r = { 10, 10, 100, 30 };
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            Button *btn = button_create(r, s, "Btn");
            widget_destroy((Widget *)btn);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Button Create + Destroy", iters, elapsed);
    }

    /* Label creation with dynamic string duplication */
    {
        uint64_t iters = 100000;
        Style s = style_default();
        Rect r = { 0, 0, 200, 24 };
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            Label *lbl = label_create(r, s, "Hardware Acceleration Status");
            widget_destroy((Widget *)lbl);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Label Create (strdup) + Destroy", iters, elapsed);
    }

    /* Toggle creation */
    {
        uint64_t iters = 100000;
        Style s = style_default();
        Rect r = { 0, 0, 51, 31 };
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            Toggle *t = toggle_create(r, s, true);
            widget_destroy((Widget *)t);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Toggle Create + Destroy", iters, elapsed);
    }

    /* Slider creation */
    {
        uint64_t iters = 100000;
        Style s = style_default();
        Rect r = { 0, 0, 200, 28 };
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            Slider *sl = slider_create(r, s, 0.0f, 100.0f, 50.0f);
            widget_destroy((Widget *)sl);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Slider Create + Destroy", iters, elapsed);
    }

    /* Deep Scene Graph: 1 Panel with 100 child widgets */
    {
        uint64_t iters = 2000;
        Style s = style_default();
        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            Panel *panel = panel_create((Rect){0, 0, 800, 600}, s);
            for (int c = 0; c < 100; ++c) {
                Rect br = { (float)(c * 5), (float)(c * 5), 80, 25 };
                Button *b = button_create(br, s, "Item");
                widget_add_child((Widget *)panel, (Widget *)b);
            }
            widget_destroy((Widget *)panel);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Scene Graph (1 Panel + 100 Children)", iters, elapsed);
    }
}

/* ------------------------------------------------------------------ */
/* 3. Event Processing & Animation Benchmarks                         */
/* ------------------------------------------------------------------ */

static void bench_events_and_animation(void)
{
    print_bench_header("Event Dispatch & Animation Simulation");

    /* Event dispatch across 50 widgets */
    {
        uint64_t iters = 50000;
        Widget *root = widget_create();
        for (int i = 0; i < 50; ++i) {
            Rect r = { (float)(i * 10), (float)(i * 10), 100, 40 };
            Button *btn = button_create(r, style_default(), "Test");
            widget_add_child(root, (Widget *)btn);
        }

        SDL_Event ev = {0};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 250.0f;
        ev.motion.y = 250.0f;

        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            for (size_t c = 0; c < root->child_count; ++c) {
                Widget *ch = root->children[c];
                if (ch->handle_event) ch->handle_event(ch, &ev);
            }
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Event Dispatch (50 widgets hierarchy)", iters, elapsed);
        widget_destroy(root);
    }

    /* Slider interactive dragging (event processing + value snapping) */
    {
        uint64_t iters = 100000;
        Rect r = { 10.0f, 10.0f, 300.0f, 28.0f };
        Slider *sl = slider_create(r, style_default(), 0.0f, 100.0f, 0.0f);

        SDL_Event down_ev = {0};
        down_ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        down_ev.button.button = SDL_BUTTON_LEFT;
        down_ev.button.x = 10.0f;
        down_ev.button.y = 20.0f;
        sl->base.handle_event((Widget *)sl, &down_ev);

        SDL_Event move_ev = {0};
        move_ev.type = SDL_EVENT_MOUSE_MOTION;
        move_ev.motion.y = 20.0f;

        uint64_t start = get_time_ns();
        for (uint64_t i = 0; i < iters; ++i) {
            move_ev.motion.x = 10.0f + (float)(i % 300);
            sl->base.handle_event((Widget *)sl, &move_ev);
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Slider Drag Event Stream", iters, elapsed);

        widget_destroy((Widget *)sl);
    }

    /* High-density Animation: 5,000 animated toggle switches updated simultaneously */
    {
        const int NUM_TOGGLES = 5000;
        uint64_t iters = 1000; /* 1000 simulated frames */
        Toggle **toggles = malloc(sizeof(Toggle *) * (size_t)NUM_TOGGLES);
        Style s = style_default();
        Rect r = { 0, 0, 51, 31 };

        for (int i = 0; i < NUM_TOGGLES; ++i) {
            toggles[i] = toggle_create(r, s, (i % 2 == 0));
            toggle_set_on(toggles[i], (i % 2 != 0)); /* trigger animation */
        }

        float dt = 1.0f / 60.0f; /* 60 FPS tick */
        uint64_t start = get_time_ns();
        for (uint64_t frame = 0; frame < iters; ++frame) {
            for (int i = 0; i < NUM_TOGGLES; ++i) {
                toggle_update(toggles[i], dt);
            }
        }
        uint64_t elapsed = get_time_ns() - start;
        print_bench_result("Update 5,000 Animated Toggles / Frame", iters, elapsed);

        for (int i = 0; i < NUM_TOGGLES; ++i) {
            widget_destroy((Widget *)toggles[i]);
        }
        free(toggles);
    }
}

/* ------------------------------------------------------------------ */
/* Main Benchmark Runner                                              */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\n\033[1;32m==============================================================================\033[0m\n");
    printf("\033[1;32m       cgui Release Performance Benchmarks (High-Precision Monotonic Timer)     \033[0m\n");
    printf("\033[1;32m==============================================================================\033[0m\n");

    CguiTextEngine *engine = cgui_text_create();
    if (!engine) {
        fprintf(stderr, "Failed to initialize CguiTextEngine for benchmarks\n");
        return 1;
    }

    bench_text_rendering(engine);
    bench_widget_lifecycle();
    bench_events_and_animation();

    cgui_text_destroy(engine);

    printf("\n\033[1;32m==============================================================================\033[0m\n");
    printf("All benchmarks completed successfully.\n\n");
    return 0;
}
