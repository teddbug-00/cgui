/**
 * text_field_test.c – Unit tests for the TextField widget
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <SDL3/SDL.h>

#include "text_field.h"

static int g_tests_passed = 0;
static int g_tests_total  = 0;

#define TEST(name) \
    do { \
        g_tests_total++; \
        printf("  %-52s ", name); \
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
/* 1. Creation & Default State                                          */
/* ------------------------------------------------------------------ */

static void test_create_defaults(void)
{
    TEST("text_field_create sets sane defaults");
    Rect r = {10.0f, 10.0f, 280.0f, 38.0f};
    TextField *tf = text_field_create(r, style_default(), "Type here…");
    if (!tf) { FAIL("NULL returned"); return; }

    if (strcmp(text_field_get_text(tf), "") != 0) { FAIL("initial text not empty"); goto done; }
    if (tf->cursor_pos != 0)   { FAIL("cursor_pos not 0"); goto done; }
    if (tf->focused)           { FAIL("should not be focused"); goto done; }
    if (!tf->placeholder)      { FAIL("placeholder not set"); goto done; }
    if (strcmp(tf->placeholder, "Type here…") != 0) { FAIL("wrong placeholder"); goto done; }
    if (!tf->base.draw)        { FAIL("draw fn not set"); goto done; }
    if (!tf->base.handle_event){ FAIL("event fn not set"); goto done; }
    if (!tf->base.destroy)     { FAIL("destroy fn not set"); goto done; }
    if (tf->capacity < 64)     { FAIL("capacity too small"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_create_no_placeholder(void)
{
    TEST("text_field_create with NULL placeholder");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL returned"); return; }

    if (tf->placeholder != NULL) { FAIL("placeholder should be NULL"); goto done; }
    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 2. Text Set / Get                                                    */
/* ------------------------------------------------------------------ */

static void test_set_get_text(void)
{
    TEST("text_field_set_text / get_text roundtrip");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_text(tf, "Hello");
    if (strcmp(text_field_get_text(tf), "Hello") != 0) { FAIL("wrong text after set"); goto done; }
    if (tf->cursor_pos != 5) { FAIL("cursor not at end"); goto done; }

    text_field_set_text(tf, "");
    if (strcmp(text_field_get_text(tf), "") != 0) { FAIL("empty string set failed"); goto done; }
    if (tf->cursor_pos != 0) { FAIL("cursor not reset to 0"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_get_text_never_null(void)
{
    TEST("text_field_get_text never returns NULL");
    if (text_field_get_text(NULL) == NULL) { FAIL("returned NULL on NULL input"); return; }

    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }
    const char *t = text_field_get_text(tf);
    if (t == NULL) { FAIL("returned NULL for empty field"); goto done; }
    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 3. Focus State                                                       */
/* ------------------------------------------------------------------ */

static void test_focus_state(void)
{
    TEST("text_field_set_focused / is_focused");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    if (text_field_is_focused(tf)) { FAIL("should start unfocused"); goto done; }
    text_field_set_focused(tf, true);
    if (!text_field_is_focused(tf)) { FAIL("should be focused"); goto done; }
    text_field_set_focused(tf, false);
    if (text_field_is_focused(tf)) { FAIL("should be unfocused again"); goto done; }
    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_focus_resets_blink(void)
{
    TEST("focus state resets blink timer");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    tf->blink_timer = 3.7f;
    text_field_set_focused(tf, true);
    if (tf->blink_timer != 0.0f) { FAIL("blink_timer not reset on focus"); goto done; }
    if (!tf->cursor_visible)     { FAIL("cursor should be visible on focus"); goto done; }
    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 4. Text Input Events via SDL_EVENT_TEXT_INPUT                       */
/* ------------------------------------------------------------------ */

static void test_text_input_event(void)
{
    TEST("SDL_EVENT_TEXT_INPUT appends text at cursor");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);

    SDL_Event e;
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = "Hi";
    text_field_event((Widget *)tf, &e);

    if (strcmp(text_field_get_text(tf), "Hi") != 0) { FAIL("text not appended"); goto done; }
    if (tf->cursor_pos != 2)  { FAIL("cursor pos wrong"); goto done; }

    e.text.text = "!";
    text_field_event((Widget *)tf, &e);
    if (strcmp(text_field_get_text(tf), "Hi!") != 0) { FAIL("second append failed"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_text_input_ignored_when_unfocused(void)
{
    TEST("SDL_EVENT_TEXT_INPUT ignored when not focused");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    /* NOT focused */
    SDL_Event e;
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = "ghost";
    text_field_event((Widget *)tf, &e);

    if (tf->length != 0) { FAIL("text was accepted while unfocused"); goto done; }
    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 5. Keyboard Navigation & Editing                                     */
/* ------------------------------------------------------------------ */

static void fire_key(TextField *tf, SDL_Keycode key, SDL_Keymod mod)
{
    SDL_Event e;
    e.type    = SDL_EVENT_KEY_DOWN;
    e.key.key = key;
    e.key.mod = mod;
    text_field_event((Widget *)tf, &e);
}

static void test_backspace(void)
{
    TEST("BACKSPACE deletes character before cursor");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_set_text(tf, "Hello");

    fire_key(tf, SDLK_BACKSPACE, SDL_KMOD_NONE);
    if (strcmp(text_field_get_text(tf), "Hell") != 0) { FAIL("wrong text after backspace"); goto done; }
    if (tf->cursor_pos != 4) { FAIL("cursor pos wrong after backspace"); goto done; }

    /* Backspace at pos 0 is a no-op */
    tf->cursor_pos = 0;
    fire_key(tf, SDLK_BACKSPACE, SDL_KMOD_NONE);
    if (tf->length != 4) { FAIL("backspace at pos 0 removed a char"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_delete_forward(void)
{
    TEST("DELETE removes character after cursor");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_set_text(tf, "Hello");
    tf->cursor_pos = 0;

    fire_key(tf, SDLK_DELETE, SDL_KMOD_NONE);
    if (strcmp(text_field_get_text(tf), "ello") != 0) { FAIL("wrong text after delete"); goto done; }
    if (tf->cursor_pos != 0) { FAIL("cursor pos changed after forward delete"); goto done; }

    /* Delete at end is a no-op */
    tf->cursor_pos = tf->length;
    fire_key(tf, SDLK_DELETE, SDL_KMOD_NONE);
    if (strcmp(text_field_get_text(tf), "ello") != 0) { FAIL("delete at end changed text"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_arrow_keys(void)
{
    TEST("LEFT / RIGHT arrow keys move cursor correctly");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_set_text(tf, "abc");  /* length=3, cursor at 3 */

    fire_key(tf, SDLK_LEFT, SDL_KMOD_NONE);
    if (tf->cursor_pos != 2) { FAIL("LEFT didn't move cursor to 2"); goto done; }

    fire_key(tf, SDLK_LEFT, SDL_KMOD_NONE);
    if (tf->cursor_pos != 1) { FAIL("LEFT didn't move cursor to 1"); goto done; }

    fire_key(tf, SDLK_RIGHT, SDL_KMOD_NONE);
    if (tf->cursor_pos != 2) { FAIL("RIGHT didn't move cursor back to 2"); goto done; }

    /* LEFT past beginning clamps to 0 */
    tf->cursor_pos = 0;
    fire_key(tf, SDLK_LEFT, SDL_KMOD_NONE);
    if (tf->cursor_pos != 0) { FAIL("LEFT past start moved past 0"); goto done; }

    /* RIGHT past end clamps to length */
    tf->cursor_pos = tf->length;
    fire_key(tf, SDLK_RIGHT, SDL_KMOD_NONE);
    if (tf->cursor_pos != tf->length) { FAIL("RIGHT past end went past length"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_home_end_keys(void)
{
    TEST("HOME / END keys jump cursor to start / end");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_set_text(tf, "Hello World");

    fire_key(tf, SDLK_HOME, SDL_KMOD_NONE);
    if (tf->cursor_pos != 0) { FAIL("HOME didn't move cursor to 0"); goto done; }

    fire_key(tf, SDLK_END, SDL_KMOD_NONE);
    if (tf->cursor_pos != tf->length) { FAIL("END didn't move cursor to end"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_escape_clears_focus(void)
{
    TEST("ESCAPE removes focus from text field");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    if (!tf->focused) { FAIL("focus not set"); goto done; }

    fire_key(tf, SDLK_ESCAPE, SDL_KMOD_NONE);
    if (tf->focused) { FAIL("still focused after ESCAPE"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 6. Cursor Blink Animation                                            */
/* ------------------------------------------------------------------ */

static void test_cursor_blink_unfocused(void)
{
    TEST("cursor not visible when unfocused after update");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, false);
    text_field_update(tf, 0.1f);
    if (tf->cursor_visible) { FAIL("cursor visible while unfocused"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static void test_cursor_blink_cycle(void)
{
    TEST("cursor blinks ON→OFF→ON across a full 1.0s cycle");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_update(tf, 0.0f);
    if (!tf->cursor_visible) { FAIL("cursor not visible at t=0"); goto done; }

    text_field_update(tf, 0.6f);   /* now at 0.6s → OFF (>0.53s) */
    if (tf->cursor_visible) { FAIL("cursor should be OFF at 0.6s"); goto done; }

    text_field_update(tf, 0.45f);  /* now at 1.05s → ON again (wraps: 0.05s) */
    if (!tf->cursor_visible) { FAIL("cursor should be ON again at 1.05s"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 7. Callbacks                                                         */
/* ------------------------------------------------------------------ */

static int g_change_calls = 0;
static char g_change_text[256];

static void on_change(TextField *tf, const char *text, void *ud)
{
    (void)tf; (void)ud;
    g_change_calls++;
    strncpy(g_change_text, text, sizeof(g_change_text) - 1);
}

static void test_change_callback(void)
{
    TEST("on_change fires when text is modified");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    g_change_calls = 0;
    text_field_set_change_handler(tf, on_change, NULL);

    text_field_set_focused(tf, true);

    SDL_Event e;
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = "A";
    text_field_event((Widget *)tf, &e);

    if (g_change_calls != 1) { FAIL("on_change not called exactly once"); goto done; }
    if (strcmp(g_change_text, "A") != 0) { FAIL("wrong text in callback"); goto done; }

    fire_key(tf, SDLK_BACKSPACE, SDL_KMOD_NONE);
    if (g_change_calls != 2) { FAIL("on_change not called on backspace"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

static int g_submit_calls = 0;

static void on_submit(TextField *tf, const char *text, void *ud)
{
    (void)tf; (void)text; (void)ud;
    g_submit_calls++;
}

static void test_submit_callback(void)
{
    TEST("on_submit fires on RETURN key");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    g_submit_calls = 0;
    text_field_set_submit_handler(tf, on_submit, NULL);
    text_field_set_focused(tf, true);

    fire_key(tf, SDLK_RETURN, SDL_KMOD_NONE);
    if (g_submit_calls != 1) { FAIL("on_submit not called on RETURN"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 8. Capacity Growth                                                   */
/* ------------------------------------------------------------------ */

static void test_capacity_growth(void)
{
    TEST("buffer grows to hold >64 byte initial capacity");
    Rect r = {0, 0, 400, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);

    /* Insert 100 ASCII chars via TEXT_INPUT events */
    SDL_Event e;
    e.type = SDL_EVENT_TEXT_INPUT;
    for (int i = 0; i < 100; i++) {
        e.text.text = "x";
        text_field_event((Widget *)tf, &e);
    }

    if (tf->length != 100) { FAIL("wrong length after 100 inserts"); goto done; }
    if (tf->cursor_pos != 100) { FAIL("cursor not at end"); goto done; }
    if (tf->capacity < 101) { FAIL("capacity not grown"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 9. Mid-string Insertion                                              */
/* ------------------------------------------------------------------ */

static void test_mid_string_insert(void)
{
    TEST("text inserted at mid-cursor position");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), NULL);
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_focused(tf, true);
    text_field_set_text(tf, "Helo");   /* typo: cursor at pos 4 */
    tf->cursor_pos = 3;               /* place cursor between 'l' and 'o' */

    SDL_Event e;
    e.type = SDL_EVENT_TEXT_INPUT;
    e.text.text = "l";
    text_field_event((Widget *)tf, &e);

    if (strcmp(text_field_get_text(tf), "Hello") != 0) { FAIL("mid-insert produced wrong text"); goto done; }
    if (tf->cursor_pos != 4) { FAIL("cursor not advanced after mid-insert"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 10. Mouse Click Focus                                               */
/* ------------------------------------------------------------------ */

static void test_mouse_click_focus(void)
{
    TEST("clicking inside bounds gives focus, outside removes it");
    Rect r = {100.0f, 100.0f, 200.0f, 38.0f};
    TextField *tf = text_field_create(r, style_default(), "hint");
    if (!tf) { FAIL("NULL"); return; }

    SDL_Event click_inside;
    click_inside.type         = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click_inside.button.button = SDL_BUTTON_LEFT;
    click_inside.button.x     = 150.0f;
    click_inside.button.y     = 119.0f;
    text_field_event((Widget *)tf, &click_inside);
    if (!tf->focused) { FAIL("click inside did not set focus"); goto done; }

    SDL_Event click_outside;
    click_outside.type          = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click_outside.button.button = SDL_BUTTON_LEFT;
    click_outside.button.x      = 10.0f;
    click_outside.button.y      = 10.0f;
    text_field_event((Widget *)tf, &click_outside);
    if (tf->focused) { FAIL("click outside did not remove focus"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* 11. set_placeholder API                                              */
/* ------------------------------------------------------------------ */

static void test_set_placeholder(void)
{
    TEST("text_field_set_placeholder updates string correctly");
    Rect r = {0, 0, 200, 36};
    TextField *tf = text_field_create(r, style_default(), "Old");
    if (!tf) { FAIL("NULL"); return; }

    text_field_set_placeholder(tf, "New hint");
    if (!tf->placeholder || strcmp(tf->placeholder, "New hint") != 0) { FAIL("placeholder not updated"); goto done; }

    text_field_set_placeholder(tf, NULL);
    if (tf->placeholder != NULL) { FAIL("placeholder should be NULL after NULL set"); goto done; }

    PASS();
done:
    widget_destroy((Widget *)tf);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("\n=== TextField Widget Tests ===\n\n");

    /* 1. Creation */
    test_create_defaults();
    test_create_no_placeholder();

    /* 2. Text access */
    test_set_get_text();
    test_get_text_never_null();

    /* 3. Focus */
    test_focus_state();
    test_focus_resets_blink();

    /* 4. Text input events */
    test_text_input_event();
    test_text_input_ignored_when_unfocused();

    /* 5. Keyboard editing */
    test_backspace();
    test_delete_forward();
    test_arrow_keys();
    test_home_end_keys();
    test_escape_clears_focus();

    /* 6. Cursor blink */
    test_cursor_blink_unfocused();
    test_cursor_blink_cycle();

    /* 7. Callbacks */
    test_change_callback();
    test_submit_callback();

    /* 8. Buffer growth */
    test_capacity_growth();

    /* 9. Mid-string insert */
    test_mid_string_insert();

    /* 10. Mouse click */
    test_mouse_click_focus();

    /* 11. Placeholder API */
    test_set_placeholder();

    printf("\n=== Results: %d / %d passed ===\n\n",
           g_tests_passed, g_tests_total);

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
