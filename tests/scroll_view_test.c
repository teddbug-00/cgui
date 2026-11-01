#include "scroll_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.01f;
}

int main(void)
{
    VkContext clip_context = {0};
    clip_context.frame_open = true;
    clip_context.swapchain_extent = (VkExtent2D){300, 200};
    VkDraw clip_draw = { .ctx = &clip_context };
    if (!vk_draw_clip_push(&clip_draw, 10.4f, 20.2f, 100.1f, 50.2f) ||
        clip_draw.clip_depth != 1 ||
        clip_draw.clip_stack[0].offset.x != 10 ||
        clip_draw.clip_stack[0].offset.y != 20 ||
        clip_draw.clip_stack[0].extent.width != 101 ||
        clip_draw.clip_stack[0].extent.height != 51 ||
        !vk_draw_clip_push(&clip_draw, 50.0f, 0.0f, 100.0f, 100.0f) ||
        clip_draw.clip_depth != 2 ||
        clip_draw.clip_stack[1].offset.x != 50 ||
        clip_draw.clip_stack[1].offset.y != 20 ||
        clip_draw.clip_stack[1].extent.width != 61 ||
        clip_draw.clip_stack[1].extent.height != 51) {
        fprintf(stderr, "Nested draw clipping did not intersect correctly\n");
        return 1;
    }
    vk_draw_clip_pop(&clip_draw);
    if (clip_draw.clip_depth != 1 ||
        vk_draw_clip_push(&clip_draw, 250.0f, 0.0f, 20.0f, 20.0f) ||
        clip_draw.clip_depth != 1) {
        fprintf(stderr, "Clip pop or off-screen clipping failed\n");
        return 1;
    }
    vk_draw_clip_pop(&clip_draw);

    ScrollView *view = scroll_view_create(
        (Rect){10.0f, 20.0f, 200.0f, 100.0f},
        scroll_view_style_default());
    Widget *content = widget_create();
    if (!view || !content ||
        !scroll_view_set_content(view, content, 300.0f)) {
        widget_destroy(scroll_view_widget(view));
        if (!content || !content->parent) widget_destroy(content);
        fprintf(stderr, "Failed to create scroll view content\n");
        return 1;
    }

    Widget *view_widget = scroll_view_widget(view);
    if (!near(scroll_view_max_scroll(view), 200.0f) ||
        !near(content->bounds.x, 10.0f) ||
        !near(content->bounds.y, 20.0f) ||
        !near(content->bounds.w, 200.0f) ||
        !near(content->bounds.h, 300.0f)) {
        fprintf(stderr, "Initial viewport/content bounds are incorrect\n");
        widget_destroy(view_widget);
        return 1;
    }

    SDL_Event wheel = {0};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.mouse_x = 50.0f;
    wheel.wheel.mouse_y = 50.0f;
    wheel.wheel.y = -1.0f;
    view_widget->handle_event(view_widget, &wheel);
    if (!near(scroll_view_scroll_offset(view), 0.0f)) {
        fprintf(stderr, "Wheel scrolling did not start smoothly\n");
        widget_destroy(view_widget);
        return 1;
    }
    scroll_view_update(view, 1.0f / 60.0f);
    float first_step = scroll_view_scroll_offset(view);
    if (!(first_step > 0.0f && first_step < 48.0f)) {
        fprintf(stderr, "Wheel motion did not interpolate smoothly\n");
        widget_destroy(view_widget);
        return 1;
    }
    for (int i = 0; i < 240; ++i) {
        scroll_view_update(view, 1.0f / 120.0f);
    }
    if (!near(scroll_view_scroll_offset(view), 48.0f) ||
        !near(content->bounds.y, -28.0f)) {
        fprintf(stderr, "Smooth wheel motion did not settle at its target\n");
        widget_destroy(view_widget);
        return 1;
    }

    SDL_Event pointer = {0};
    pointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    pointer.button.button = SDL_BUTTON_LEFT;
    pointer.button.x = 205;
    pointer.button.y = 115;
    view_widget->handle_event(view_widget, &pointer);
    if (!near(scroll_view_scroll_offset(view), 148.0f)) {
        fprintf(stderr, "Scrollbar track click did not page the content\n");
        widget_destroy(view_widget);
        return 1;
    }

    if (!scroll_view_set_scroll_offset(view, 0.0f)) {
        widget_destroy(view_widget);
        return 1;
    }
    pointer.button.y = 25;
    view_widget->handle_event(view_widget, &pointer);
    pointer.type = SDL_EVENT_MOUSE_MOTION;
    pointer.motion.x = 205.0f;
    pointer.motion.y = 85.0f;
    view_widget->handle_event(view_widget, &pointer);
    if (!(scroll_view_scroll_offset(view) > 170.0f)) {
        fprintf(stderr, "Dragging the scrollbar thumb did not track the pointer\n");
        widget_destroy(view_widget);
        return 1;
    }
    pointer.type = SDL_EVENT_MOUSE_BUTTON_UP;
    pointer.button.button = SDL_BUTTON_LEFT;
    pointer.button.x = 300;
    pointer.button.y = 300;
    view_widget->handle_event(view_widget, &pointer);
    float released_offset = scroll_view_scroll_offset(view);
    pointer.type = SDL_EVENT_MOUSE_MOTION;
    pointer.motion.x = 205.0f;
    pointer.motion.y = 40.0f;
    view_widget->handle_event(view_widget, &pointer);
    if (!near(scroll_view_scroll_offset(view), released_offset)) {
        fprintf(stderr, "Scrollbar continued dragging after pointer release\n");
        widget_destroy(view_widget);
        return 1;
    }
    if (!scroll_view_set_scroll_offset(view, 48.0f)) {
        widget_destroy(view_widget);
        return 1;
    }

    wheel.wheel.mouse_x = 300.0f;
    view_widget->handle_event(view_widget, &wheel);
    if (!near(scroll_view_scroll_offset(view), 48.0f)) {
        fprintf(stderr, "Wheel outside viewport changed scroll offset\n");
        widget_destroy(view_widget);
        return 1;
    }

    if (!scroll_view_set_scroll_offset(view, 1000.0f) ||
        !near(scroll_view_scroll_offset(view), 200.0f)) {
        fprintf(stderr, "Scroll offset was not clamped to content bounds\n");
        widget_destroy(view_widget);
        return 1;
    }
    view_widget->handle_event(view_widget, &wheel);
    if (!near(scroll_view_scroll_offset(view), 200.0f)) {
        fprintf(stderr, "Programmatic scroll position was not immediate\n");
        widget_destroy(view_widget);
        return 1;
    }
    wheel.wheel.mouse_x = 50.0f;
    wheel.wheel.y = -1.0f;
    view_widget->handle_event(view_widget, &wheel);
    for (int i = 0; i < 10; ++i) {
        scroll_view_update(view, 1.0f / 60.0f);
    }
    if (!(scroll_view_scroll_offset(view) > 200.0f)) {
        fprintf(stderr, "Bottom edge did not bounce on overscroll\n");
        widget_destroy(view_widget);
        return 1;
    }
    for (int i = 0; i < 60; ++i) scroll_view_update(view, 0.1f);
    if (!near(scroll_view_scroll_offset(view), 200.0f)) {
        fprintf(stderr, "Bottom edge bounce did not settle\n");
        widget_destroy(view_widget);
        return 1;
    }
    if (!scroll_view_set_scroll_offset(view, 0.0f)) {
        widget_destroy(view_widget);
        return 1;
    }
    wheel.wheel.y = 1.0f;
    view_widget->handle_event(view_widget, &wheel);
    for (int i = 0; i < 10; ++i) {
        scroll_view_update(view, 1.0f / 60.0f);
    }
    if (!(scroll_view_scroll_offset(view) < 0.0f)) {
        fprintf(stderr, "Top edge did not bounce on overscroll\n");
        widget_destroy(view_widget);
        return 1;
    }
    for (int i = 0; i < 60; ++i) scroll_view_update(view, 0.1f);
    if (!near(scroll_view_scroll_offset(view), 0.0f)) {
        fprintf(stderr, "Top edge bounce did not settle\n");
        widget_destroy(view_widget);
        return 1;
    }

    if (!scroll_view_set_scroll_offset(view, 200.0f)) {
        widget_destroy(view_widget);
        return 1;
    }
    widget_set_bounds(view_widget, (Rect){0.0f, 0.0f, 200.0f, 250.0f});
    if (!near(scroll_view_max_scroll(view), 50.0f) ||
        !near(scroll_view_scroll_offset(view), 50.0f) ||
        !near(content->bounds.y, -50.0f)) {
        fprintf(stderr, "Resize did not clamp and reposition scroll content\n");
        widget_destroy(view_widget);
        return 1;
    }
    if (!scroll_view_set_content_height(view, 100.0f) ||
        !near(scroll_view_max_scroll(view), 0.0f) ||
        !near(scroll_view_scroll_offset(view), 0.0f) ||
        !near(content->bounds.y, 0.0f)) {
        fprintf(stderr, "Content shrink did not reset the scroll position\n");
        widget_destroy(view_widget);
        return 1;
    }
    pointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    pointer.button.button = SDL_BUTTON_LEFT;
    pointer.button.x = 195;
    pointer.button.y = 240;
    view_widget->handle_event(view_widget, &pointer);
    if (!near(scroll_view_scroll_offset(view), 0.0f)) {
        fprintf(stderr, "Scrollbar remained interactive without overflow\n");
        widget_destroy(view_widget);
        return 1;
    }

    puts("ScrollView tests passed!");
    widget_destroy(view_widget);
    return 0;
}
