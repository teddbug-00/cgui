/**
 * toggle.h – macOS-style pill toggle/switch widget
 */

#ifndef TOGGLE_H
#define TOGGLE_H

#include "gui.h"
#include "vk_draw.h"

typedef struct Toggle Toggle;

typedef void (*ToggleChangeFn)(Toggle *self, bool is_on, void *userdata);

struct Toggle {
    Widget base;              /* MUST be first */

    bool on;                  /* Current state: on/off */
    bool hovered;
    bool pressed;

    /* Animation state */
    float anim_t;             /* 0.0 = fully OFF, 1.0 = fully ON */

    /* Colors (macOS defaults set in create) */
    uint8_t track_on[4];      /* Green track when ON  */
    uint8_t track_off[4];     /* Grey track when OFF  */
    uint8_t knob_color[4];    /* White knob           */
    uint8_t knob_shadow[4];   /* Subtle knob shadow   */

    /* Optional label text displayed to the left */
    char *label;

    ToggleChangeFn on_change;
    void *userdata;
};

/** Create a toggle widget. bounds defines the toggle track area (recommended: 51x31). */
Toggle *toggle_create(Rect bounds, Style style, bool initial_state);

/** Set the change callback. */
void toggle_set_change_handler(Toggle *t, ToggleChangeFn fn, void *userdata);

/** Set a text label displayed to the left of the toggle. */
void toggle_set_label(Toggle *t, const char *label);

/** Set custom track colors (on/off). */
void toggle_set_track_colors(Toggle *t,
                             const uint8_t on_color[4],
                             const uint8_t off_color[4]);

/** Get current toggle state. */
bool toggle_is_on(const Toggle *t);

/** Programmatically set toggle state (triggers animation, no callback). */
void toggle_set_on(Toggle *t, bool on);

/** Advance animation by dt seconds. Call once per frame. */
void toggle_update(Toggle *t, float dt);

void toggle_draw(Widget *self, struct VkContext *vk);
void toggle_event(Widget *self, const void *ev);
void toggle_destroy_fn(Widget *self);

void toggle_set_draw_context(VkDraw *draw);

#endif /* TOGGLE_H */
