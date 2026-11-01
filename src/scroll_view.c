/**
 * scroll_view.c - Clipped two-axis scrolling viewport.
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
    float content_width;
    float content_height;
    float position_x;
    float position_y;
    float velocity_x;
    float velocity_y;
    float scrollbar_drag_offset;
    float scrollbar_visible_time;
    int scrollbar_drag_axis;
    bool scrollbar_hovered_x;
    bool scrollbar_hovered_y;
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
#define SCROLLBAR_FADE_DELAY 0.8f
#define SCROLLBAR_FADE_DURATION 0.3f
#define SCROLLBAR_VERTICAL 1
#define SCROLLBAR_HORIZONTAL 2

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

static void reveal_scrollbar(ScrollView *view)
{
    view->scrollbar_visible_time =
        SCROLLBAR_FADE_DELAY + SCROLLBAR_FADE_DURATION;
}

static float scrollbar_opacity(const ScrollView *view)
{
    if (view->scrollbar_hovered_x || view->scrollbar_hovered_y ||
        view->scrollbar_drag_axis != 0) return 1.0f;
    if (view->scrollbar_visible_time >= SCROLLBAR_FADE_DURATION) return 1.0f;
    return fmaxf(0.0f, view->scrollbar_visible_time /
                        SCROLLBAR_FADE_DURATION);
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

float scroll_view_max_scroll_x(const ScrollView *view)
{
    if (!view) return 0.0f;
    return fmaxf(0.0f, view->content_width - view->base.bounds.w);
}

static float rubber_band(float distance, float dimension)
{
    if (distance <= 0.0f || dimension <= 0.0f) return 0.0f;
    return dimension * (1.0f - 1.0f /
        (distance * SCROLL_RUBBER_BAND_FACTOR / dimension + 1.0f));
}

static float displayed_axis_offset(float position, float maximum,
                                   float dimension)
{
    if (position < 0.0f) {
        return -rubber_band(-position, dimension);
    }
    if (position > maximum) {
        return maximum + rubber_band(position - maximum, dimension);
    }
    return position;
}

static bool scrollbar_geometry(const ScrollView *view, int axis, Rect *track,
                               Rect *thumb)
{
    bool horizontal = axis == SCROLLBAR_HORIZONTAL;
    float viewport = horizontal ? view->base.bounds.w : view->base.bounds.h;
    float content = horizontal ? view->content_width : view->content_height;
    float maximum = horizontal ? scroll_view_max_scroll_x(view)
                               : scroll_view_max_scroll(view);
    float position = horizontal ? view->position_x : view->position_y;
    float cross_viewport = horizontal ? view->base.bounds.h
                                      : view->base.bounds.w;
    if (maximum <= 0.0f || viewport <= 0.0f || cross_viewport <= 0.0f) {
        return false;
    }

    if (horizontal) {
        float track_width = viewport;
        if (scroll_view_max_scroll(view) > 0.0f) {
            track_width = fmaxf(0.0f, track_width - SCROLLBAR_HIT_WIDTH);
        }
        float thumb_length = fmaxf(SCROLLBAR_MIN_THUMB_HEIGHT,
                                   track_width * viewport / content);
        thumb_length = fminf(thumb_length, track_width);
        float travel = track_width - thumb_length;
        float thumb_offset = maximum > 0.0f
            ? fminf(maximum, fmaxf(0.0f, position)) / maximum * travel : 0.0f;
        *track = (Rect) {
            .x = view->base.bounds.x,
            .y = view->base.bounds.y + view->base.bounds.h -
                 fminf(SCROLLBAR_WIDTH, cross_viewport) - 3.0f,
            .w = track_width,
            .h = fminf(SCROLLBAR_WIDTH, cross_viewport),
        };
        *thumb = (Rect) {
            .x = track->x + thumb_offset,
            .y = track->y,
            .w = fminf(thumb_length, track->w),
            .h = track->h,
        };
    } else {
        float track_height = viewport;
        if (scroll_view_max_scroll_x(view) > 0.0f) {
            track_height = fmaxf(0.0f, track_height - SCROLLBAR_HIT_WIDTH);
        }
        float thumb_length = fmaxf(SCROLLBAR_MIN_THUMB_HEIGHT,
                                   track_height * viewport / content);
        thumb_length = fminf(thumb_length, track_height);
        float travel = track_height - thumb_length;
        float thumb_offset = maximum > 0.0f
            ? fminf(maximum, fmaxf(0.0f, position)) / maximum * travel : 0.0f;
        *track = (Rect) {
            .x = view->base.bounds.x + view->base.bounds.w -
                 fminf(SCROLLBAR_WIDTH, cross_viewport) - 3.0f,
            .y = view->base.bounds.y,
            .w = fminf(SCROLLBAR_WIDTH, cross_viewport),
            .h = track_height,
        };
        *thumb = (Rect) {
            .x = track->x,
            .y = track->y + thumb_offset,
            .w = track->w,
            .h = fminf(thumb_length, track->h),
        };
    }
    return true;
}

static bool point_in_scrollbar_hit_area(const ScrollView *view, int axis,
                                        float x, float y)
{
    Rect track;
    Rect thumb;
    if (!scrollbar_geometry(view, axis, &track, &thumb)) return false;
    Rect hit_area;
    if (axis == SCROLLBAR_HORIZONTAL) {
        hit_area = (Rect) {
            .x = view->base.bounds.x,
            .y = view->base.bounds.y + view->base.bounds.h -
                 fminf(SCROLLBAR_HIT_WIDTH, view->base.bounds.h),
            .w = track.w,
            .h = fminf(SCROLLBAR_HIT_WIDTH, view->base.bounds.h),
        };
    } else {
        hit_area = (Rect) {
            .x = view->base.bounds.x + view->base.bounds.w -
                 fminf(SCROLLBAR_HIT_WIDTH, view->base.bounds.w),
            .y = view->base.bounds.y,
            .w = fminf(SCROLLBAR_HIT_WIDTH, view->base.bounds.w),
            .h = track.h,
        };
    }
    return point_in_rect(x, y, &hit_area);
}

static void update_content_bounds(ScrollView *view)
{
    if (!view || !view->content) return;
    view->content->bounds = (Rect) {
        .x = view->base.bounds.x - displayed_axis_offset(
            view->position_x, scroll_view_max_scroll_x(view),
            view->base.bounds.w),
        .y = view->base.bounds.y - displayed_axis_offset(
            view->position_y, scroll_view_max_scroll(view),
            view->base.bounds.h),
        .w = fmaxf(view->content_width, view->base.bounds.w),
        .h = fmaxf(view->content_height, view->base.bounds.h),
    };
    widget_layout(view->content);
}

static void scroll_view_layout(Widget *self)
{
    ScrollView *view = (ScrollView *)self;
    view->position_x = fminf(scroll_view_max_scroll_x(view),
                             fmaxf(0.0f, view->position_x));
    view->position_y = fminf(scroll_view_max_scroll(view),
                             fmaxf(0.0f, view->position_y));
    view->velocity_x = 0.0f;
    view->velocity_y = 0.0f;
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

    float opacity = scrollbar_opacity(view);
    if (opacity > 0.0f) {
        for (int axis = SCROLLBAR_VERTICAL;
             axis <= SCROLLBAR_HORIZONTAL; ++axis) {
            Rect track;
            Rect thumb;
            if (!scrollbar_geometry(view, axis, &track, &thumb)) continue;
            bool horizontal = axis == SCROLLBAR_HORIZONTAL;
            bool active = view->scrollbar_drag_axis == axis ||
                (horizontal ? view->scrollbar_hovered_x
                            : view->scrollbar_hovered_y);
            uint8_t track_color[4] = {96, 101, 110,
                (uint8_t)(30.0f * opacity)};
            uint8_t thumb_color[4] = {
                92, 98, 108, (uint8_t)((active ? 190.0f : 125.0f) * opacity)
            };
            float width = active ? 8.0f : SCROLLBAR_WIDTH;
            if (horizontal) {
                float y = view->base.bounds.y + view->base.bounds.h -
                          width - 2.0f;
                if (y < view->base.bounds.y) y = view->base.bounds.y;
                vk_draw_rounded_rect(g_draw, track.x + 2.0f, y,
                                     fmaxf(0.0f, track.w - 4.0f), width,
                                     width * 0.5f, 0.5f, track_color);
                vk_draw_rounded_rect(g_draw, thumb.x, y, thumb.w, width,
                                     width * 0.5f, 0.5f, thumb_color);
            } else {
                float x = view->base.bounds.x + view->base.bounds.w -
                          width - 2.0f;
                if (x < view->base.bounds.x) x = view->base.bounds.x;
                vk_draw_rounded_rect(g_draw, x, track.y + 2.0f, width,
                                     fmaxf(0.0f, track.h - 4.0f),
                                     width * 0.5f, 0.5f, track_color);
                vk_draw_rounded_rect(g_draw, x, thumb.y, width, thumb.h,
                                     width * 0.5f, 0.5f, thumb_color);
            }
        }
    }
    if (view->focused) {
        static const uint8_t focus_color[4] = {0, 122, 255, 220};
        const Rect *bounds = &self->bounds;
        float length = fminf(12.0f, fminf(bounds->w, bounds->h) * 0.2f);
        float thickness = fminf(2.0f, length * 0.18f);
        float inset = 2.0f;
        if (length > thickness && thickness > 0.0f) {
            float left = bounds->x + inset;
            float right = bounds->x + bounds->w - inset - length;
            float top = bounds->y + inset;
            float bottom = bounds->y + bounds->h - inset - length;
            float radius = thickness * 0.5f;
            vk_draw_rounded_rect(g_draw, left, top, length, thickness,
                                 radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, left, top, thickness, length,
                                 radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, right, top, length, thickness,
                                 radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, right + length - thickness, top,
                                 thickness, length, radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, left, bottom + length - thickness,
                                 length, thickness, radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, left, bottom, thickness, length,
                                 radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, right, bottom + length - thickness,
                                 length, thickness, radius, 0.5f, focus_color);
            vk_draw_rounded_rect(g_draw, right + length - thickness, bottom,
                                 thickness, length, radius, 0.5f, focus_color);
        }
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
                view, view->position_y - view->view_style.wheel_step);
            return;
        case SDLK_DOWN:
            (void)scroll_view_set_scroll_offset(
                view, view->position_y + view->view_style.wheel_step);
            return;
        case SDLK_LEFT:
            (void)scroll_view_set_scroll_offset_x(
                view, view->position_x - view->view_style.wheel_step);
            return;
        case SDLK_RIGHT:
            (void)scroll_view_set_scroll_offset_x(
                view, view->position_x + view->view_style.wheel_step);
            return;
        case SDLK_PAGEUP:
            (void)scroll_view_set_scroll_offset(view,
                                                view->position_y - page_step);
            return;
        case SDLK_PAGEDOWN:
            (void)scroll_view_set_scroll_offset(view,
                                                view->position_y + page_step);
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
        if (view->scrollbar_drag_axis != 0) {
            int axis = view->scrollbar_drag_axis;
            Rect track;
            Rect thumb;
            if (scrollbar_geometry(view, axis, &track, &thumb)) {
                bool horizontal = axis == SCROLLBAR_HORIZONTAL;
                float travel = horizontal ? track.w - thumb.w
                                          : track.h - thumb.h;
                float pointer = horizontal ? x : y;
                float origin = horizontal ? track.x : track.y;
                float thumb_top = pointer - view->scrollbar_drag_offset;
                float fraction = travel > 0.0f
                    ? (thumb_top - origin) / travel : 0.0f;
                if (horizontal) {
                    (void)scroll_view_set_scroll_offset_x(
                        view, fraction * scroll_view_max_scroll_x(view));
                } else {
                    (void)scroll_view_set_scroll_offset(
                        view, fraction * scroll_view_max_scroll(view));
                }
            }
            return;
        }
        view->scrollbar_hovered_y = point_in_scrollbar_hit_area(
            view, SCROLLBAR_VERTICAL, x, y);
        view->scrollbar_hovered_x = point_in_scrollbar_hit_area(
            view, SCROLLBAR_HORIZONTAL, x, y);
        if (view->scrollbar_hovered_x || view->scrollbar_hovered_y) {
            reveal_scrollbar(view);
            return;
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT &&
        view->scrollbar_drag_axis != 0) {
        view->scrollbar_drag_axis = 0;
        return;
    }
    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT &&
        (point_in_scrollbar_hit_area(view, SCROLLBAR_VERTICAL,
                                     (float)ev->button.x,
                                     (float)ev->button.y) ||
         point_in_scrollbar_hit_area(view, SCROLLBAR_HORIZONTAL,
                                     (float)ev->button.x,
                                     (float)ev->button.y))) {
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
            ? -1.0f : 1.0f;
        if (scroll_view_max_scroll_x(view) > 0.0f) {
            view->velocity_x += direction * ev->wheel.x *
                                view->view_style.wheel_step * SCROLL_FRICTION;
        }
        if (scroll_view_max_scroll(view) > 0.0f) {
            view->velocity_y -= direction * ev->wheel.y *
                                view->view_style.wheel_step * SCROLL_FRICTION;
        }
        reveal_scrollbar(view);
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        point_in_rect((float)ev->button.x, (float)ev->button.y,
                      &self->bounds) &&
        (point_in_scrollbar_hit_area(view, SCROLLBAR_VERTICAL,
                                     (float)ev->button.x,
                                     (float)ev->button.y) ||
         point_in_scrollbar_hit_area(view, SCROLLBAR_HORIZONTAL,
                                     (float)ev->button.x,
                                     (float)ev->button.y))) {
        int axis = point_in_scrollbar_hit_area(
            view, SCROLLBAR_VERTICAL, (float)ev->button.x,
            (float)ev->button.y) ? SCROLLBAR_VERTICAL
                                 : SCROLLBAR_HORIZONTAL;
        reveal_scrollbar(view);
        Rect track;
        Rect thumb;
        if (!scrollbar_geometry(view, axis, &track, &thumb)) return;
        bool horizontal = axis == SCROLLBAR_HORIZONTAL;
        float pointer = horizontal ? (float)ev->button.x
                                   : (float)ev->button.y;
        float thumb_start = horizontal ? thumb.x : thumb.y;
        float viewport = horizontal ? view->base.bounds.w
                                    : view->base.bounds.h;
        float position = horizontal ? view->position_x : view->position_y;
        float offset = pointer - thumb_start;
        if (point_in_rect((float)ev->button.x, (float)ev->button.y, &thumb)) {
            view->scrollbar_drag_axis = axis;
            view->scrollbar_drag_offset = offset;
            if (horizontal) view->velocity_x = 0.0f;
            else view->velocity_y = 0.0f;
        } else {
            float direction = pointer < thumb_start ? -1.0f : 1.0f;
            if (horizontal) {
                (void)scroll_view_set_scroll_offset_x(
                    view, position + direction * viewport);
            } else {
                (void)scroll_view_set_scroll_offset(
                    view, position + direction * viewport);
            }
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
    view->content_width = bounds.w;
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
    view->content_width = view->base.bounds.w;
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
    view->position_y = fminf(view->position_y, scroll_view_max_scroll(view));
    view->velocity_y = 0.0f;
    update_content_bounds(view);
    return true;
}

bool scroll_view_set_content_width(ScrollView *view, float content_width)
{
    if (!view || !view->content || !isfinite(content_width) ||
        content_width < 0.0f) {
        return false;
    }
    view->content_width = content_width;
    view->position_x = fminf(view->position_x,
                             scroll_view_max_scroll_x(view));
    view->velocity_x = 0.0f;
    update_content_bounds(view);
    return true;
}

bool scroll_view_set_scroll_offset(ScrollView *view, float offset)
{
    if (!view || !isfinite(offset)) return false;
    float maximum = scroll_view_max_scroll(view);
    view->position_y = fminf(maximum, fmaxf(0.0f, offset));
    view->velocity_y = 0.0f;
    reveal_scrollbar(view);
    update_content_bounds(view);
    return true;
}

bool scroll_view_set_scroll_offset_x(ScrollView *view, float offset)
{
    if (!view || !isfinite(offset)) return false;
    float maximum = scroll_view_max_scroll_x(view);
    view->position_x = fminf(maximum, fmaxf(0.0f, offset));
    view->velocity_x = 0.0f;
    reveal_scrollbar(view);
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

static void update_scroll_axis(float *position, float *velocity,
                               float maximum, float dt)
{
    if (*position < 0.0f) {
        float acceleration = -SCROLL_SPRING_STIFFNESS * *position -
                            SCROLL_SPRING_DAMPING * *velocity;
        *velocity += acceleration * dt;
        *position += *velocity * dt;
    } else if (*position > maximum) {
        float displacement = *position - maximum;
        float acceleration = -SCROLL_SPRING_STIFFNESS * displacement -
                            SCROLL_SPRING_DAMPING * *velocity;
        *velocity += acceleration * dt;
        *position += *velocity * dt;
    } else {
        float decay = expf(-SCROLL_FRICTION * dt);
        *position += *velocity * (1.0f - decay) / SCROLL_FRICTION;
        *velocity *= decay;
    }

    if (*position < 0.0f && fabsf(*position) < 0.01f) {
        *position = 0.0f;
        *velocity = 0.0f;
    } else if (*position > maximum &&
               fabsf(*position - maximum) < 0.01f) {
        *position = maximum;
        *velocity = 0.0f;
    }
    if (fabsf(*velocity) < 0.05f &&
        *position >= 0.0f && *position <= maximum) {
        *velocity = 0.0f;
    }
}

void scroll_view_update(ScrollView *view, float delta_seconds)
{
    if (!view || !view->content || !isfinite(delta_seconds) ||
        delta_seconds <= 0.0f) {
        return;
    }

    if (!view->scrollbar_hovered_x && !view->scrollbar_hovered_y &&
        view->scrollbar_drag_axis == 0) {
        view->scrollbar_visible_time =
            fmaxf(0.0f, view->scrollbar_visible_time - delta_seconds);
    }

    float remaining = fminf(delta_seconds, 0.1f);
    while (remaining > 0.0f) {
        float dt = fminf(remaining, 1.0f / 120.0f);
        float maximum_x = scroll_view_max_scroll_x(view);
        float maximum_y = scroll_view_max_scroll(view);
        if (view->scrollbar_drag_axis != SCROLLBAR_HORIZONTAL) {
            update_scroll_axis(&view->position_y, &view->velocity_y,
                               maximum_y, dt);
        }
        if (view->scrollbar_drag_axis != SCROLLBAR_VERTICAL) {
            update_scroll_axis(&view->position_x, &view->velocity_x,
                               maximum_x, dt);
        }
        remaining -= dt;
    }

    update_content_bounds(view);
}

float scroll_view_scroll_offset(const ScrollView *view)
{
    return view ? displayed_axis_offset(view->position_y,
        scroll_view_max_scroll(view), view->base.bounds.h) : 0.0f;
}

float scroll_view_scroll_offset_x(const ScrollView *view)
{
    return view ? displayed_axis_offset(view->position_x,
        scroll_view_max_scroll_x(view), view->base.bounds.w) : 0.0f;
}

void scroll_view_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
