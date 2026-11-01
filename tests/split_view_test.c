#include "split_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TestPane {
    Widget base;
    size_t events;
} TestPane;

static void pane_event(Widget *widget, const void *event)
{
    (void)event;
    ++((TestPane *)widget)->events;
}

static TestPane *pane_create(void)
{
    TestPane *pane = calloc(1, sizeof(*pane));
    if (!pane) return NULL;
    pane->base.style = style_default();
    pane->base.draw = widget_draw_noop;
    pane->base.handle_event = pane_event;
    pane->base.destroy = widget_destroy_noop;
    return pane;
}

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.02f;
}

int main(void)
{
    SplitViewStyle style = split_view_style_default();
    style.ratio = 0.4f;
    style.divider_size = 8.0f;
    style.first_min_size = 100.0f;
    style.second_min_size = 120.0f;
    SplitView *view = split_view_create(
        (Rect){10.0f, 20.0f, 410.0f, 300.0f}, &style);
    TestPane *first = pane_create();
    TestPane *second = pane_create();
    if (!view || !first || !second ||
        !split_view_set_panes(view, (Widget *)first, (Widget *)second)) {
        fprintf(stderr, "Failed to create horizontal SplitView\n");
        widget_destroy(split_view_widget(view));
        widget_destroy((Widget *)first);
        widget_destroy((Widget *)second);
        return 1;
    }

    if (!near(first->base.bounds.x, 10.0f) ||
        !near(first->base.bounds.y, 20.0f) ||
        !near(first->base.bounds.w, 160.8f) ||
        !near(first->base.bounds.h, 300.0f) ||
        !near(second->base.bounds.x, 178.8f) ||
        !near(second->base.bounds.w, 241.2f) ||
        !near(split_view_ratio(view), 0.4f) ||
        split_view_first_pane(view) != (Widget *)first ||
        split_view_second_pane(view) != (Widget *)second) {
        fprintf(stderr, "Horizontal pane layout or accessors are incorrect\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }

    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 174;
    event.button.y = 100;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 233.2f;
    event.motion.y = 100.0f;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 233;
    event.button.y = 100;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    if (!near(first->base.bounds.w, 219.8f) ||
        !near(second->base.bounds.x, 237.8f) ||
        !(split_view_ratio(view) > 0.5f && split_view_ratio(view) < 0.6f)) {
        fprintf(stderr, "Dragging the horizontal divider did not resize panes\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.x = 238;
    event.button.y = 100;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    if (second->events != 1 || first->events != 0) {
        fprintf(stderr, "Mouse input was not routed to the pane under pointer\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }

    if (split_view_set_ratio(view, 0.0f) ||
        split_view_set_ratio(view, 1.0f) ||
        split_view_set_ratio(view, NAN)) {
        fprintf(stderr, "SplitView accepted an invalid ratio\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.x = 234;
    event.button.y = 100;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = -100.0f;
    event.motion.y = 100.0f;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    if (!near(first->base.bounds.w, 100.0f)) {
        fprintf(stderr, "Dragging violated the first pane minimum size\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.x = 114;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 900.0f;
    split_view_widget(view)->handle_event(split_view_widget(view), &event);
    if (!near(second->base.bounds.w, 120.0f)) {
        fprintf(stderr, "Dragging violated the second pane minimum size\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }
    widget_set_bounds(split_view_widget(view),
                      (Rect){0.0f, 0.0f, 50.0f, 100.0f});
    if (!near(first->base.bounds.w + second->base.bounds.w, 42.0f) ||
        first->base.bounds.w < 0.0f || second->base.bounds.w < 0.0f) {
        fprintf(stderr, "Narrow SplitView produced invalid pane sizes\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }
    widget_destroy(split_view_widget(view));

    style.orientation = SPLIT_VIEW_VERTICAL;
    style.ratio = 0.5f;
    style.first_min_size = 50.0f;
    style.second_min_size = 60.0f;
    view = split_view_create((Rect){5.0f, 7.0f, 300.0f, 207.0f}, &style);
    first = pane_create();
    second = pane_create();
    if (!view || !first || !second ||
        !split_view_set_panes(view, (Widget *)first, (Widget *)second) ||
        !near(first->base.bounds.h, 99.5f) ||
        !near(second->base.bounds.y, 114.5f) ||
        !near(second->base.bounds.h, 99.5f)) {
        fprintf(stderr, "Vertical SplitView layout is incorrect\n");
        widget_destroy(split_view_widget(view));
        widget_destroy((Widget *)first);
        widget_destroy((Widget *)second);
        return 1;
    }
    widget_set_bounds(split_view_widget(view),
                      (Rect){0.0f, 0.0f, 300.0f, 307.0f});
    if (!near(first->base.bounds.h, 149.5f) ||
        !near(second->base.bounds.y, 157.5f) ||
        !near(second->base.bounds.h, 149.5f)) {
        fprintf(stderr, "Resizing vertical SplitView did not relayout panes\n");
        widget_destroy(split_view_widget(view));
        return 1;
    }
    widget_destroy(split_view_widget(view));

    style.orientation = (SplitViewOrientation)99;
    if (split_view_create((Rect){0}, &style)) {
        fprintf(stderr, "SplitView accepted an invalid orientation\n");
        return 1;
    }
    puts("SplitView tests passed!");
    return 0;
}
