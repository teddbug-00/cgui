/**
 * layout.c – Row and column layout using child Style width/height hints.
 */

#include "layout.h"

#include <math.h>
#include <stdlib.h>

struct LayoutContainer {
    Widget base;
    LayoutStyle layout_style;
};

static void layout_widget_reflow(Widget *self)
{
    layout_container_reflow((LayoutContainer *)self);
}

static void layout_draw(Widget *self, struct VkContext *context)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) child->draw(child, context);
    }
}

static void layout_event(Widget *self, const void *event)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->handle_event) child->handle_event(child, event);
    }
}

LayoutStyle layout_style_default(LayoutDirection direction)
{
    return (LayoutStyle) {
        .direction = direction,
        .cross_align = LAYOUT_ALIGN_FILL,
        .spacing = 0.0f,
        .padding = 0.0f,
    };
}

static bool layout_style_valid(const LayoutStyle *style)
{
    return style &&
           (style->direction == LAYOUT_HORIZONTAL ||
            style->direction == LAYOUT_VERTICAL) &&
           style->cross_align >= LAYOUT_ALIGN_FILL &&
           style->cross_align <= LAYOUT_ALIGN_END &&
           isfinite(style->spacing) && style->spacing >= 0.0f &&
           isfinite(style->padding) && style->padding >= 0.0f;
}

LayoutContainer *layout_container_create(Rect bounds, LayoutStyle style)
{
    if (!layout_style_valid(&style)) return NULL;
    LayoutContainer *layout = calloc(1, sizeof(*layout));
    if (!layout) return NULL;

    layout->base.bounds = bounds;
    layout->base.style = style_default();
    layout->base.draw = layout_draw;
    layout->base.handle_event = layout_event;
    layout->base.destroy = widget_destroy_noop;
    layout->base.layout = layout_widget_reflow;
    layout->layout_style = style;
    return layout;
}

Widget *layout_container_widget(LayoutContainer *layout)
{
    return layout ? &layout->base : NULL;
}

bool layout_container_add_child(LayoutContainer *layout, Widget *child)
{
    return layout && widget_add_child(&layout->base, child);
}

bool layout_container_remove_child(LayoutContainer *layout, Widget *child)
{
    return layout && widget_remove_child(&layout->base, child);
}

bool layout_container_set_style(LayoutContainer *layout,
                                const LayoutStyle *style)
{
    if (!layout || !layout_style_valid(style)) return false;
    layout->layout_style = *style;
    widget_layout((Widget *)layout);
    return true;
}

void layout_container_reflow(LayoutContainer *layout)
{
    if (!layout) return;
    Widget *base = &layout->base;
    const LayoutStyle *style = &layout->layout_style;
    size_t count = base->child_count;
    if (count == 0) return;

    bool horizontal = style->direction == LAYOUT_HORIZONTAL;
    float main_extent = horizontal ? base->bounds.w : base->bounds.h;
    float cross_extent = horizontal ? base->bounds.h : base->bounds.w;
    float content_main = fmaxf(0.0f, main_extent - 2.0f * style->padding);
    float content_cross = fmaxf(0.0f, cross_extent - 2.0f * style->padding);
    float gaps = style->spacing * (float)(count - 1);
    float remaining = fmaxf(0.0f, content_main - gaps);
    size_t flexible_count = 0;

    for (size_t i = 0; i < count; ++i) {
        Widget *child = base->children[i];
        float requested = horizontal ? child->style.width : child->style.height;
        if (isfinite(requested) && requested > 0.0f) {
            remaining -= requested;
        } else {
            ++flexible_count;
        }
    }
    float flexible_size = flexible_count
        ? fmaxf(0.0f, remaining) / (float)flexible_count : 0.0f;

    float cursor = (horizontal ? base->bounds.x : base->bounds.y) +
                   style->padding;
    float cross_start = (horizontal ? base->bounds.y : base->bounds.x) +
                        style->padding;

    for (size_t i = 0; i < count; ++i) {
        Widget *child = base->children[i];
        float requested_main = horizontal
            ? child->style.width : child->style.height;
        float requested_cross = horizontal
            ? child->style.height : child->style.width;
        float child_main = isfinite(requested_main) && requested_main > 0.0f
            ? requested_main : flexible_size;
        float child_cross = style->cross_align != LAYOUT_ALIGN_FILL &&
                            isfinite(requested_cross) && requested_cross > 0.0f
            ? fminf(requested_cross, content_cross) : content_cross;
        float cross_offset = 0.0f;

        if (style->cross_align != LAYOUT_ALIGN_FILL &&
            requested_cross > 0.0f &&
            style->cross_align == LAYOUT_ALIGN_CENTER) {
            cross_offset = (content_cross - child_cross) * 0.5f;
        } else if (style->cross_align != LAYOUT_ALIGN_FILL &&
                   requested_cross > 0.0f &&
                   style->cross_align == LAYOUT_ALIGN_END) {
            cross_offset = content_cross - child_cross;
        }

        if (horizontal) {
            child->bounds = (Rect) {
                .x = cursor,
                .y = cross_start + cross_offset,
                .w = child_main,
                .h = child_cross,
            };
        } else {
            child->bounds = (Rect) {
                .x = cross_start + cross_offset,
                .y = cursor,
                .w = child_cross,
                .h = child_main,
            };
        }
        cursor += child_main + style->spacing;
    }
}
