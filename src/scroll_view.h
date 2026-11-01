/**
 * scroll_view.h - Clipped vertical viewport for retained widget content.
 */

#ifndef CGUI_SCROLL_VIEW_H
#define CGUI_SCROLL_VIEW_H

#include "gui.h"
#include "vk_draw.h"

typedef struct ScrollView ScrollView;

typedef struct ScrollViewStyle {
    float wheel_step;
} ScrollViewStyle;

ScrollViewStyle scroll_view_style_default(void);
ScrollView *scroll_view_create(Rect bounds, ScrollViewStyle style);
Widget *scroll_view_widget(ScrollView *view);
bool scroll_view_set_content(ScrollView *view, Widget *content,
                             float content_height);
bool scroll_view_set_content_height(ScrollView *view, float content_height);
bool scroll_view_set_scroll_offset(ScrollView *view, float offset);
/** Advance smooth scrolling and edge-bounce physics by elapsed seconds. */
void scroll_view_update(ScrollView *view, float delta_seconds);
float scroll_view_scroll_offset(const ScrollView *view);
float scroll_view_max_scroll(const ScrollView *view);
void scroll_view_set_draw_context(VkDraw *draw);

#endif
