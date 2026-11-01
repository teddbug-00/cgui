/**
 * text_field.h – macOS-style single-line text input field widget
 */

#ifndef TEXT_FIELD_H
#define TEXT_FIELD_H

#include "gui.h"
#include "vk_draw.h"

typedef struct TextField TextField;

typedef void (*TextFieldChangeFn)(TextField *self, const char *text, void *userdata);
typedef void (*TextFieldSubmitFn)(TextField *self, const char *text, void *userdata);

struct TextField {
    Widget base;                 /* MUST be first */

    char *text;                  /* Dynamically allocated UTF-8 string */
    size_t capacity;             /* Allocated buffer size */
    size_t length;               /* Byte length (strlen) */
    size_t cursor_pos;           /* Byte offset of cursor (0..length) */

    char *placeholder;           /* Displayed when text is empty */

    bool focused;
    bool hovered;

    /* Cursor animation */
    float blink_timer;           /* Accumulated time in seconds */
    bool cursor_visible;         /* Calculated visibility based on blink cycle */

    /* Horizontal text scroll offset for long strings */
    float scroll_x;

    /* Styling & Colors (macOS defaults) */
    float corner_radius;         /* Corner rounding (default ~8.0 px) */
    float font_size;             /* Text size (default ~14.0 px) */
    float padding_x;             /* Left/right inner padding (default ~12.0 px) */

    uint8_t bg_color[4];         /* White field background */
    uint8_t border_normal[4];    /* Subtle light gray border */
    uint8_t border_focused[4];   /* macOS system blue focus ring */
    uint8_t text_color[4];       /* Dark text color */
    uint8_t placeholder_color[4];/* Muted placeholder text color */
    uint8_t cursor_color[4];     /* Blue cursor tint */

    TextFieldChangeFn on_change;
    void *on_change_userdata;

    TextFieldSubmitFn on_submit;
    void *on_submit_userdata;
};

/** Create a new text field widget. */
TextField *text_field_create(Rect bounds, Style style, const char *placeholder);

/** Set the text content programmatically. */
void text_field_set_text(TextField *tf, const char *text);

/** Get the current text content (never returns NULL). */
const char *text_field_get_text(const TextField *tf);

/** Set placeholder text. */
void text_field_set_placeholder(TextField *tf, const char *placeholder);

/** Programmatically set focus state. */
void text_field_set_focused(TextField *tf, bool focused);

/** Get current focus state. */
bool text_field_is_focused(const TextField *tf);

/** Set text modification callback. */
void text_field_set_change_handler(TextField *tf, TextFieldChangeFn fn, void *userdata);

/** Set Return/Enter submission callback. */
void text_field_set_submit_handler(TextField *tf, TextFieldSubmitFn fn, void *userdata);

/** Advance cursor blink animation by dt seconds. Call once per frame. */
void text_field_update(TextField *tf, float dt);

void text_field_draw(Widget *self, struct VkContext *vk);
void text_field_event(Widget *self, const void *ev);
void text_field_destroy_fn(Widget *self);

void text_field_set_draw_context(VkDraw *draw);

#endif /* TEXT_FIELD_H */
