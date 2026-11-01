/**
 * tab_view.h - Editor-style tabs with per-tab widget content.
 */

#ifndef CGUI_TAB_VIEW_H
#define CGUI_TAB_VIEW_H

#include "gui.h"
#include "vk_draw.h"

typedef struct TabView TabView;

typedef struct TabViewStyle {
    float tab_height;
    float tab_min_width;
    float tab_max_width;
    float tab_font_size;
    float tab_icon_size;
    float horizontal_padding;
    float corner_radius;
    uint8_t background[4];
    uint8_t strip_background[4];
    uint8_t active_tab[4];
    uint8_t inactive_tab[4];
    uint8_t hover_tab[4];
    uint8_t border[4];
    uint8_t text[4];
    uint8_t active_text[4];
} TabViewStyle;

typedef void (*TabViewSelectionFn)(TabView *view, size_t index,
                                   void *userdata);

TabViewStyle tab_view_style_default(void);
TabView *tab_view_create(Rect bounds, const TabViewStyle *style);
Widget *tab_view_widget(TabView *view);
bool tab_view_add_tab(TabView *view, const char *title, Widget *content);
/** Set a non-owning renderer-loaded icon for a tab; NULL removes its icon. */
bool tab_view_set_icon(TabView *view, size_t index, const VkDrawIcon *icon);
const VkDrawIcon *tab_view_icon(const TabView *view, size_t index);
/**
 * Set a non-owning, renderer-loaded icon for tab close controls.
 * Passing NULL restores the built-in text close glyph.
 */
void tab_view_set_close_icon(TabView *view, const VkDrawIcon *icon);
/** Remove a tab and transfer its content widget to the caller. */
Widget *tab_view_take_tab(TabView *view, size_t index);
bool tab_view_set_active(TabView *view, size_t index);
size_t tab_view_tab_count(const TabView *view);
size_t tab_view_active_index(const TabView *view);
const char *tab_view_title(const TabView *view, size_t index);
Widget *tab_view_content(const TabView *view, size_t index);
void tab_view_set_selection_handler(TabView *view,
                                    TabViewSelectionFn callback,
                                    void *userdata);
void tab_view_set_draw_context(VkDraw *draw);

#endif
