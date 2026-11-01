#include "layout.h"

#include <math.h>
#include <stdio.h>

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.01f;
}

int main(void)
{
    LayoutStyle row_style = layout_style_default(LAYOUT_HORIZONTAL);
    row_style.padding = 10.0f;
    row_style.spacing = 10.0f;
    row_style.cross_align = LAYOUT_ALIGN_CENTER;
    LayoutContainer *row = layout_container_create(
        (Rect){10.0f, 20.0f, 300.0f, 100.0f}, row_style);
    Widget *fixed = widget_create();
    LayoutContainer *nested_layout = layout_container_create(
        (Rect){0}, layout_style_default(LAYOUT_VERTICAL));
    Widget *nested = layout_container_widget(nested_layout);
    Widget *flexible = widget_create();
    Widget *nested_child = widget_create();
    if (!row || !fixed || !nested || !flexible || !nested_child) {
        widget_destroy(layout_container_widget(row));
        widget_destroy(fixed);
        widget_destroy(nested);
        widget_destroy(flexible);
        widget_destroy(nested_child);
        return 1;
    }

    fixed->style.width = 40.0f;
    fixed->style.height = 20.0f;
    nested->style.height = 30.0f;
    nested_child->style.height = 10.0f;
    if (!layout_container_add_child(row, fixed) ||
        !layout_container_add_child(row, nested) ||
        !layout_container_add_child(row, flexible) ||
        !widget_add_child(nested, nested_child)) {
        fprintf(stderr, "Failed to assemble layout hierarchy\n");
        if (!fixed->parent) widget_destroy(fixed);
        if (!nested->parent) widget_destroy(nested);
        if (!flexible->parent) widget_destroy(flexible);
        if (!nested_child->parent) widget_destroy(nested_child);
        widget_destroy(layout_container_widget(row));
        return 1;
    }

    Widget *row_widget = layout_container_widget(row);
    if (!near(fixed->bounds.x, 20.0f) ||
        !near(fixed->bounds.y, 60.0f) ||
        !near(fixed->bounds.w, 40.0f) ||
        !near(fixed->bounds.h, 20.0f) ||
        !near(nested->bounds.x, 70.0f) ||
        !near(nested->bounds.y, 55.0f) ||
        !near(nested->bounds.w, 110.0f) ||
        !near(nested->bounds.h, 30.0f) ||
        !near(flexible->bounds.x, 190.0f) ||
        !near(flexible->bounds.w, 110.0f) ||
        !near(nested_child->bounds.x, nested->bounds.x) ||
        !near(nested_child->bounds.y, nested->bounds.y) ||
        !near(nested_child->bounds.h, 10.0f)) {
        fprintf(stderr, "Row, flex, alignment, or nested layout failed\n");
        widget_destroy(row_widget);
        return 1;
    }

    if (!layout_container_remove_child(row, flexible) ||
        flexible->parent != NULL ||
        !near(nested->bounds.w, 230.0f) ||
        !layout_container_add_child(row, flexible) ||
        !near(nested->bounds.w, 110.0f)) {
        fprintf(stderr, "Child removal or automatic reflow failed\n");
        if (flexible->parent != row_widget) widget_destroy(flexible);
        widget_destroy(row_widget);
        return 1;
    }

    LayoutStyle invalid = row_style;
    invalid.spacing = -1.0f;
    if (layout_container_set_style(row, &invalid)) {
        fprintf(stderr, "Invalid layout style was accepted\n");
        widget_destroy(row_widget);
        return 1;
    }

    widget_set_bounds(row_widget, (Rect){0.0f, 0.0f, 400.0f, 120.0f});
    if (!near(flexible->bounds.w, 160.0f)) {
        fprintf(stderr, "Bounds update did not reflow flex children\n");
        widget_destroy(row_widget);
        return 1;
    }

    widget_destroy(row_widget);

    LayoutContainer *weighted_row = layout_container_create(
        (Rect){0.0f, 0.0f, 320.0f, 40.0f},
        (LayoutStyle) {
            .direction = LAYOUT_HORIZONTAL,
            .cross_align = LAYOUT_ALIGN_FILL,
            .spacing = 10.0f,
            .padding = 10.0f,
        });
    Widget *weighted_fixed = widget_create();
    Widget *weighted_one = widget_create();
    Widget *weighted_two = widget_create();
    if (!weighted_row || !weighted_fixed || !weighted_one || !weighted_two) {
        widget_destroy(layout_container_widget(weighted_row));
        widget_destroy(weighted_fixed);
        widget_destroy(weighted_one);
        widget_destroy(weighted_two);
        return 1;
    }
    weighted_fixed->style.width = 50.0f;
    if (!layout_container_add_child(weighted_row, weighted_fixed) ||
        !layout_container_add_child(weighted_row, weighted_one) ||
        !layout_container_add_child(weighted_row, weighted_two)) {
        widget_destroy(layout_container_widget(weighted_row));
        if (!weighted_fixed->parent) widget_destroy(weighted_fixed);
        if (!weighted_one->parent) widget_destroy(weighted_one);
        if (!weighted_two->parent) widget_destroy(weighted_two);
        return 1;
    }
    LayoutChildStyle one_style = layout_child_style_default();
    one_style.flex_weight = 1.0f;
    one_style.min_size = 40.0f;
    one_style.max_size = 60.0f;
    one_style.margin = (LayoutInsets) {
        .top = 4.0f, .right = 7.0f, .bottom = 6.0f, .left = 5.0f,
    };
    one_style.cross_align = LAYOUT_ALIGN_START;
    LayoutChildStyle two_style = layout_child_style_default();
    two_style.flex_weight = 2.0f;
    two_style.min_size = 50.0f;
    weighted_one->style.height = 7.0f;
    weighted_two->style.height = 10.0f;
    two_style.margin.top = 2.0f;
    two_style.margin.bottom = 2.0f;
    two_style.cross_align = LAYOUT_ALIGN_END;
    if (!layout_container_set_child_style(weighted_row, weighted_one,
                                          one_style) ||
        !layout_container_set_child_style(weighted_row, weighted_two,
                                          two_style) ||
        !near(weighted_one->bounds.w, 60.0f) ||
        !near(weighted_two->bounds.w, 158.0f) ||
        !near(weighted_one->bounds.x, 75.0f) ||
        !near(weighted_one->bounds.y, 14.0f) ||
        !near(weighted_two->bounds.x, 152.0f) ||
        !near(weighted_two->bounds.y, 18.0f) ||
        !near(weighted_two->bounds.h, 10.0f)) {
        fprintf(stderr, "Weighted flex, margins, or cross alignment failed\n");
        widget_destroy(layout_container_widget(weighted_row));
        return 1;
    }

    widget_set_bounds(layout_container_widget(weighted_row),
                      (Rect){0.0f, 0.0f, 160.0f, 40.0f});
    if (!near(weighted_one->bounds.w, 40.0f) ||
        !near(weighted_two->bounds.w, 50.0f)) {
        fprintf(stderr, "Minimum constraints were not preserved\n");
        widget_destroy(layout_container_widget(weighted_row));
        return 1;
    }
    LayoutChildStyle invalid_child_style = one_style;
    invalid_child_style.margin.left = -1.0f;
    if (layout_container_set_child_style(weighted_row, weighted_one,
                                         invalid_child_style)) {
        fprintf(stderr, "Invalid child margin was accepted\n");
        widget_destroy(layout_container_widget(weighted_row));
        return 1;
    }
    widget_destroy(layout_container_widget(weighted_row));

    LayoutStyle column_style = layout_style_default(LAYOUT_VERTICAL);
    column_style.padding = 10.0f;
    LayoutContainer *column = layout_container_create(
        (Rect){0.0f, 0.0f, 100.0f, 100.0f}, column_style);
    Widget *column_child = widget_create();
    if (!column || !column_child) {
        widget_destroy(layout_container_widget(column));
        widget_destroy(column_child);
        return 1;
    }
    column_child->style.width = 20.0f;
    column_child->style.height = 20.0f;
    if (!layout_container_add_child(column, column_child)) {
        widget_destroy(column_child);
        widget_destroy(layout_container_widget(column));
        return 1;
    }
    LayoutChildStyle column_child_style = layout_child_style_default();
    column_child_style.cross_align = LAYOUT_ALIGN_END;
    column_child_style.margin = (LayoutInsets) {
        .top = 3.0f, .right = 6.0f, .bottom = 5.0f, .left = 10.0f,
    };
    if (!layout_container_set_child_style(column, column_child,
                                          column_child_style) ||
        !near(column_child->bounds.x, 64.0f) ||
        !near(column_child->bounds.y, 13.0f) ||
        !near(column_child->bounds.w, 20.0f) ||
        !near(column_child->bounds.h, 20.0f)) {
        fprintf(stderr, "Vertical margins or cross alignment failed\n");
        widget_destroy(layout_container_widget(column));
        return 1;
    }
    widget_destroy(layout_container_widget(column));

    puts("Widget layout tests passed!");
    return 0;
}
