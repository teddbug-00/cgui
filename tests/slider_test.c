/**
 * slider_test.c – Unit tests for the Slider widget
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <SDL3/SDL.h>

#include "slider.h"

static int g_tests_passed = 0;
static int g_tests_total  = 0;

#define TEST(name) \
    do { \
        g_tests_total++; \
        printf("  %-48s ", name); \
    } while (0)

#define PASS() \
    do { \
        g_tests_passed++; \
        printf("\033[32mPASS\033[0m\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("\033[31mFAIL\033[0m  %s\n", msg); \
    } while (0)

static void test_create_and_defaults(void)
{
    TEST("slider_create and default range");
    Rect r = { 10, 10, 200, 30 };
    Slider *s = slider_create(r, style_default(), 0.0f, 100.0f, 50.0f);
    if (!s) { FAIL("NULL returned"); return; }

    if (s->value != 50.0f)        { FAIL("wrong initial value"); goto cleanup; }
    if (s->min_value != 0.0f)     { FAIL("wrong min_value"); goto cleanup; }
    if (s->max_value != 100.0f)   { FAIL("wrong max_value"); goto cleanup; }
    if (s->is_dragging)           { FAIL("should not be dragging"); goto cleanup; }
    if (!s->base.draw)            { FAIL("draw fn not set"); goto cleanup; }
    if (!s->base.handle_event)    { FAIL("event fn not set"); goto cleanup; }

    PASS();
cleanup:
    free(s);
}

static void test_clamping(void)
{
    TEST("slider_set_value range clamping");
    Rect r = { 0, 0, 100, 20 };
    Slider *s = slider_create(r, style_default(), 10.0f, 20.0f, 15.0f);
    if (!s) { FAIL("NULL"); return; }

    slider_set_value(s, 5.0f);
    if (s->value != 10.0f) { FAIL("failed to clamp to min"); goto cleanup; }

    slider_set_value(s, 35.0f);
    if (s->value != 20.0f) { FAIL("failed to clamp to max"); goto cleanup; }

    slider_set_value(s, 12.5f);
    if (s->value != 12.5f) { FAIL("failed to set valid value"); goto cleanup; }

    PASS();
cleanup:
    free(s);
}

static void test_stepping(void)
{
    TEST("slider_set_step discrete snapping");
    Rect r = { 0, 0, 100, 20 };
    Slider *s = slider_create(r, style_default(), 0.0f, 100.0f, 0.0f);
    if (!s) { FAIL("NULL"); return; }

    slider_set_step(s, 10.0f);
    slider_set_value(s, 14.0f);
    if (fabsf(s->value - 10.0f) > 1e-4f) { FAIL("should snap down to 10.0"); goto cleanup; }

    slider_set_value(s, 16.0f);
    if (fabsf(s->value - 20.0f) > 1e-4f) { FAIL("should snap up to 20.0"); goto cleanup; }

    PASS();
cleanup:
    free(s);
}

static float g_last_cb_val = -1.0f;
static void on_slider_change(Slider *self, float val, void *userdata)
{
    (void)self;
    (void)userdata;
    g_last_cb_val = val;
}

static void test_callbacks(void)
{
    TEST("slider_set_change_handler fires on value change");
    Rect r = { 0, 0, 100, 20 };
    Slider *s = slider_create(r, style_default(), 0.0f, 1.0f, 0.0f);
    if (!s) { FAIL("NULL"); return; }

    g_last_cb_val = -1.0f;
    slider_set_change_handler(s, on_slider_change, NULL);
    slider_set_value(s, 0.75f);

    if (fabsf(g_last_cb_val - 0.75f) > 1e-4f) {
        FAIL("callback did not fire with new value");
        goto cleanup;
    }

    PASS();
cleanup:
    free(s);
}

static void test_mouse_drag_event(void)
{
    TEST("slider_event mouse drag updates value");
    /* Slider width = 120, thumb_radius = 10 -> travel = 100 px from x=10 to x=110 */
    Rect r = { 0, 0, 120, 20 };
    Slider *s = slider_create(r, style_default(), 0.0f, 100.0f, 0.0f);
    if (!s) { FAIL("NULL"); return; }

    /* Click mouse down at center of track (x = 60) */
    SDL_Event down_ev = {0};
    down_ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down_ev.button.button = SDL_BUTTON_LEFT;
    down_ev.button.x = 60.0f;
    down_ev.button.y = 10.0f;
    s->base.handle_event((Widget *)s, &down_ev);

    if (!s->is_dragging) { FAIL("expected is_dragging = true"); goto cleanup; }
    if (fabsf(s->value - 50.0f) > 1.0f) { FAIL("expected value around 50.0 after click at center"); goto cleanup; }

    /* Drag mouse to 3/4 position (x = 85 -> 75% of travel) */
    SDL_Event move_ev = {0};
    move_ev.type = SDL_EVENT_MOUSE_MOTION;
    move_ev.motion.x = 85.0f;
    move_ev.motion.y = 10.0f;
    s->base.handle_event((Widget *)s, &move_ev);

    if (fabsf(s->value - 75.0f) > 1.0f) { FAIL("expected value around 75.0 after drag"); goto cleanup; }

    /* Release mouse */
    SDL_Event up_ev = {0};
    up_ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up_ev.button.button = SDL_BUTTON_LEFT;
    s->base.handle_event((Widget *)s, &up_ev);

    if (s->is_dragging) { FAIL("expected is_dragging = false after button up"); goto cleanup; }

    PASS();
cleanup:
    free(s);
}

static void test_custom_colors(void)
{
    TEST("slider_set_track_colors");
    Rect r = { 0, 0, 100, 20 };
    Slider *s = slider_create(r, style_default(), 0.0f, 1.0f, 0.5f);
    if (!s) { FAIL("NULL"); return; }

    uint8_t act[4]   = { 52, 199, 89, 255 };  /* green */
    uint8_t inact[4] = { 100, 100, 100, 255 };
    slider_set_track_colors(s, act, inact);

    if (memcmp(s->track_active, act, 4) != 0)     { FAIL("active color mismatch"); goto cleanup; }
    if (memcmp(s->track_inactive, inact, 4) != 0) { FAIL("inactive color mismatch"); goto cleanup; }

    PASS();
cleanup:
    free(s);
}

static void test_null_safety(void)
{
    TEST("null safety");
    slider_set_value(NULL, 10.0f);
    slider_get_value(NULL);
    slider_set_step(NULL, 1.0f);
    slider_set_change_handler(NULL, NULL, NULL);
    slider_set_track_colors(NULL, NULL, NULL);
    PASS();
}

int main(void)
{
    printf("\n=== Slider Widget Tests ===\n\n");

    test_create_and_defaults();
    test_clamping();
    test_stepping();
    test_callbacks();
    test_mouse_drag_event();
    test_custom_colors();
    test_null_safety();

    printf("\n%d / %d tests passed\n\n", g_tests_passed, g_tests_total);
    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
