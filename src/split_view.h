/**
 * split_view.h - Two-pane layout with a draggable divider.
 */

#ifndef CGUI_SPLIT_VIEW_H
#define CGUI_SPLIT_VIEW_H

#include "gui.h"
#include "vk_draw.h"

typedef struct SplitView SplitView;

typedef enum SplitViewOrientation {
    /** First pane on the left, second pane on the right. */
    SPLIT_VIEW_HORIZONTAL,
    /** First pane on top, second pane on the bottom. */
    SPLIT_VIEW_VERTICAL,
} SplitViewOrientation;

typedef struct SplitViewStyle {
    SplitViewOrientation orientation;
    float ratio;
    /** Divider hit-target thickness; visual separator remains thinner. */
    float divider_size;
    float first_min_size;
    float second_min_size;
    uint8_t divider_color[4];
    uint8_t divider_hover_color[4];
    uint8_t divider_active_color[4];
} SplitViewStyle;

SplitViewStyle split_view_style_default(void);
SplitView *split_view_create(Rect bounds, const SplitViewStyle *style);
Widget *split_view_widget(SplitView *view);
/** Attach two unparented panes; SplitView owns them after success. */
bool split_view_set_panes(SplitView *view, Widget *first, Widget *second);
bool split_view_set_ratio(SplitView *view, float ratio);
float split_view_ratio(const SplitView *view);
Widget *split_view_first_pane(const SplitView *view);
Widget *split_view_second_pane(const SplitView *view);
void split_view_set_draw_context(VkDraw *draw);

#endif
