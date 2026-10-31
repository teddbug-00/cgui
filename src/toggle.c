/**
 * toggle.c – macOS-style pill toggle/switch widget
 *
 * A smooth, animated on/off toggle that mimics the native macOS switch.
 * Track: 51×31 pill, knob: 27×27 circle, ~125 ms ease-in-out transition.
 */

#include "toggle.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Global draw context                                                */
/* ------------------------------------------------------------------ */

static VkDraw *g_draw;

void toggle_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

/* ------------------------------------------------------------------ */
/*  Internal constants                                                 */
/* ------------------------------------------------------------------ */

/** Knob diameter (px). */
#define KNOB_SIZE   27.0f

/** Inset of the knob from the track edges (px). */
#define KNOB_INSET   2.0f

/** Animation speed – higher is faster (1/speed ≈ transition time). */
#define ANIM_SPEED   8.0f

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

/** SmoothStep easing: slow start, fast middle, slow end. */
static inline float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/** Clamp a float to [0, 1]. */
static inline float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/** Linearly interpolate a single byte channel. */
static inline uint8_t lerp_byte(uint8_t a, uint8_t b, float t)
{
    return (uint8_t)(a + t * (b - a));
}

/** Lerp an RGBA colour tuple. */
static void lerp_color(uint8_t out[4],
                       const uint8_t a[4],
                       const uint8_t b[4],
                       float t)
{
    out[0] = lerp_byte(a[0], b[0], t);
    out[1] = lerp_byte(a[1], b[1], t);
    out[2] = lerp_byte(a[2], b[2], t);
    out[3] = lerp_byte(a[3], b[3], t);
}

/** Point-in-rect hit test. */
static bool point_in_rect(float px, float py, const Rect *r)
{
    return px >= r->x && px <= r->x + r->w &&
           py >= r->y && py <= r->y + r->h;
}

/* ------------------------------------------------------------------ */
/*  Create / Destroy                                                   */
/* ------------------------------------------------------------------ */

Toggle *toggle_create(Rect bounds, Style style, bool initial_state)
{
    Toggle *t = calloc(1, sizeof(*t));
    if (!t) return NULL;

    /* Widget base */
    t->base.bounds      = bounds;
    t->base.style       = style;
    t->base.draw        = toggle_draw;
    t->base.handle_event = toggle_event;
    t->base.destroy     = toggle_destroy_fn;

    /* Toggle state */
    t->on     = initial_state;
    t->anim_t = initial_state ? 1.0f : 0.0f;

    /* macOS default colours */
    memcpy(t->track_on,    (uint8_t[]){52, 199, 89, 255},   4);
    memcpy(t->track_off,   (uint8_t[]){200, 200, 200, 255}, 4);
    memcpy(t->knob_color,  (uint8_t[]){255, 255, 255, 255}, 4);
    memcpy(t->knob_shadow, (uint8_t[]){0, 0, 0, 40},        4);

    return t;
}

void toggle_destroy_fn(Widget *self)
{
    if (!self) return;
    Toggle *t = (Toggle *)self;
    free(t->label);
    /* No further cleanup; widget_destroy handles children. */
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void toggle_set_change_handler(Toggle *t, ToggleChangeFn fn, void *userdata)
{
    if (!t) return;
    t->on_change = fn;
    t->userdata  = userdata;
}

void toggle_set_label(Toggle *t, const char *label)
{
    if (!t) return;
    free(t->label);
    t->label = label ? strdup(label) : NULL;
}

void toggle_set_track_colors(Toggle *t,
                             const uint8_t on_color[4],
                             const uint8_t off_color[4])
{
    if (!t) return;
    memcpy(t->track_on,  on_color,  4);
    memcpy(t->track_off, off_color, 4);
}

bool toggle_is_on(const Toggle *t)
{
    return t ? t->on : false;
}

void toggle_set_on(Toggle *t, bool on)
{
    if (!t) return;
    t->on = on;
    /* Animation will converge in toggle_update(). No callback fired. */
}

/* ------------------------------------------------------------------ */
/*  Animation                                                          */
/* ------------------------------------------------------------------ */

void toggle_update(Toggle *t, float dt)
{
    if (!t) return;

    float target = t->on ? 1.0f : 0.0f;

    if (fabsf(t->anim_t - target) < 1e-5f) {
        t->anim_t = target;          /* snap when close enough */
        return;
    }

    /* Move anim_t toward target at ANIM_SPEED per second. */
    if (t->anim_t < target) {
        t->anim_t += ANIM_SPEED * dt;
        if (t->anim_t > target) t->anim_t = target;
    } else {
        t->anim_t -= ANIM_SPEED * dt;
        if (t->anim_t < target) t->anim_t = target;
    }

    t->anim_t = clamp01(t->anim_t);
}

/* ------------------------------------------------------------------ */
/*  Drawing                                                            */
/* ------------------------------------------------------------------ */

void toggle_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) return;
    Toggle *t = (Toggle *)self;

    const Rect *b = &self->bounds;

    float track_x = b->x;
    float track_y = b->y;
    float track_w = b->w;
    float track_h = b->h;
    float track_r = track_h * 0.5f;   /* pill radius */

    float ease = smoothstep(t->anim_t);

    /* --- 1. Track background ---------------------------------------- */
    uint8_t track_col[4];
    lerp_color(track_col, t->track_off, t->track_on, ease);

    vk_draw_rounded_rect(g_draw,
                         track_x, track_y,
                         track_w, track_h,
                         track_r,
                         1.0f,           /* soft edge */
                         track_col);

    /* --- 2. Knob geometry ------------------------------------------- */
    float knob_sz = KNOB_SIZE;
    float off_x   = track_x + KNOB_INSET;
    float on_x    = track_x + track_w - knob_sz - KNOB_INSET;
    float knob_x  = off_x + ease * (on_x - off_x);
    float knob_y  = track_y + (track_h - knob_sz) * 0.5f;
    float knob_r  = knob_sz * 0.5f;

    /* --- 3. Knob shadow (slightly larger, offset down 1 px) --------- */
    float shadow_grow = 1.0f;   /* expand 1 px on each side */
    vk_draw_rounded_rect(g_draw,
                         knob_x  - shadow_grow,
                         knob_y  - shadow_grow + 1.0f,
                         knob_sz + shadow_grow * 2.0f,
                         knob_sz + shadow_grow * 2.0f,
                         knob_r  + shadow_grow,
                         1.0f,
                         t->knob_shadow);

    /* --- 4. Knob ---------------------------------------------------- */
    vk_draw_rounded_rect(g_draw,
                         knob_x, knob_y,
                         knob_sz, knob_sz,
                         knob_r,
                         1.0f,
                         t->knob_color);

    /* --- 5. Optional label text (to the left of the track) ---------- */
    if (t->label) {
        float label_w = track_x - b->x;   /* available space left */
        /* Fall back to a reasonable width if the track fills the rect. */
        if (label_w < 10.0f) label_w = 80.0f;

        vk_draw_text_centered_styled(g_draw,
                                     t->label,
                                     b->x - label_w - 8.0f,
                                     track_y,
                                     label_w,
                                     track_h,
                                     self->style.text_size > 0
                                         ? self->style.text_size : 14.0f,
                                     self->style.text_style,
                                     self->style.text_color);
    }
}

/* ------------------------------------------------------------------ */
/*  Event handling                                                     */
/* ------------------------------------------------------------------ */

void toggle_event(Widget *self, const void *ev)
{
    if (!self || !ev) return;
    Toggle *t = (Toggle *)self;

    const SDL_Event *e = (const SDL_Event *)ev;

    switch (e->type) {

    case SDL_EVENT_MOUSE_MOTION: {
        float mx = e->motion.x;
        float my = e->motion.y;
        t->hovered = point_in_rect(mx, my, &self->bounds);
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        float mx = e->button.x;
        float my = e->button.y;
        if (point_in_rect(mx, my, &self->bounds))
            t->pressed = true;
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        if (!t->pressed) break;
        t->pressed = false;

        float mx = e->button.x;
        float my = e->button.y;
        if (point_in_rect(mx, my, &self->bounds)) {
            t->on = !t->on;
            if (t->on_change)
                t->on_change(t, t->on, t->userdata);
        }
        break;
    }

    default:
        break;
    }
}
