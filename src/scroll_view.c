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
    float scrollbar_drag_offset;
    bool scrollbar_dragging;
    bool scrollbar_hovered;
    bool focused;
};

static VkDraw *g_draw;

#define SCROLL_FRICTION 8.0f
#define SCROLL_SPRING_STIFFNESS 100.0f
#define SCROLL_SPRING_DAMPING 20.0f
#define SCROLL_RUBBER_BAND_FACTOR 0.55f
#define SCROLLBAR_WIDTH 6.0f
#define SCROLLBAR_HIT_WIDTH 16.0f
#define SCROLLBAR_MIN_THUMB_HEIGHT 24.0f

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

static bool scrollbar_geometry(const ScrollView *view, Rect *track,
                               Rect *thumb)
{
    float maximum = scroll_view_max_scroll(view);
    if (maximum <= 0.0f || view->base.bounds.w <= 0.0f ||
        view->base.bounds.h <= 0.0f) {
        return false;
    }

    float width = fminf(SCROLLBAR_WIDTH, view->base.bounds.w);
    float track_x = view->base.bounds.x + view->base.bounds.w - width - 3.0f;
    if (track_x < view->base.bounds.x) track_x = view->base.bounds.x;
    *track = (Rect) {
        .x = track_x,
        .y = view->base.bounds.y,
        .w = width,
        .h = view->base.bounds.h,
    };

    float thumb_height = fmaxf(SCROLLBAR_MIN_THUMB_HEIGHT,
        track->h * track->h / view->content_height);
    thumb_height = fminf(thumb_height, track->h);
    float travel = track->h - thumb_height;
    float offset = fminf(maximum, fmaxf(0.0f, view->position));
    *thumb = (Rect) {
        .x = track->x,
        .y = track->y + (maximum > 0.0f ? offset / maximum * travel : 0.0f),
        .w = track->w,
        .h = thumb_height,
    };
    return true;
}

static bool point_in_scrollbar_hit_area(const ScrollView *view, float x,
                                        float y)
{
    Rect track;
    Rect thumb;
    if (!scrollbar_geometry(view, &track, &thumb)) return false;
    Rect hit_area = {
        .x = fmaxf(view->base.bounds.x,
                   view->base.bounds.x + view->base.bounds.w -
                   SCROLLBAR_HIT_WIDTH),
        .y = track.y,
        .w = fminf(SCROLLBAR_HIT_WIDTH, view->base.bounds.w),
        .h = track.h,
    };
    return point_in_rect(x, y, &hit_area);
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
    if (!view->content || !g_draw) return;
    if (view->content->draw) {
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

    Rect track;
    Rect thumb;
    if (scrollbar_geometry(view, &track, &thumb)) {
        static const uint8_t track_color[4] = {96, 101, 110, 30};
        uint8_t thumb_color[4] = {
            92, 98, 108, view->scrollbar_hovered || view->scrollbar_dragging
                ? 190 : 125
        };
        vk_draw_rounded_rect(g_draw, track.x, track.y + 2.0f, track.w,
                             fmaxf(0.0f, track.h - 4.0f), track.w * 0.5f,
                             0.5f, track_color);
        vk_draw_rounded_rect(g_draw, thumb.x, thumb.y, thumb.w, thumb.h,
                             thumb.w * 0.5f, 0.5f, thumb_color);
    }
    if (view->focused) {
        static const uint8_t focus_color[4] = {0, 122, 255, 190};
        const Rect *bounds = &self->bounds;
        vk_draw_rounded_rect(g_draw, bounds->x, bounds->y,
                             bounds->w, 2.0f, 1.0f, 0.5f, focus_color);
        vk_draw_rounded_rect(g_draw, bounds->x, bounds->y + bounds->h - 2.0f,
                             bounds->w, 2.0f, 1.0f, 0.5f, focus_color);
        vk_draw_rounded_rect(g_draw, bounds->x, bounds->y,
                             2.0f, bounds->h, 1.0f, 0.5f, focus_color);
        vk_draw_rounded_rect(g_draw, bounds->x + bounds->w - 2.0f, bounds->y,
                             2.0f, bounds->h, 1.0f, 0.5f, focus_color);
    }
}

static void scroll_view_event(Widget *self, const void *event)
{
    ScrollView *view = (ScrollView *)self;
    const SDL_Event *ev = event;
    if (!view->content || !ev) return;

    if (ev->type == SDL_EVENT_KEY_DOWN && view->focused) {
        float page_step = fmaxf(view->base.bounds.h -
                                view->view_style.wheel_step, 0.0f);
        switch (ev->key.key) {
        case SDLK_UP:
            (void)scroll_view_set_scroll_offset(
                view, view->position - view->view_style.wheel_step);
            return;
        case SDLK_DOWN:
            (void)scroll_view_set_scroll_offset(
                view, view->position + view->view_style.wheel_step);
            return;
        case SDLK_PAGEUP:
            (void)scroll_view_set_scroll_offset(view,
                                                view->position - page_step);
            return;
        case SDLK_PAGEDOWN:
            (void)scroll_view_set_scroll_offset(view,
                                                view->position + page_step);
            return;
        case SDLK_HOME:
            (void)scroll_view_set_scroll_offset(view, 0.0f);
            return;
        case SDLK_END:
            (void)scroll_view_set_scroll_offset(
                view, scroll_view_max_scroll(view));
            return;
        case SDLK_ESCAPE:
            view->focused = false;
            return;
        default:
            break;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        float x = ev->motion.x;
        float y = ev->motion.y;
        if (view->scrollbar_dragging) {
            Rect track;
            Rect thumb;
            if (scrollbar_geometry(view, &track, &thumb)) {
                float travel = track.h - thumb.h;
                float thumb_top = y - view->scrollbar_drag_offset;
                float fraction = travel > 0.0f
                    ? (thumb_top - track.y) / travel : 0.0f;
                (void)scroll_view_set_scroll_offset(
                    view, fraction * scroll_view_max_scroll(view));
            }
            return;
        }
        view->scrollbar_hovered = point_in_scrollbar_hit_area(view, x, y);
        if (view->scrollbar_hovered) return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT && view->scrollbar_dragging) {
        view->scrollbar_dragging = false;
        return;
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT &&
        point_in_scrollbar_hit_area(view, (float)ev->button.x,
                                    (float)ev->button.y)) {
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        view->focused = point_in_rect((float)ev->button.x,
                                      (float)ev->button.y, &self->bounds);
    }

    if (ev->type == SDL_EVENT_MOUSE_WHEEL &&
        point_in_rect(ev->wheel.mouse_x, ev->wheel.mouse_y, &self->bounds)) {
        float direction = ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED
            ? 1.0f : -1.0f;
        float delta = direction * ev->wheel.y * view->view_style.wheel_step;
        view->velocity += delta * SCROLL_FRICTION;
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        point_in_rect((float)ev->button.x, (float)ev->button.y,
                      &self->bounds) &&
        point_in_scrollbar_hit_area(view, (float)ev->button.x,
                                    (float)ev->button.y)) {
        Rect track;
        Rect thumb;
        if (!scrollbar_geometry(view, &track, &thumb)) return;
        float y = (float)ev->button.y;
        if (point_in_rect((float)ev->button.x, y, &thumb)) {
            view->scrollbar_dragging = true;
            view->scrollbar_drag_offset = y - thumb.y;
            view->velocity = 0.0f;
        } else {
            float direction = y < thumb.y ? -1.0f : 1.0f;
            (void)scroll_view_set_scroll_offset(
                view, view->position + direction * view->base.bounds.h);
        }
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

void scroll_view_set_focused(ScrollView *view, bool focused)
{
    if (view) view->focused = focused;
}

bool scroll_view_is_focused(const ScrollView *view)
{
    return view && view->focused;
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
