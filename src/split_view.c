/**
 * split_view.c - Two-pane layout with a draggable divider.
 */

#include "split_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

struct SplitView {
    Widget base;
    SplitViewStyle view_style;
    Widget *first;
    Widget *second;
    float ratio;
    float drag_offset;
    bool dragging;
    bool divider_hovered;
    SDL_Cursor *resize_cursor;
    size_t focused_pane;
};

static VkDraw *g_draw;
static SplitView *g_cursor_owner;

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

SplitViewStyle split_view_style_default(void)
{
    return (SplitViewStyle) {
        .orientation = SPLIT_VIEW_HORIZONTAL,
        .ratio = 0.3f,
        .divider_size = 8.0f,
        .first_min_size = 120.0f,
        .second_min_size = 160.0f,
        .divider_color = {222, 224, 228, 255},
        .divider_hover_color = {198, 201, 206, 255},
        .divider_active_color = {143, 148, 156, 255},
    };
}

static float split_view_axis_size(const SplitView *view)
{
    return view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
        ? view->base.bounds.w : view->base.bounds.h;
}

static float split_view_axis_origin(const SplitView *view)
{
    return view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
        ? view->base.bounds.x : view->base.bounds.y;
}

static float split_view_available_size(const SplitView *view)
{
    return fmaxf(0.0f, split_view_axis_size(view) -
                       view->view_style.divider_size);
}

static float split_view_first_size(const SplitView *view)
{
    float available = split_view_available_size(view);
    if (available <= 0.0f) return 0.0f;
    float first_min = fminf(view->view_style.first_min_size, available);
    float second_min = fminf(view->view_style.second_min_size,
                             available - first_min);
    float first = view->ratio * available;
    return fminf(fmaxf(first, first_min), available - second_min);
}

static Rect split_view_divider_bounds(const SplitView *view)
{
    float first_size = split_view_first_size(view);
    if (view->view_style.orientation == SPLIT_VIEW_HORIZONTAL) {
        return (Rect) {
            .x = view->base.bounds.x + first_size,
            .y = view->base.bounds.y,
            .w = fminf(view->view_style.divider_size,
                       view->base.bounds.w - first_size),
            .h = view->base.bounds.h,
        };
    }
    return (Rect) {
        .x = view->base.bounds.x,
        .y = view->base.bounds.y + first_size,
        .w = view->base.bounds.w,
        .h = fminf(view->view_style.divider_size,
                   view->base.bounds.h - first_size),
    };
}

static void split_view_layout(Widget *self)
{
    SplitView *view = (SplitView *)self;
    float first_size = split_view_first_size(view);
    float divider = fminf(view->view_style.divider_size,
                          split_view_axis_size(view));
    if (view->first) {
        if (view->view_style.orientation == SPLIT_VIEW_HORIZONTAL) {
            view->first->bounds = (Rect) {
                self->bounds.x, self->bounds.y, first_size, self->bounds.h,
            };
        } else {
            view->first->bounds = (Rect) {
                self->bounds.x, self->bounds.y, self->bounds.w, first_size,
            };
        }
        widget_layout(view->first);
    }
    if (view->second) {
        float second_size = fmaxf(
            0.0f, split_view_axis_size(view) - first_size - divider);
        if (view->view_style.orientation == SPLIT_VIEW_HORIZONTAL) {
            view->second->bounds = (Rect) {
                self->bounds.x + first_size + divider,
                self->bounds.y, second_size, self->bounds.h,
            };
        } else {
            view->second->bounds = (Rect) {
                self->bounds.x, self->bounds.y + first_size + divider,
                self->bounds.w, second_size,
            };
        }
        widget_layout(view->second);
    }
}

static void split_view_draw(Widget *self, struct VkContext *context)
{
    SplitView *view = (SplitView *)self;
    if (view->first && view->first->draw) {
        view->first->draw(view->first, context);
    }
    if (view->second && view->second->draw) {
        view->second->draw(view->second, context);
    }
    if (g_draw) {
        Rect divider = split_view_divider_bounds(view);
        bool horizontal =
            view->view_style.orientation == SPLIT_VIEW_HORIZONTAL;
        float line_size = 1.0f;
        float line_x = divider.x + (divider.w - line_size) * 0.5f;
        float line_y = divider.y + (divider.h - line_size) * 0.5f;
        vk_draw_rounded_rect(g_draw, line_x, line_y,
                             horizontal ? line_size : divider.w,
                             horizontal ? divider.h : line_size,
                             0.5f, 0.5f,
                             view->view_style.divider_color);

        if (view->divider_hovered || view->dragging) {
            const uint8_t *handle_color = view->dragging
                ? view->view_style.divider_active_color
                : view->view_style.divider_hover_color;
            float handle_length = fminf(28.0f,
                horizontal ? divider.h - 12.0f : divider.w - 12.0f);
            float handle_thickness = 4.0f;
            if (handle_length > 0.0f) {
                float handle_x = horizontal
                    ? divider.x + (divider.w - handle_thickness) * 0.5f
                    : divider.x + (divider.w - handle_length) * 0.5f;
                float handle_y = horizontal
                    ? divider.y + (divider.h - handle_length) * 0.5f
                    : divider.y + (divider.h - handle_thickness) * 0.5f;
                vk_draw_rounded_rect(g_draw, handle_x, handle_y,
                                     horizontal ? handle_thickness
                                                : handle_length,
                                     horizontal ? handle_length
                                                : handle_thickness,
                                     2.0f, 0.5f, handle_color);
                for (int i = -1; i <= 1; ++i) {
                    float dot_size = 2.0f;
                    float dot_x = horizontal
                        ? handle_x + (handle_thickness - dot_size) * 0.5f
                        : divider.x + divider.w * 0.5f - dot_size * 0.5f;
                    float dot_y = horizontal
                        ? divider.y + divider.h * 0.5f - dot_size * 0.5f
                              + (float)i * 5.0f
                        : handle_y + (handle_thickness - dot_size) * 0.5f;
                    if (!horizontal) dot_x += (float)i * 5.0f;
                    vk_draw_rounded_rect(g_draw, dot_x, dot_y,
                                         dot_size, dot_size, 1.0f, 0.5f,
                                         view->view_style.divider_active_color);
                }
            }
        }
    }
}

static void split_view_update_cursor(SplitView *view, bool hovering)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) return;
    if (hovering) {
        if (!view->resize_cursor) {
            SDL_SystemCursor shape =
                view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
                    ? SDL_SYSTEM_CURSOR_EW_RESIZE
                    : SDL_SYSTEM_CURSOR_NS_RESIZE;
            view->resize_cursor = SDL_CreateSystemCursor(shape);
            if (!view->resize_cursor) {
                fprintf(stderr, "Failed to create SplitView resize cursor: %s\n",
                        SDL_GetError());
                return;
            }
        }
        if (SDL_GetCursor() != view->resize_cursor) {
            if (!SDL_SetCursor(view->resize_cursor)) {
                fprintf(stderr, "Failed to set SplitView resize cursor: %s\n",
                        SDL_GetError());
                return;
            }
        }
        g_cursor_owner = view;
    } else if (g_cursor_owner) {
        if (SDL_GetCursor() == g_cursor_owner->resize_cursor &&
            !SDL_SetCursor(SDL_GetDefaultCursor())) {
            fprintf(stderr, "Failed to restore default cursor: %s\n",
                    SDL_GetError());
            return;
        }
        g_cursor_owner = NULL;
    }
}

static void split_view_destroy(Widget *self)
{
    SplitView *view = (SplitView *)self;
    if (view->resize_cursor) {
        if (g_cursor_owner == view) {
            if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
                split_view_update_cursor(view, false);
            } else {
                split_view_update_cursor(view, false);
            }
            g_cursor_owner = NULL;
        }
        SDL_DestroyCursor(view->resize_cursor);
    }
}

static void split_view_update_drag(SplitView *view, float coordinate)
{
    float available = split_view_available_size(view);
    if (available <= 0.0f) return;
    float first_size = coordinate - view->drag_offset -
                       split_view_axis_origin(view);
    float first_min = fminf(view->view_style.first_min_size, available);
    float second_min = fminf(view->view_style.second_min_size,
                             available - first_min);
    first_size = fminf(fmaxf(first_size, first_min),
                       available - second_min);
    view->ratio = first_size / available;
    split_view_layout(&view->base);
}

static size_t split_view_pane_at(const SplitView *view, float x, float y)
{
    if (view->first && point_in_rect(x, y, &view->first->bounds)) return 0;
    if (view->second && point_in_rect(x, y, &view->second->bounds)) return 1;
    return SIZE_MAX;
}

static void split_view_event(Widget *self, const void *event)
{
    SplitView *view = (SplitView *)self;
    const SDL_Event *ev = event;
    if (!ev) return;

    Rect divider = split_view_divider_bounds(view);
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        float x = ev->motion.x;
        float y = ev->motion.y;
        view->divider_hovered = point_in_rect(x, y, &divider);
        split_view_update_cursor(view, view->divider_hovered || view->dragging);
        if (view->dragging) {
            split_view_update_drag(
                view, view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
                    ? x : y);
            return;
        }
        size_t pane = split_view_pane_at(view, x, y);
        if (pane != SIZE_MAX) {
            Widget *child = pane == 0 ? view->first : view->second;
            if (child->handle_event) child->handle_event(child, event);
        }
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        float x = (float)ev->button.x;
        float y = (float)ev->button.y;
        if (point_in_rect(x, y, &divider)) {
            view->dragging = true;
            view->divider_hovered = true;
            view->drag_offset =
                view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
                    ? x - divider.x : y - divider.y;
            split_view_update_cursor(view, true);
            return;
        }
        size_t pane = split_view_pane_at(view, x, y);
        if (pane != SIZE_MAX) {
            view->focused_pane = pane;
            Widget *child = pane == 0 ? view->first : view->second;
            if (child->handle_event) child->handle_event(child, event);
        }
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT) {
        if (view->dragging) {
            split_view_update_drag(
                view,
                view->view_style.orientation == SPLIT_VIEW_HORIZONTAL
                    ? (float)ev->button.x : (float)ev->button.y);
            view->dragging = false;
            divider = split_view_divider_bounds(view);
            view->divider_hovered = point_in_rect(
                (float)ev->button.x, (float)ev->button.y, &divider);
            split_view_update_cursor(view, view->divider_hovered);
            return;
        }
        size_t pane = split_view_pane_at(
            view, (float)ev->button.x, (float)ev->button.y);
        if (pane != SIZE_MAX) {
            Widget *child = pane == 0 ? view->first : view->second;
            if (child->handle_event) child->handle_event(child, event);
        }
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        size_t pane = split_view_pane_at(view, ev->wheel.mouse_x,
                                         ev->wheel.mouse_y);
        if (pane != SIZE_MAX) {
            Widget *child = pane == 0 ? view->first : view->second;
            if (child->handle_event) child->handle_event(child, event);
        }
        return;
    }

    Widget *focused = view->focused_pane == 0 ? view->first : view->second;
    if (focused && focused->handle_event) {
        focused->handle_event(focused, event);
    }
}

static bool valid_style(SplitViewStyle style)
{
    return (style.orientation == SPLIT_VIEW_HORIZONTAL ||
            style.orientation == SPLIT_VIEW_VERTICAL) &&
           isfinite(style.ratio) && style.ratio > 0.0f && style.ratio < 1.0f &&
           isfinite(style.divider_size) && style.divider_size > 0.0f &&
           isfinite(style.first_min_size) && style.first_min_size >= 0.0f &&
           isfinite(style.second_min_size) && style.second_min_size >= 0.0f;
}

SplitView *split_view_create(Rect bounds, const SplitViewStyle *style)
{
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h) ||
        bounds.w < 0.0f || bounds.h < 0.0f) {
        return NULL;
    }
    SplitViewStyle resolved = style ? *style : split_view_style_default();
    if (!valid_style(resolved)) return NULL;

    SplitView *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->base.bounds = bounds;
    view->base.style = style_default();
    view->base.draw = split_view_draw;
    view->base.handle_event = split_view_event;
    view->base.destroy = split_view_destroy;
    view->base.layout = split_view_layout;
    view->view_style = resolved;
    view->ratio = resolved.ratio;
    return view;
}

Widget *split_view_widget(SplitView *view)
{
    return view ? &view->base : NULL;
}

bool split_view_set_panes(SplitView *view, Widget *first, Widget *second)
{
    if (!view || !first || !second || first == second ||
        first->parent || second->parent || view->first || view->second) {
        return false;
    }
    if (!widget_add_child(&view->base, first)) return false;
    if (!widget_add_child(&view->base, second)) {
        (void)widget_remove_child(&view->base, first);
        return false;
    }
    view->first = first;
    view->second = second;
    split_view_layout(&view->base);
    return true;
}

bool split_view_set_ratio(SplitView *view, float ratio)
{
    if (!view || !isfinite(ratio) || ratio <= 0.0f || ratio >= 1.0f)
        return false;
    view->ratio = ratio;
    split_view_layout(&view->base);
    return true;
}

float split_view_ratio(const SplitView *view)
{
    return view ? view->ratio : 0.0f;
}

Widget *split_view_first_pane(const SplitView *view)
{
    return view ? view->first : NULL;
}

Widget *split_view_second_pane(const SplitView *view)
{
    return view ? view->second : NULL;
}

void split_view_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
