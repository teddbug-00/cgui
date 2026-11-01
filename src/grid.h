/**
 * grid.h - Two-dimensional row/column widget layout.
 */

#ifndef CGUI_GRID_H
#define CGUI_GRID_H

#include "layout.h"

typedef struct GridTrack {
    float size;
    float weight;
    float min_size;
    float max_size;
} GridTrack;

typedef struct GridStyle {
    float row_gap;
    float column_gap;
    float padding;
} GridStyle;

typedef struct GridChildStyle {
    LayoutInsets margin;
    LayoutAlign horizontal_align;
    LayoutAlign vertical_align;
} GridChildStyle;

typedef struct GridLayout GridLayout;

GridTrack grid_track_fixed(float size);
GridTrack grid_track_flexible(float weight, float min_size, float max_size);
GridStyle grid_style_default(void);
GridChildStyle grid_child_style_default(void);

GridLayout *grid_layout_create(Rect bounds, GridStyle style,
                               const GridTrack *columns, size_t column_count,
                               const GridTrack *rows, size_t row_count);
Widget *grid_layout_widget(GridLayout *grid);
bool grid_layout_add_child(GridLayout *grid, Widget *child,
                           size_t row, size_t column,
                           size_t row_span, size_t column_span);
bool grid_layout_remove_child(GridLayout *grid, Widget *child);
bool grid_layout_set_child_style(GridLayout *grid, Widget *child,
                                 GridChildStyle style);
void grid_layout_reflow(GridLayout *grid);

#endif
