/**
 * layout.c – Row and column layout using child Style width/height hints.
 */

#include "layout.h"

#include <math.h>
#include <stdlib.h>

struct LayoutContainer {
    Widget base;
    LayoutStyle layout_style;
    struct LayoutChildOptions *child_options;
};

typedef struct LayoutChildOptions {
    Widget *child;
    LayoutChildStyle style;
    struct LayoutChildOptions *next;
} LayoutChildOptions;

static LayoutChildStyle child_style_default(void)
{
    return (LayoutChildStyle) {
        .flex_weight = 1.0f,
        .min_size = 0.0f,
        .max_size = 0.0f,
        .margin = {0},
        .cross_align = LAYOUT_ALIGN_INHERIT,
    };
}

static LayoutChildOptions *find_child_options(LayoutContainer *layout,
                                               Widget *child)
{
    for (LayoutChildOptions *options = layout->child_options;
         options; options = options->next) {
        if (options->child == child) return options;
    }
    return NULL;
}

static void layout_widget_reflow(Widget *self)
{
    layout_container_reflow((LayoutContainer *)self);
}

static void layout_destroy(Widget *self)
{
    LayoutContainer *layout = (LayoutContainer *)self;
    LayoutChildOptions *options = layout->child_options;
    while (options) {
        LayoutChildOptions *next = options->next;
        free(options);
        options = next;
    }
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

LayoutChildStyle layout_child_style_default(void)
{
    return child_style_default();
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
    layout->base.destroy = layout_destroy;
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
    if (!layout || !widget_remove_child(&layout->base, child)) return false;
    LayoutChildOptions **link = &layout->child_options;
    while (*link && (*link)->child != child) link = &(*link)->next;
    if (*link) {
        LayoutChildOptions *removed = *link;
        *link = removed->next;
        free(removed);
    }
    return true;
}

static bool child_style_valid(LayoutChildStyle style)
{
    return isfinite(style.flex_weight) && style.flex_weight > 0.0f &&
           isfinite(style.min_size) && style.min_size >= 0.0f &&
           isfinite(style.max_size) && style.max_size >= 0.0f &&
           (style.max_size == 0.0f || style.max_size >= style.min_size) &&
           isfinite(style.margin.top) && style.margin.top >= 0.0f &&
           isfinite(style.margin.right) && style.margin.right >= 0.0f &&
           isfinite(style.margin.bottom) && style.margin.bottom >= 0.0f &&
           isfinite(style.margin.left) && style.margin.left >= 0.0f &&
           style.cross_align >= LAYOUT_ALIGN_FILL &&
           style.cross_align <= LAYOUT_ALIGN_INHERIT;
}

bool layout_container_set_child_style(LayoutContainer *layout, Widget *child,
                                      LayoutChildStyle style)
{
    if (!layout || !child || child->parent != &layout->base ||
        !child_style_valid(style)) {
        return false;
    }

    LayoutChildOptions *options = find_child_options(layout, child);
    if (!options) {
        options = malloc(sizeof(*options));
        if (!options) return false;
        options->child = child;
        options->next = layout->child_options;
        layout->child_options = options;
    }
    options->style = style;
    widget_layout(&layout->base);
    return true;
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
    float fixed_total = 0.0f;
    float flexible_min_total = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        Widget *child = base->children[i];
        float requested = horizontal ? child->style.width : child->style.height;
        LayoutChildOptions *options = find_child_options(layout, child);
        LayoutChildStyle child_style = options
            ? options->style : child_style_default();
        float main_margins = horizontal
            ? child_style.margin.left + child_style.margin.right
            : child_style.margin.top + child_style.margin.bottom;
        float minimum = child_style.min_size;
        float maximum = child_style.max_size > 0.0f
            ? child_style.max_size : INFINITY;
        float child_main;
        if (isfinite(requested) && requested > 0.0f) {
            child_main = fminf(maximum, fmaxf(minimum, requested));
            fixed_total += child_main + main_margins;
        } else {
            child_main = minimum;
            flexible_min_total += minimum + main_margins;
        }
        if (horizontal) child->bounds.w = child_main;
        else child->bounds.h = child_main;
    }

    float extra = fmaxf(0.0f, content_main - gaps -
                             fixed_total - flexible_min_total);
    for (;;) {
        float total_weight = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            Widget *child = base->children[i];
            float requested = horizontal ? child->style.width
                                         : child->style.height;
            if (isfinite(requested) && requested > 0.0f) continue;
            LayoutChildOptions *options = find_child_options(layout, child);
            LayoutChildStyle child_style = options
                ? options->style : child_style_default();
            float current = horizontal ? child->bounds.w : child->bounds.h;
            if (child_style.max_size == 0.0f ||
                current < child_style.max_size) {
                total_weight += child_style.flex_weight;
            }
        }
        if (total_weight <= 0.0f || extra <= 0.0f) break;

        bool capped = false;
        float capped_extra = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            Widget *child = base->children[i];
            float requested = horizontal ? child->style.width
                                         : child->style.height;
            if (isfinite(requested) && requested > 0.0f) continue;
            LayoutChildOptions *options = find_child_options(layout, child);
            LayoutChildStyle child_style = options
                ? options->style : child_style_default();
            float current = horizontal ? child->bounds.w : child->bounds.h;
            if (child_style.max_size == 0.0f ||
                current >= child_style.max_size) continue;
            float share = extra * child_style.flex_weight / total_weight;
            if (current + share >= child_style.max_size) {
                capped = true;
                capped_extra += child_style.max_size - current;
            }
        }

        if (!capped) {
            for (size_t i = 0; i < count; ++i) {
                Widget *child = base->children[i];
                float requested = horizontal ? child->style.width
                                             : child->style.height;
                if (isfinite(requested) && requested > 0.0f) continue;
                LayoutChildOptions *options = find_child_options(layout, child);
                LayoutChildStyle child_style = options
                    ? options->style : child_style_default();
                float current = horizontal ? child->bounds.w : child->bounds.h;
                if (child_style.max_size > 0.0f &&
                    current >= child_style.max_size) continue;
                float share = extra * child_style.flex_weight / total_weight;
                if (horizontal) child->bounds.w = current + share;
                else child->bounds.h = current + share;
            }
            break;
        }

        for (size_t i = 0; i < count; ++i) {
            Widget *child = base->children[i];
            float requested = horizontal ? child->style.width
                                         : child->style.height;
            if (isfinite(requested) && requested > 0.0f) continue;
            LayoutChildOptions *options = find_child_options(layout, child);
            LayoutChildStyle child_style = options
                ? options->style : child_style_default();
            float current = horizontal ? child->bounds.w : child->bounds.h;
            if (child_style.max_size == 0.0f ||
                current >= child_style.max_size) continue;
            float share = extra * child_style.flex_weight / total_weight;
            if (current + share >= child_style.max_size) {
                if (horizontal) child->bounds.w = child_style.max_size;
                else child->bounds.h = child_style.max_size;
            }
        }
        extra = fmaxf(0.0f, extra - capped_extra);
    }

    float cursor = (horizontal ? base->bounds.x : base->bounds.y) +
                   style->padding;
    float cross_start = (horizontal ? base->bounds.y : base->bounds.x) +
                        style->padding;

    for (size_t i = 0; i < count; ++i) {
        Widget *child = base->children[i];
        LayoutChildOptions *options = find_child_options(layout, child);
        LayoutChildStyle child_style = options
            ? options->style : child_style_default();
        float requested_cross = horizontal
            ? child->style.height : child->style.width;
        float child_main = horizontal ? child->bounds.w : child->bounds.h;
        float margin_main_start = horizontal
            ? child_style.margin.left : child_style.margin.top;
        float margin_main_end = horizontal
            ? child_style.margin.right : child_style.margin.bottom;
        float margin_cross_start = horizontal
            ? child_style.margin.top : child_style.margin.left;
        float margin_cross_end = horizontal
            ? child_style.margin.bottom : child_style.margin.right;
        float available_cross = fmaxf(
            0.0f, content_cross - margin_cross_start - margin_cross_end);
        LayoutAlign cross_align =
            child_style.cross_align == LAYOUT_ALIGN_INHERIT
                ? style->cross_align : child_style.cross_align;
        float child_cross = cross_align != LAYOUT_ALIGN_FILL &&
                            isfinite(requested_cross) && requested_cross > 0.0f
            ? fminf(requested_cross, available_cross) : available_cross;
        float cross_offset = 0.0f;

        if (cross_align == LAYOUT_ALIGN_CENTER) {
            cross_offset = (available_cross - child_cross) * 0.5f;
        } else if (cross_align == LAYOUT_ALIGN_END) {
            cross_offset = available_cross - child_cross;
        }

        if (horizontal) {
            child->bounds = (Rect) {
                .x = cursor + margin_main_start,
                .y = cross_start + margin_cross_start + cross_offset,
                .w = child_main,
                .h = child_cross,
            };
        } else {
            child->bounds = (Rect) {
                .x = cross_start + margin_cross_start + cross_offset,
                .y = cursor + margin_main_start,
                .w = child_cross,
                .h = child_main,
            };
        }
        cursor += margin_main_start + child_main + margin_main_end +
                  style->spacing;
    }
}
