/**
 * layout.h – Simple row and column widget layout.
 */

#ifndef CGUI_LAYOUT_H
#define CGUI_LAYOUT_H

#include "gui.h"

typedef enum LayoutDirection {
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
} LayoutDirection;

typedef enum LayoutAlign {
    LAYOUT_ALIGN_FILL,
    LAYOUT_ALIGN_START,
    LAYOUT_ALIGN_CENTER,
    LAYOUT_ALIGN_END,
    LAYOUT_ALIGN_INHERIT,
} LayoutAlign;

typedef struct LayoutInsets {
    float top;
    float right;
    float bottom;
    float left;
} LayoutInsets;

typedef struct LayoutStyle {
    LayoutDirection direction;
    LayoutAlign cross_align;
    float spacing;
    float padding;
} LayoutStyle;

typedef struct LayoutChildStyle {
    float flex_weight;
    float min_size;
    float max_size;
    LayoutInsets margin;
    LayoutAlign cross_align;
} LayoutChildStyle;

typedef struct LayoutContainer LayoutContainer;

/**
 * Child Style.width/height act as fixed-size hints. A missing main-axis hint
 * shares remaining space according to flex_weight. min_size/max_size constrain
 * the main-axis size; max_size zero means unbounded. Margins reduce available
 * space on both axes. LAYOUT_ALIGN_INHERIT uses the container's alignment.
 */
LayoutStyle layout_style_default(LayoutDirection direction);
LayoutChildStyle layout_child_style_default(void);
LayoutContainer *layout_container_create(Rect bounds, LayoutStyle style);
Widget *layout_container_widget(LayoutContainer *layout);
bool layout_container_add_child(LayoutContainer *layout, Widget *child);
bool layout_container_remove_child(LayoutContainer *layout, Widget *child);
bool layout_container_set_child_style(LayoutContainer *layout, Widget *child,
                                      LayoutChildStyle style);
bool layout_container_set_style(LayoutContainer *layout,
                                const LayoutStyle *style);
void layout_container_reflow(LayoutContainer *layout);

#endif
