/**
 * toggle_test.c – Unit tests for the Toggle/Switch widget
 *
 * Tests toggle creation, state management, animation, and callbacks
 * without requiring a GPU context.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "toggle.h"

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

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_create_default_off(void)
{
    TEST("toggle_create (initial OFF)");
    Rect bounds = { 100.0f, 100.0f, 51.0f, 31.0f };
    Style style = style_default();
    Toggle *t = toggle_create(bounds, style, false);

    if (!t) { FAIL("toggle_create returned NULL"); return; }
    if (t->on)                { FAIL("expected on=false"); goto cleanup; }
    if (t->anim_t != 0.0f)   { FAIL("expected anim_t=0.0"); goto cleanup; }
    if (t->hovered)           { FAIL("expected hovered=false"); goto cleanup; }
    if (t->pressed)           { FAIL("expected pressed=false"); goto cleanup; }

    /* Verify base widget setup */
    if (t->base.bounds.w != 51.0f) { FAIL("wrong width"); goto cleanup; }
    if (t->base.bounds.h != 31.0f) { FAIL("wrong height"); goto cleanup; }
    if (!t->base.draw)             { FAIL("draw fn not set"); goto cleanup; }
    if (!t->base.handle_event)     { FAIL("event fn not set"); goto cleanup; }

    PASS();
cleanup:
    free(t);
}

static void test_create_default_on(void)
{
    TEST("toggle_create (initial ON)");
    Rect bounds = { 100.0f, 100.0f, 51.0f, 31.0f };
    Style style = style_default();
    Toggle *t = toggle_create(bounds, style, true);

    if (!t) { FAIL("toggle_create returned NULL"); return; }
    if (!t->on)               { FAIL("expected on=true"); goto cleanup; }
    if (t->anim_t != 1.0f)    { FAIL("expected anim_t=1.0"); goto cleanup; }

    PASS();
cleanup:
    free(t);
}

static void test_toggle_set_on(void)
{
    TEST("toggle_set_on programmatic");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    toggle_set_on(t, true);
    if (!t->on)  { FAIL("expected on=true after set"); goto cleanup; }
    /* anim_t should not jump — it starts animating */

    toggle_set_on(t, false);
    if (t->on)   { FAIL("expected on=false after set"); goto cleanup; }

    PASS();
cleanup:
    free(t);
}

static void test_animation_converges(void)
{
    TEST("toggle_update animation converges");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    /* Turn on — animate until fully on */
    toggle_set_on(t, true);
    for (int i = 0; i < 100; ++i) {
        toggle_update(t, 1.0f / 60.0f);
    }
    if (fabsf(t->anim_t - 1.0f) > 0.01f) {
        FAIL("anim_t didn't converge to 1.0");
        goto cleanup;
    }

    /* Turn off — animate back */
    toggle_set_on(t, false);
    for (int i = 0; i < 100; ++i) {
        toggle_update(t, 1.0f / 60.0f);
    }
    if (fabsf(t->anim_t - 0.0f) > 0.01f) {
        FAIL("anim_t didn't converge to 0.0");
        goto cleanup;
    }

    PASS();
cleanup:
    free(t);
}

static bool g_callback_fired = false;
static bool g_callback_state = false;
static void test_callback_fn(Toggle *self, bool is_on, void *userdata)
{
    (void)self;
    (void)userdata;
    g_callback_fired = true;
    g_callback_state = is_on;
}

static void test_change_handler(void)
{
    TEST("toggle_set_change_handler");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    toggle_set_change_handler(t, test_callback_fn, NULL);
    if (t->on_change != test_callback_fn) {
        FAIL("callback not stored");
        goto cleanup;
    }

    PASS();
cleanup:
    free(t);
}

static void test_set_label(void)
{
    TEST("toggle_set_label");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    toggle_set_label(t, "Dark Mode");
    if (!t->label || strcmp(t->label, "Dark Mode") != 0) {
        FAIL("label not set correctly");
        goto cleanup;
    }

    /* Update label */
    toggle_set_label(t, "Auto Brightness");
    if (!t->label || strcmp(t->label, "Auto Brightness") != 0) {
        FAIL("label not updated correctly");
        goto cleanup;
    }

    PASS();
cleanup:
    if (t) {
        toggle_destroy_fn((Widget *)t);
        free(t);
    }
}

static void test_track_colors(void)
{
    TEST("toggle_set_track_colors");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    uint8_t on_c[4]  = { 255, 0, 0, 255 };
    uint8_t off_c[4] = { 0, 0, 255, 255 };
    toggle_set_track_colors(t, on_c, off_c);

    if (memcmp(t->track_on, on_c, 4) != 0) {
        FAIL("on color not set");
        goto cleanup;
    }
    if (memcmp(t->track_off, off_c, 4) != 0) {
        FAIL("off color not set");
        goto cleanup;
    }

    PASS();
cleanup:
    free(t);
}

static void test_is_on_getter(void)
{
    TEST("toggle_is_on getter");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), true);
    if (!t) { FAIL("NULL"); return; }

    if (!toggle_is_on(t)) { FAIL("expected true"); goto cleanup; }
    toggle_set_on(t, false);
    if (toggle_is_on(t)) { FAIL("expected false after set_on(false)"); goto cleanup; }

    PASS();
cleanup:
    free(t);
}

static void test_null_safety(void)
{
    TEST("null pointer safety");

    /* These should not crash */
    toggle_set_on(NULL, true);
    toggle_update(NULL, 0.016f);
    toggle_set_label(NULL, "test");
    toggle_set_change_handler(NULL, test_callback_fn, NULL);
    toggle_set_track_colors(NULL, NULL, NULL);

    PASS();
}

static void test_animation_partial(void)
{
    TEST("toggle_update partial animation");
    Rect bounds = { 0, 0, 51, 31 };
    Toggle *t = toggle_create(bounds, style_default(), false);
    if (!t) { FAIL("NULL"); return; }

    toggle_set_on(t, true);
    /* One frame at 60fps should move partway */
    toggle_update(t, 1.0f / 60.0f);
    if (t->anim_t <= 0.0f) {
        FAIL("anim_t should have increased");
        goto cleanup;
    }
    if (t->anim_t >= 1.0f) {
        FAIL("anim_t should not have reached 1.0 in one frame");
        goto cleanup;
    }

    PASS();
cleanup:
    free(t);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("\n=== Toggle Widget Tests ===\n\n");

    test_create_default_off();
    test_create_default_on();
    test_toggle_set_on();
    test_animation_converges();
    test_change_handler();
    test_set_label();
    test_track_colors();
    test_is_on_getter();
    test_null_safety();
    test_animation_partial();

    printf("\n%d / %d tests passed\n\n", g_tests_passed, g_tests_total);
    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
