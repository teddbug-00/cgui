/**
 * grid.c - Grid layout with fixed and weighted flexible tracks.
 */

#include "grid.h"

#include <math.h>
#include <stdlib.h>

typedef struct GridChild {
    Widget *widget;
    size_t row;
    size_t column;
    size_t row_span;
    size_t column_span;
    GridChildStyle style;
    struct GridChild *next;
} GridChild;

struct GridLayout {
    Widget base;
    GridStyle style;
    GridTrack *columns;
    GridTrack *rows;
    float *column_sizes;
    float *row_sizes;
    size_t column_count;
    size_t row_count;
    GridChild *children;
};

static bool finite_nonnegative(float value)
{
    return isfinite(value) && value >= 0.0f;
}

GridTrack grid_track_fixed(float size)
{
    return (GridTrack){ .size = size };
}

GridTrack grid_track_flexible(float weight, float min_size, float max_size)
{
    return (GridTrack) {
        .weight = weight,
        .min_size = min_size,
        .max_size = max_size,
    };
}

GridStyle grid_style_default(void)
{
    return (GridStyle){0};
}

GridChildStyle grid_child_style_default(void)
{
    return (GridChildStyle) {
        .horizontal_align = LAYOUT_ALIGN_FILL,
        .vertical_align = LAYOUT_ALIGN_FILL,
    };
}

static bool track_valid(GridTrack track)
{
    return finite_nonnegative(track.size) &&
           finite_nonnegative(track.weight) &&
           finite_nonnegative(track.min_size) &&
           finite_nonnegative(track.max_size) &&
           (track.max_size == 0.0f || track.max_size >= track.min_size) &&
           (track.size > 0.0f || track.weight > 0.0f);
}

static bool style_valid(GridStyle style)
{
    return finite_nonnegative(style.row_gap) &&
           finite_nonnegative(style.column_gap) &&
           finite_nonnegative(style.padding);
}

static bool child_style_valid(GridChildStyle style)
{
    return finite_nonnegative(style.margin.top) &&
           finite_nonnegative(style.margin.right) &&
           finite_nonnegative(style.margin.bottom) &&
           finite_nonnegative(style.margin.left) &&
           style.horizontal_align >= LAYOUT_ALIGN_FILL &&
           style.horizontal_align <= LAYOUT_ALIGN_END &&
           style.vertical_align >= LAYOUT_ALIGN_FILL &&
           style.vertical_align <= LAYOUT_ALIGN_END;
}

static GridChild *find_child(GridLayout *grid, Widget *widget)
{
    for (GridChild *child = grid->children; child; child = child->next) {
        if (child->widget == widget) return child;
    }
    return NULL;
}

static void grid_widget_reflow(Widget *self)
{
    grid_layout_reflow((GridLayout *)self);
}

static void grid_draw(Widget *self, struct VkContext *context)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) child->draw(child, context);
    }
}

static void grid_event(Widget *self, const void *event)
{
    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->handle_event) child->handle_event(child, event);
    }
}

static void grid_destroy(Widget *self)
{
    GridLayout *grid = (GridLayout *)self;
    GridChild *child = grid->children;
    while (child) {
        GridChild *next = child->next;
        free(child);
        child = next;
    }
    free(grid->columns);
    free(grid->rows);
    free(grid->column_sizes);
    free(grid->row_sizes);
}

GridLayout *grid_layout_create(Rect bounds, GridStyle style,
                               const GridTrack *columns, size_t column_count,
                               const GridTrack *rows, size_t row_count)
{
    if (!style_valid(style) || !columns || !rows ||
        column_count == 0 || row_count == 0 ||
        column_count > SIZE_MAX / sizeof(*columns) ||
        row_count > SIZE_MAX / sizeof(*rows)) {
        return NULL;
    }
    for (size_t i = 0; i < column_count; ++i) {
        if (!track_valid(columns[i])) return NULL;
    }
    for (size_t i = 0; i < row_count; ++i) {
        if (!track_valid(rows[i])) return NULL;
    }

    GridLayout *grid = calloc(1, sizeof(*grid));
    if (!grid) return NULL;
    grid->columns = malloc(column_count * sizeof(*columns));
    grid->rows = malloc(row_count * sizeof(*rows));
    grid->column_sizes = calloc(column_count, sizeof(*grid->column_sizes));
    grid->row_sizes = calloc(row_count, sizeof(*grid->row_sizes));
    if (!grid->columns || !grid->rows ||
        !grid->column_sizes || !grid->row_sizes) {
        free(grid->columns);
        free(grid->rows);
        free(grid->column_sizes);
        free(grid->row_sizes);
        free(grid);
        return NULL;
    }
    for (size_t i = 0; i < column_count; ++i) grid->columns[i] = columns[i];
    for (size_t i = 0; i < row_count; ++i) grid->rows[i] = rows[i];
    grid->column_count = column_count;
    grid->row_count = row_count;
    grid->style = style;
    grid->base.bounds = bounds;
    grid->base.style = style_default();
    grid->base.draw = grid_draw;
    grid->base.handle_event = grid_event;
    grid->base.destroy = grid_destroy;
    grid->base.layout = grid_widget_reflow;
    return grid;
}

Widget *grid_layout_widget(GridLayout *grid)
{
    return grid ? &grid->base : NULL;
}

bool grid_layout_add_child(GridLayout *grid, Widget *widget,
                           size_t row, size_t column,
                           size_t row_span, size_t column_span)
{
    if (!grid || !widget || row_span == 0 || column_span == 0 ||
        row >= grid->row_count || column >= grid->column_count ||
        row_span > grid->row_count - row ||
        column_span > grid->column_count - column ||
        find_child(grid, widget)) {
        return false;
    }

    GridChild *child = calloc(1, sizeof(*child));
    if (!child) return false;
    child->widget = widget;
    child->row = row;
    child->column = column;
    child->row_span = row_span;
    child->column_span = column_span;
    child->style = grid_child_style_default();
    child->next = grid->children;
    grid->children = child;
    if (!widget_add_child(&grid->base, widget)) {
        grid->children = child->next;
        free(child);
        return false;
    }
    return true;
}

bool grid_layout_remove_child(GridLayout *grid, Widget *widget)
{
    if (!grid || !widget || !find_child(grid, widget) ||
        !widget_remove_child(&grid->base, widget)) {
        return false;
    }
    GridChild **link = &grid->children;
    while (*link && (*link)->widget != widget) link = &(*link)->next;
    GridChild *removed = *link;
    *link = removed->next;
    free(removed);
    return true;
}

bool grid_layout_set_child_style(GridLayout *grid, Widget *widget,
                                 GridChildStyle style)
{
    if (!grid || !widget || widget->parent != &grid->base ||
        !child_style_valid(style)) {
        return false;
    }
    GridChild *child = find_child(grid, widget);
    if (!child) return false;
    child->style = style;
    widget_layout(&grid->base);
    return true;
}

static void resolve_tracks(const GridTrack *tracks, size_t count,
                           float available, float gap, float *sizes)
{
    float remaining = fmaxf(0.0f, available - gap * (float)(count - 1));
    float flexible_minimum = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        GridTrack track = tracks[i];
        float maximum = track.max_size > 0.0f ? track.max_size : INFINITY;
        if (track.size > 0.0f) {
            sizes[i] = fminf(maximum, fmaxf(track.min_size, track.size));
            remaining -= sizes[i];
        } else {
            sizes[i] = track.min_size;
            flexible_minimum += track.min_size;
        }
    }
    float extra = fmaxf(0.0f, remaining - flexible_minimum);

    for (;;) {
        float total_weight = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            if (tracks[i].size > 0.0f) continue;
            if (tracks[i].max_size == 0.0f ||
                sizes[i] < tracks[i].max_size) {
                total_weight += tracks[i].weight;
            }
        }
        if (total_weight <= 0.0f || extra <= 0.0f) return;

        bool capped = false;
        float used = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            GridTrack track = tracks[i];
            if (track.size > 0.0f || track.max_size == 0.0f ||
                sizes[i] >= track.max_size) continue;
            float share = extra * track.weight / total_weight;
            if (sizes[i] + share >= track.max_size) {
                capped = true;
                used += track.max_size - sizes[i];
            }
        }
        if (!capped) {
            for (size_t i = 0; i < count; ++i) {
                GridTrack track = tracks[i];
                if (track.size > 0.0f ||
                    (track.max_size > 0.0f &&
                     sizes[i] >= track.max_size)) continue;
                sizes[i] += extra * track.weight / total_weight;
            }
            return;
        }
        for (size_t i = 0; i < count; ++i) {
            GridTrack track = tracks[i];
            if (track.size > 0.0f || track.max_size == 0.0f ||
                sizes[i] >= track.max_size) continue;
            float share = extra * track.weight / total_weight;
            if (sizes[i] + share >= track.max_size) sizes[i] = track.max_size;
        }
        extra = fmaxf(0.0f, extra - used);
    }
}

static float track_span_position(const float *sizes, size_t first, float gap)
{
    float result = 0.0f;
    for (size_t i = 0; i < first; ++i) result += sizes[i] + gap;
    return result;
}

static float track_span_size(const float *sizes, size_t first,
                             size_t span, float gap)
{
    float result = gap * (float)(span - 1);
    for (size_t i = 0; i < span; ++i) result += sizes[first + i];
    return result;
}

static float align_child(float start, float extent, float requested,
                         LayoutAlign align, float margin_start,
                         float margin_end, float *child_extent)
{
    float available = fmaxf(0.0f, extent - margin_start - margin_end);
    *child_extent = (align == LAYOUT_ALIGN_FILL || requested <= 0.0f)
        ? available : fminf(requested, available);
    float offset = 0.0f;
    if (align == LAYOUT_ALIGN_CENTER) offset = (available - *child_extent) * 0.5f;
    else if (align == LAYOUT_ALIGN_END) offset = available - *child_extent;
    return start + margin_start + offset;
}

void grid_layout_reflow(GridLayout *grid)
{
    if (!grid) return;
    Rect bounds = grid->base.bounds;
    float inner_width = fmaxf(0.0f, bounds.w - 2.0f * grid->style.padding);
    float inner_height = fmaxf(0.0f, bounds.h - 2.0f * grid->style.padding);
    resolve_tracks(grid->columns, grid->column_count, inner_width,
                   grid->style.column_gap, grid->column_sizes);
    resolve_tracks(grid->rows, grid->row_count, inner_height,
                   grid->style.row_gap, grid->row_sizes);

    for (GridChild *child = grid->children; child; child = child->next) {
        Widget *widget = child->widget;
        float cell_x = bounds.x + grid->style.padding +
            track_span_position(grid->column_sizes, child->column,
                            grid->style.column_gap);
        float cell_y = bounds.y + grid->style.padding +
            track_span_position(grid->row_sizes, child->row,
                            grid->style.row_gap);
        float cell_width = track_span_size(
            grid->column_sizes, child->column, child->column_span,
            grid->style.column_gap);
        float cell_height = track_span_size(
            grid->row_sizes, child->row, child->row_span,
            grid->style.row_gap);
        widget->bounds.x = align_child(
            cell_x, cell_width, widget->style.width,
            child->style.horizontal_align, child->style.margin.left,
            child->style.margin.right, &widget->bounds.w);
        widget->bounds.y = align_child(
            cell_y, cell_height, widget->style.height,
            child->style.vertical_align, child->style.margin.top,
            child->style.margin.bottom, &widget->bounds.h);
    }
}
