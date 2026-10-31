/**
 * slider.c – macOS-style horizontal slider / progress widget
 */

#include "slider.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static VkDraw *g_draw = NULL;

void slider_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

static inline float clampf(float v, float min_v, float max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

static bool point_in_rect(float px, float py, const Rect *r)
{
    return px >= r->x && px <= r->x + r->w &&
           py >= r->y && py <= r->y + r->h;
}

Slider *slider_create(Rect bounds, Style style, float min_val, float max_val, float initial_val)
{
    if (max_val <= min_val) {
        max_val = min_val + 1.0f;
    }

    Slider *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->base.bounds       = bounds;
    s->base.style        = style;
    s->base.draw         = slider_draw;
    s->base.handle_event = slider_event;
    s->base.destroy      = slider_destroy_fn;

    s->min_value = min_val;
    s->max_value = max_val;
    s->value     = clampf(initial_val, min_val, max_val);
    s->step      = 0.0f;

    /* macOS control defaults */
    s->track_height = 5.0f;
    s->thumb_radius = 10.0f;

    /* macOS system blue fill (#007AFF) */
    memcpy(s->track_active,   (uint8_t[]){0, 122, 255, 255},   4);
    /* Inactive muted background track */
    memcpy(s->track_inactive, (uint8_t[]){220, 220, 225, 255}, 4);
    /* Crisp white circular thumb */
    memcpy(s->thumb_color,    (uint8_t[]){255, 255, 255, 255}, 4);
    /* Soft drop shadow behind thumb */
    memcpy(s->thumb_shadow,   (uint8_t[]){0, 0, 0, 45},        4);

    return s;
}

void slider_destroy_fn(Widget *self)
{
    (void)self;
}

float slider_get_value(const Slider *slider)
{
    return slider ? slider->value : 0.0f;
}

void slider_set_step(Slider *slider, float step)
{
    if (slider) {
        slider->step = fmaxf(0.0f, step);
    }
}

void slider_set_value(Slider *slider, float value)
{
    if (!slider) return;
    float clamped = clampf(value, slider->min_value, slider->max_value);
    if (slider->step > 0.0f) {
        float steps = roundf((clamped - slider->min_value) / slider->step);
        clamped = slider->min_value + steps * slider->step;
        clamped = clampf(clamped, slider->min_value, slider->max_value);
    }

    if (fabsf(clamped - slider->value) > 1e-5f) {
        slider->value = clamped;
        if (slider->on_change) {
            slider->on_change(slider, slider->value, slider->userdata);
        }
    }
}

void slider_set_change_handler(Slider *slider, SliderChangeFn fn, void *userdata)
{
    if (!slider) return;
    slider->on_change = fn;
    slider->userdata  = userdata;
}

void slider_set_track_colors(Slider *slider,
                             const uint8_t active[4],
                             const uint8_t inactive[4])
{
    if (!slider) return;
    if (active)   memcpy(slider->track_active,   active,   4);
    if (inactive) memcpy(slider->track_inactive, inactive, 4);
}

void slider_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) return;
    Slider *s = (Slider *)self;

    const Rect *b = &self->bounds;
    float th = s->track_height;
    float tr = s->thumb_radius;
    float ty = b->y + (b->h - th) * 0.5f;

    /* Travel distance between center of thumb at min and max */
    float travel = b->w - 2.0f * tr;
    if (travel < 1.0f) travel = 1.0f;

    float norm = (s->value - s->min_value) / (s->max_value - s->min_value);
    norm = clampf(norm, 0.0f, 1.0f);

    float thumb_cx = b->x + tr + norm * travel;
    float thumb_cy = b->y + b->h * 0.5f;

    /* 1. Full inactive background track pill */
    vk_draw_rounded_rect(g_draw, b->x, ty, b->w, th, th * 0.5f, 1.0f, s->track_inactive);

    /* 2. Active filled portion from left up to thumb center */
    float active_w = thumb_cx - b->x;
    if (active_w > 0.0f) {
        vk_draw_rounded_rect(g_draw, b->x, ty, active_w, th, th * 0.5f, 1.0f, s->track_active);
    }

    /* 3. Thumb drop shadow */
    float shadow_grow = 1.0f;
    vk_draw_rounded_rect(g_draw,
                         thumb_cx - tr - shadow_grow,
                         thumb_cy - tr - shadow_grow + 1.0f,
                         (tr + shadow_grow) * 2.0f,
                         (tr + shadow_grow) * 2.0f,
                         tr + shadow_grow,
                         1.0f,
                         s->thumb_shadow);

    /* 4. Thumb knob */
    vk_draw_rounded_rect(g_draw,
                         thumb_cx - tr,
                         thumb_cy - tr,
                         tr * 2.0f,
                         tr * 2.0f,
                         tr,
                         1.0f,
                         s->thumb_color);
}

void slider_event(Widget *self, const void *ev_ptr)
{
    if (!self || !ev_ptr) return;
    Slider *s = (Slider *)self;
    const SDL_Event *ev = (const SDL_Event *)ev_ptr;

    const Rect *b = &self->bounds;
    float tr = s->thumb_radius;
    float travel = b->w - 2.0f * tr;
    if (travel < 1.0f) travel = 1.0f;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION: {
        float mx = ev->motion.x;
        float my = ev->motion.y;
        s->is_hovered = point_in_rect(mx, my, b);

        if (s->is_dragging) {
            float t = (mx - (b->x + tr)) / travel;
            t = clampf(t, 0.0f, 1.0f);
            float new_val = s->min_value + t * (s->max_value - s->min_value);
            slider_set_value(s, new_val);
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (ev->button.button != SDL_BUTTON_LEFT) break;
        float mx = ev->button.x;
        float my = ev->button.y;
        if (point_in_rect(mx, my, b)) {
            s->is_dragging = true;
            float t = (mx - (b->x + tr)) / travel;
            t = clampf(t, 0.0f, 1.0f);
            float new_val = s->min_value + t * (s->max_value - s->min_value);
            slider_set_value(s, new_val);
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (ev->button.button == SDL_BUTTON_LEFT) {
            s->is_dragging = false;
        }
        break;
    }

    default:
        break;
    }
}
