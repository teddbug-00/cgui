/**
 * popover_menu.c - Keyboard- and pointer-accessible popover menu.
 */

#include "popover_menu.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct PopoverMenuItem {
    char *label;
    bool enabled;
} PopoverMenuItem;

struct PopoverMenu {
    Widget base;
    PopoverMenuStyle menu_style;
    PopoverMenuItem *items;
    size_t item_count;
    size_t item_capacity;
    size_t active_index;
    size_t pressed_index;
    bool open;
    PopoverMenuSelectFn on_select;
    void *userdata;
};

static VkDraw *g_draw;

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

static char *copy_string(const char *text)
{
    size_t length = strlen(text);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}

PopoverMenuStyle popover_menu_style_default(void)
{
    return (PopoverMenuStyle) {
        .row_height = 30.0f,
        .padding = 6.0f,
        .horizontal_padding = 12.0f,
        .corner_radius = 9.0f,
        .font_size = 13.0f,
        .background = {255, 255, 255, 255},
        .border = {0, 0, 0, 24},
        .hover_background = {235, 239, 246, 255},
        .text = {37, 39, 43, 255},
        .disabled_text = {155, 158, 164, 255},
    };
}

static float popover_menu_height(const PopoverMenu *menu)
{
    return menu->menu_style.padding * 2.0f +
           (float)menu->item_count * menu->menu_style.row_height;
}

static void popover_menu_layout(Widget *self)
{
    PopoverMenu *menu = (PopoverMenu *)self;
    self->bounds.h = popover_menu_height(menu);
}

static void popover_menu_destroy(Widget *self)
{
    PopoverMenu *menu = (PopoverMenu *)self;
    for (size_t i = 0; i < menu->item_count; ++i)
        free(menu->items[i].label);
    free(menu->items);
}

static Rect popover_menu_item_bounds(const PopoverMenu *menu, size_t index)
{
    return (Rect) {
        .x = menu->base.bounds.x + menu->menu_style.padding,
        .y = menu->base.bounds.y + menu->menu_style.padding +
             (float)index * menu->menu_style.row_height,
        .w = fmaxf(0.0f, menu->base.bounds.w -
                   menu->menu_style.padding * 2.0f),
        .h = menu->menu_style.row_height,
    };
}

static void popover_menu_draw(Widget *self, struct VkContext *context)
{
    (void)context;
    PopoverMenu *menu = (PopoverMenu *)self;
    if (!menu->open || !g_draw) return;

    Style box = style_default();
    box.corner_radius = menu->menu_style.corner_radius;
    box.border_width = 0.5f;
    box.shadow_offset_y = 3.0f;
    box.shadow_blur = 12.0f;
    box.shadow_color[3] = 38;
    memcpy(box.bg, menu->menu_style.background, 4);
    memcpy(box.border_color, menu->menu_style.border, 4);
    vk_draw_style_box(g_draw, self->bounds.x, self->bounds.y,
                      self->bounds.w, self->bounds.h, &box);

    for (size_t i = 0; i < menu->item_count; ++i) {
        Rect item = popover_menu_item_bounds(menu, i);
        const uint8_t *color = menu->items[i].enabled
            ? menu->menu_style.text : menu->menu_style.disabled_text;
        if (menu->items[i].enabled && menu->active_index == i) {
            vk_draw_rounded_rect(g_draw, item.x, item.y, item.w, item.h,
                                 fmaxf(2.0f, menu->menu_style.corner_radius - 3.0f),
                                 0.5f, menu->menu_style.hover_background);
        }
        (void)vk_draw_text(g_draw, menu->items[i].label,
                           item.x + menu->menu_style.horizontal_padding,
                           item.y + (item.h - menu->menu_style.font_size) * 0.5f,
                           menu->menu_style.font_size, color);
    }
}

static size_t popover_menu_item_at(const PopoverMenu *menu, float x, float y)
{
    if (!point_in_rect(x, y, &menu->base.bounds) ||
        x < menu->base.bounds.x + menu->menu_style.padding ||
        x >= menu->base.bounds.x + menu->base.bounds.w -
             menu->menu_style.padding) {
        return SIZE_MAX;
    }
    float row = y - menu->base.bounds.y - menu->menu_style.padding;
    if (row < 0.0f) return SIZE_MAX;
    size_t index = (size_t)(row / menu->menu_style.row_height);
    return index < menu->item_count ? index : SIZE_MAX;
}

static size_t popover_menu_find_enabled(const PopoverMenu *menu,
                                        size_t start, int direction)
{
    if (menu->item_count == 0) return SIZE_MAX;
    size_t index = start;
    for (size_t attempt = 0; attempt < menu->item_count; ++attempt) {
        if (direction > 0) {
            index = index == SIZE_MAX || index + 1 >= menu->item_count
                ? 0 : index + 1;
        } else {
            index = index == SIZE_MAX || index == 0
                ? menu->item_count - 1 : index - 1;
        }
        if (menu->items[index].enabled) return index;
    }
    return SIZE_MAX;
}

static void popover_menu_activate(PopoverMenu *menu, size_t index)
{
    if (index >= menu->item_count || !menu->items[index].enabled) return;
    PopoverMenuSelectFn callback = menu->on_select;
    void *userdata = menu->userdata;
    menu->open = false;
    menu->pressed_index = SIZE_MAX;
    if (callback) callback(menu, index, userdata);
}

static void popover_menu_event(Widget *self, const void *event)
{
    PopoverMenu *menu = (PopoverMenu *)self;
    const SDL_Event *ev = event;
    if (!ev || !menu->open) return;

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        size_t index = popover_menu_item_at(
            menu, (float)ev->motion.x, (float)ev->motion.y);
        menu->active_index = index < menu->item_count &&
                             menu->items[index].enabled
            ? index : SIZE_MAX;
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        float x = (float)ev->button.x;
        float y = (float)ev->button.y;
        if (!point_in_rect(x, y, &self->bounds)) {
            popover_menu_set_open(menu, false);
            return;
        }
        size_t index = popover_menu_item_at(menu, x, y);
        menu->pressed_index = index < menu->item_count &&
                              menu->items[index].enabled
            ? index : SIZE_MAX;
        menu->active_index = menu->pressed_index;
        return;
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        ev->button.button == SDL_BUTTON_LEFT) {
        size_t index = popover_menu_item_at(
            menu, (float)ev->button.x, (float)ev->button.y);
        size_t pressed = menu->pressed_index;
        menu->pressed_index = SIZE_MAX;
        if (index == pressed && index < menu->item_count) {
            popover_menu_activate(menu, index);
        }
        return;
    }

    if (ev->type != SDL_EVENT_KEY_DOWN) return;
    switch (ev->key.key) {
    case SDLK_ESCAPE:
        popover_menu_set_open(menu, false);
        break;
    case SDLK_DOWN:
        menu->active_index = popover_menu_find_enabled(
            menu, menu->active_index, 1);
        break;
    case SDLK_UP:
        menu->active_index = popover_menu_find_enabled(
            menu, menu->active_index, -1);
        break;
    case SDLK_HOME:
        menu->active_index = SIZE_MAX;
        menu->active_index = popover_menu_find_enabled(
            menu, menu->active_index, 1);
        break;
    case SDLK_END:
        menu->active_index = SIZE_MAX;
        menu->active_index = popover_menu_find_enabled(
            menu, menu->active_index, -1);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        popover_menu_activate(menu, menu->active_index);
        break;
    default:
        break;
    }
}

static bool valid_style(PopoverMenuStyle style)
{
    return isfinite(style.row_height) && style.row_height > 0.0f &&
           isfinite(style.padding) && style.padding >= 0.0f &&
           isfinite(style.horizontal_padding) &&
           style.horizontal_padding >= 0.0f &&
           isfinite(style.corner_radius) && style.corner_radius >= 0.0f &&
           isfinite(style.font_size) && style.font_size > 0.0f;
}

PopoverMenu *popover_menu_create(Rect bounds, const PopoverMenuStyle *style)
{
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h) ||
        bounds.w < 0.0f) {
        return NULL;
    }
    PopoverMenuStyle resolved = style ? *style : popover_menu_style_default();
    if (!valid_style(resolved)) return NULL;

    PopoverMenu *menu = calloc(1, sizeof(*menu));
    if (!menu) return NULL;
    menu->base.bounds = bounds;
    menu->base.style = style_default();
    menu->base.draw = popover_menu_draw;
    menu->base.handle_event = popover_menu_event;
    menu->base.destroy = popover_menu_destroy;
    menu->base.layout = popover_menu_layout;
    menu->menu_style = resolved;
    menu->active_index = SIZE_MAX;
    menu->pressed_index = SIZE_MAX;
    popover_menu_layout(&menu->base);
    return menu;
}

Widget *popover_menu_widget(PopoverMenu *menu)
{
    return menu ? &menu->base : NULL;
}

bool popover_menu_add_item(PopoverMenu *menu, const char *label, bool enabled)
{
    if (!menu || !label || label[0] == '\0' ||
        menu->item_count == SIZE_MAX ||
        menu->item_count >= SIZE_MAX / sizeof(*menu->items)) {
        return false;
    }
    char *label_copy = copy_string(label);
    if (!label_copy) return false;
    if (menu->item_count == menu->item_capacity) {
        size_t capacity = menu->item_capacity == 0 ? 4 : menu->item_capacity * 2;
        if (capacity < menu->item_capacity ||
            capacity > SIZE_MAX / sizeof(*menu->items)) {
            free(label_copy);
            return false;
        }
        PopoverMenuItem *items = realloc(
            menu->items, capacity * sizeof(*menu->items));
        if (!items) {
            free(label_copy);
            return false;
        }
        menu->items = items;
        menu->item_capacity = capacity;
    }
    menu->items[menu->item_count++] = (PopoverMenuItem) {
        .label = label_copy,
        .enabled = enabled,
    };
    popover_menu_layout(&menu->base);
    return true;
}

size_t popover_menu_item_count(const PopoverMenu *menu)
{
    return menu ? menu->item_count : 0;
}

void popover_menu_set_open(PopoverMenu *menu, bool open)
{
    if (!menu) return;
    if (menu->open != open) {
        menu->active_index = SIZE_MAX;
        menu->pressed_index = SIZE_MAX;
    }
    menu->open = open;
}

bool popover_menu_is_open(const PopoverMenu *menu)
{
    return menu && menu->open;
}

void popover_menu_set_select_handler(PopoverMenu *menu,
                                     PopoverMenuSelectFn callback,
                                     void *userdata)
{
    if (!menu) return;
    menu->on_select = callback;
    menu->userdata = userdata;
}

void popover_menu_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
