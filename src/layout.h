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
} LayoutAlign;

typedef struct LayoutStyle {
    LayoutDirection direction;
    LayoutAlign cross_align;
    float spacing;
    float padding;
} LayoutStyle;

typedef struct LayoutContainer LayoutContainer;

/**
 * Child Style.width/height act as fixed-size hints. A zero main-axis hint
 * shares remaining space equally; a zero cross-axis hint fills the container.
 */
LayoutStyle layout_style_default(LayoutDirection direction);
LayoutContainer *layout_container_create(Rect bounds, LayoutStyle style);
Widget *layout_container_widget(LayoutContainer *layout);
bool layout_container_add_child(LayoutContainer *layout, Widget *child);
bool layout_container_remove_child(LayoutContainer *layout, Widget *child);
bool layout_container_set_style(LayoutContainer *layout,
                                const LayoutStyle *style);
void layout_container_reflow(LayoutContainer *layout);

#endif
