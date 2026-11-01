/**
 * scroll_view.c - Clipped vertical scrolling viewport.
 */

#include "scroll_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct ScrollView {
    Widget base;
    ScrollViewStyle view_style;
    Widget *content;
    float content_height;
    float position;
    float velocity;
};

static VkDraw *g_draw;

#define SCROLL_FRICTION 8.0f
#define SCROLL_SPRING_STIFFNESS 100.0f
#define SCROLL_SPRING_DAMPING 20.0f
#define SCROLL_RUBBER_BAND_FACTOR 0.55f

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

ScrollViewStyle scroll_view_style_default(void)
{
    return (ScrollViewStyle){ .wheel_step = 48.0f };
}

float scroll_view_max_scroll(const ScrollView *view)
{
    if (!view) return 0.0f;
    return fmaxf(0.0f, view->content_height - view->base.bounds.h);
}

static float rubber_band(float distance, float dimension)
{
    if (distance <= 0.0f || dimension <= 0.0f) return 0.0f;
    return dimension * (1.0f - 1.0f /
        (distance * SCROLL_RUBBER_BAND_FACTOR / dimension + 1.0f));
}

static float displayed_scroll_offset(const ScrollView *view)
{
    float maximum = scroll_view_max_scroll(view);
    if (view->position < 0.0f) {
        return -rubber_band(-view->position, view->base.bounds.h);
    }
    if (view->position > maximum) {
        return maximum + rubber_band(view->position - maximum,
                                     view->base.bounds.h);
    }
    return view->position;
}

static void update_content_bounds(ScrollView *view)
{
    if (!view || !view->content) return;
    view->content->bounds = (Rect) {
        .x = view->base.bounds.x,
        .y = view->base.bounds.y - displayed_scroll_offset(view),
        .w = view->base.bounds.w,
        .h = fmaxf(view->content_height, view->base.bounds.h),
    };
    widget_layout(view->content);
}

static void scroll_view_layout(Widget *self)
{
    ScrollView *view = (ScrollView *)self;
    float maximum = scroll_view_max_scroll(view);
    view->position = fminf(maximum, fmaxf(0.0f, view->position));
    view->velocity = 0.0f;
    update_content_bounds(view);
}

static void scroll_view_draw(Widget *self, struct VkContext *context)
{
    ScrollView *view = (ScrollView *)self;
    if (!view->content || !view->content->draw || !g_draw) return;
    if (!vk_draw_clip_push(g_draw, self->bounds.x, self->bounds.y,
                           self->bounds.w, self->bounds.h)) {
        if (g_draw->clip_depth >= sizeof(g_draw->clip_stack) /
                                  sizeof(g_draw->clip_stack[0])) {
            fprintf(stderr, "ScrollView clip nesting limit reached\n");
        }
        return;
    }
    view->content->draw(view->content, context);
    vk_draw_clip_pop(g_draw);
}

static void scroll_view_event(Widget *self, const void *event)
{
    ScrollView *view = (ScrollView *)self;
    const SDL_Event *ev = event;
    if (!view->content || !ev) return;
    if (ev->type == SDL_EVENT_MOUSE_WHEEL &&
        point_in_rect(ev->wheel.mouse_x, ev->wheel.mouse_y, &self->bounds)) {
        float direction = ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED
            ? 1.0f : -1.0f;
        float delta = direction * ev->wheel.y * view->view_style.wheel_step;
        view->velocity += delta * SCROLL_FRICTION;
        return;
    }
    if ((ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         ev->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        !point_in_rect((float)ev->button.x, (float)ev->button.y,
                       &self->bounds)) {
        return;
    }
    if (view->content->handle_event) {
        view->content->handle_event(view->content, event);
    }
}

ScrollView *scroll_view_create(Rect bounds, ScrollViewStyle style)
{
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h) ||
        bounds.w < 0.0f || bounds.h < 0.0f ||
        !isfinite(style.wheel_step) || style.wheel_step <= 0.0f) {
        return NULL;
    }
    ScrollView *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->base.bounds = bounds;
    view->base.style = style_default();
    view->base.draw = scroll_view_draw;
    view->base.handle_event = scroll_view_event;
    view->base.destroy = widget_destroy_noop;
    view->base.layout = scroll_view_layout;
    view->view_style = style;
    return view;
}

Widget *scroll_view_widget(ScrollView *view)
{
    return view ? &view->base : NULL;
}

bool scroll_view_set_content(ScrollView *view, Widget *content,
                             float content_height)
{
    if (!view || !content || view->content || content->parent ||
        !isfinite(content_height) || content_height < 0.0f ||
        !widget_add_child(&view->base, content)) {
        return false;
    }
    view->content = content;
    view->content_height = content_height;
    update_content_bounds(view);
    return true;
}

bool scroll_view_set_content_height(ScrollView *view, float content_height)
{
    if (!view || !view->content || !isfinite(content_height) ||
        content_height < 0.0f) {
        return false;
    }
    view->content_height = content_height;
    view->position = fminf(view->position, scroll_view_max_scroll(view));
    view->velocity = 0.0f;
    update_content_bounds(view);
    return true;
}

bool scroll_view_set_scroll_offset(ScrollView *view, float offset)
{
    if (!view || !isfinite(offset)) return false;
    float maximum = scroll_view_max_scroll(view);
    view->position = fminf(maximum, fmaxf(0.0f, offset));
    view->velocity = 0.0f;
    update_content_bounds(view);
    return true;
}

void scroll_view_update(ScrollView *view, float delta_seconds)
{
    if (!view || !view->content || !isfinite(delta_seconds) ||
        delta_seconds <= 0.0f) {
        return;
    }

    float remaining = fminf(delta_seconds, 0.1f);
    while (remaining > 0.0f) {
        float dt = fminf(remaining, 1.0f / 120.0f);
        float maximum = scroll_view_max_scroll(view);
        if (view->position < 0.0f) {
            float acceleration = -SCROLL_SPRING_STIFFNESS * view->position -
                                SCROLL_SPRING_DAMPING * view->velocity;
            view->velocity += acceleration * dt;
            view->position += view->velocity * dt;
        } else if (view->position > maximum) {
            float displacement = view->position - maximum;
            float acceleration = -SCROLL_SPRING_STIFFNESS * displacement -
                                SCROLL_SPRING_DAMPING * view->velocity;
            view->velocity += acceleration * dt;
            view->position += view->velocity * dt;
        } else {
            float decay = expf(-SCROLL_FRICTION * dt);
            view->position += view->velocity *
                              (1.0f - decay) / SCROLL_FRICTION;
            view->velocity *= decay;
        }
        remaining -= dt;
    }

    float maximum = scroll_view_max_scroll(view);
    if (view->position < 0.0f && fabsf(view->position) < 0.01f) {
        view->position = 0.0f;
        view->velocity = 0.0f;
    } else if (view->position > maximum &&
               fabsf(view->position - maximum) < 0.01f) {
        view->position = maximum;
        view->velocity = 0.0f;
    }
    if (fabsf(view->velocity) < 0.05f &&
        view->position >= 0.0f && view->position <= maximum) {
        view->velocity = 0.0f;
    }
    update_content_bounds(view);
}

float scroll_view_scroll_offset(const ScrollView *view)
{
    return view ? displayed_scroll_offset(view) : 0.0f;
}

void scroll_view_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
