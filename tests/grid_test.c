#include "grid.h"

#include <math.h>
#include <stdio.h>

static bool near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.02f;
}

int main(void)
{
    GridTrack columns[] = {
        grid_track_fixed(60.0f),
        grid_track_flexible(1.0f, 40.0f, 80.0f),
        grid_track_flexible(2.0f, 20.0f, 0.0f),
    };
    GridTrack rows[] = {
        grid_track_flexible(1.0f, 0.0f, 0.0f),
        grid_track_flexible(2.0f, 0.0f, 0.0f),
    };
    GridStyle style = {
        .row_gap = 10.0f,
        .column_gap = 10.0f,
        .padding = 10.0f,
    };
    GridLayout *grid = grid_layout_create(
        (Rect){10.0f, 20.0f, 320.0f, 200.0f}, style,
        columns, 3, rows, 2);
    Widget *span = widget_create();
    Widget *fill = widget_create();
    if (!grid || !span || !fill) {
        widget_destroy(grid_layout_widget(grid));
        widget_destroy(span);
        widget_destroy(fill);
        return 1;
    }

    span->style.width = 100.0f;
    span->style.height = 20.0f;
    Widget *invalid_child = widget_create();
    if (!grid_layout_add_child(grid, span, 0, 1, 1, 2) ||
        !grid_layout_add_child(grid, fill, 1, 0, 1, 1) ||
        !invalid_child ||
        grid_layout_add_child(grid, invalid_child, 2, 0, 1, 1)) {
        fprintf(stderr, "Grid accepted an out-of-range child or failed adding children\n");
        if (invalid_child && !invalid_child->parent) widget_destroy(invalid_child);
        widget_destroy(grid_layout_widget(grid));
        return 1;
    }
    widget_destroy(invalid_child);

    GridChildStyle span_style = grid_child_style_default();
    span_style.margin = (LayoutInsets) {
        .top = 2.0f, .right = 5.0f, .bottom = 4.0f, .left = 5.0f,
    };
    span_style.horizontal_align = LAYOUT_ALIGN_CENTER;
    span_style.vertical_align = LAYOUT_ALIGN_END;
    if (!grid_layout_set_child_style(grid, span, span_style) ||
        !near(span->bounds.x, 155.0f) ||
        !near(span->bounds.y, 62.67f) ||
        !near(span->bounds.w, 100.0f) ||
        !near(span->bounds.h, 20.0f) ||
        !near(fill->bounds.x, 20.0f) ||
        !near(fill->bounds.y, 96.67f) ||
        !near(fill->bounds.w, 60.0f) ||
        !near(fill->bounds.h, 113.33f)) {
        fprintf(stderr, "Grid track sizing, spanning, or alignment failed\n");
        widget_destroy(grid_layout_widget(grid));
        return 1;
    }

    widget_set_bounds(grid_layout_widget(grid),
                      (Rect){0.0f, 0.0f, 400.0f, 240.0f});
    if (!near(span->bounds.x, 185.0f) ||
        !near(fill->bounds.y, 90.0f) ||
        !near(fill->bounds.h, 140.0f)) {
        fprintf(stderr, "Grid bounds update did not reflow tracks: "
                "span x=%.2f fill y=%.2f h=%.2f\n",
                span->bounds.x, fill->bounds.y, fill->bounds.h);
        widget_destroy(grid_layout_widget(grid));
        return 1;
    }

    GridChildStyle invalid = span_style;
    invalid.margin.left = -1.0f;
    if (grid_layout_set_child_style(grid, span, invalid)) {
        fprintf(stderr, "Grid accepted an invalid child style\n");
        widget_destroy(grid_layout_widget(grid));
        return 1;
    }
    widget_destroy(grid_layout_widget(grid));

    GridTrack invalid_track = grid_track_flexible(0.0f, 0.0f, 0.0f);
    if (grid_layout_create((Rect){0}, style, &invalid_track, 1, rows, 2)) {
        fprintf(stderr, "Grid accepted a zero-weight flexible track\n");
        return 1;
    }

    puts("Grid layout tests passed!");
    return 0;
}
