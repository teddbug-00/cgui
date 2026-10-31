/**
 * slider.h – macOS-style horizontal slider / progress widget
 */

#ifndef SLIDER_H
#define SLIDER_H

#include "gui.h"
#include "vk_draw.h"

typedef struct Slider Slider;

typedef void (*SliderChangeFn)(Slider *self, float value, void *userdata);

struct Slider {
    Widget base;                /* MUST be first */

    float min_value;
    float max_value;
    float value;
    float step;                 /* 0.0 for continuous, > 0.0 for discrete snapping */

    bool is_dragging;
    bool is_hovered;

    /* Appearance (macOS defaults set in create) */
    float track_height;         /* Track bar height (default ~6 px) */
    float thumb_radius;         /* Knob radius (default ~10 px) */

    uint8_t track_active[4];    /* Filled track color (macOS blue) */
    uint8_t track_inactive[4];  /* Unfilled track color (light grey) */
    uint8_t thumb_color[4];     /* White thumb knob */
    uint8_t thumb_shadow[4];    /* Subtle thumb drop shadow */

    SliderChangeFn on_change;
    void *userdata;
};

/** Create a horizontal slider widget. */
Slider *slider_create(Rect bounds, Style style, float min_val, float max_val, float initial_val);

/** Programmatically set current slider value (clamped, triggers callback if changed). */
void slider_set_value(Slider *slider, float value);

/** Get current slider value. */
float slider_get_value(const Slider *slider);

/** Set step size for discrete snapping (0.0 for continuous). */
void slider_set_step(Slider *slider, float step);

/** Set value change callback. */
void slider_set_change_handler(Slider *slider, SliderChangeFn fn, void *userdata);

/** Custom track active (filled) and inactive colors. */
void slider_set_track_colors(Slider *slider,
                             const uint8_t active[4],
                             const uint8_t inactive[4]);

void slider_draw(Widget *self, struct VkContext *vk);
void slider_event(Widget *self, const void *ev);
void slider_destroy_fn(Widget *self);

void slider_set_draw_context(VkDraw *draw);

#endif /* SLIDER_H */
