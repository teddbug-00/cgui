/**
 * popover_menu.h - Keyboard- and pointer-accessible popover menu.
 */

#ifndef CGUI_POPOVER_MENU_H
#define CGUI_POPOVER_MENU_H

#include "gui.h"
#include "vk_draw.h"

typedef struct PopoverMenu PopoverMenu;

typedef struct PopoverMenuStyle {
    float row_height;
    float padding;
    float horizontal_padding;
    float corner_radius;
    float font_size;
    uint8_t background[4];
    uint8_t border[4];
    uint8_t hover_background[4];
    uint8_t text[4];
    uint8_t disabled_text[4];
} PopoverMenuStyle;

typedef void (*PopoverMenuSelectFn)(PopoverMenu *menu, size_t index,
                                    void *userdata);

PopoverMenuStyle popover_menu_style_default(void);
/** The menu height is computed from its items; bounds.x/y/w position and size it. */
PopoverMenu *popover_menu_create(Rect bounds, const PopoverMenuStyle *style);
Widget *popover_menu_widget(PopoverMenu *menu);
bool popover_menu_add_item(PopoverMenu *menu, const char *label, bool enabled);
size_t popover_menu_item_count(const PopoverMenu *menu);
void popover_menu_set_open(PopoverMenu *menu, bool open);
bool popover_menu_is_open(const PopoverMenu *menu);
void popover_menu_set_select_handler(PopoverMenu *menu,
                                     PopoverMenuSelectFn callback,
                                     void *userdata);
void popover_menu_set_draw_context(VkDraw *draw);

#endif
