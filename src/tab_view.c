/**
 * tab_view.c - Editor-style tabs with per-tab widget content.
 */

#include "tab_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"

typedef struct TabItem {
    char *title;
    Widget *content;
    Button *close_button;
    const VkDrawIcon *icon;
    float width;
} TabItem;

struct TabView {
    Widget base;
    TabViewStyle view_style;
    TabItem *tabs;
    size_t tab_count;
    size_t tab_capacity;
    size_t active_index;
    size_t hovered_index;
    const VkDrawIcon *close_icon;
    bool focused;
    TabViewSelectionFn on_selection;
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

TabViewStyle tab_view_style_default(void)
{
    return (TabViewStyle) {
        .tab_height = 34.0f,
        .tab_min_width = 112.0f,
        .tab_max_width = 240.0f,
        .tab_font_size = 13.0f,
        .tab_icon_size = 15.0f,
        .horizontal_padding = 14.0f,
        .corner_radius = 6.0f,
        .background = {255, 255, 255, 255},
        .strip_background = {229, 230, 233, 255},
        .active_tab = {255, 255, 255, 255},
        .inactive_tab = {0, 0, 0, 0},
        .hover_tab = {217, 219, 223, 255},
        .border = {198, 200, 204, 255},
        .text = {79, 81, 85, 255},
        .active_text = {32, 33, 36, 255},
    };
}

static void tab_view_layout(Widget *self)
{
    TabView *view = (TabView *)self;
    float content_y = self->bounds.y + view->view_style.tab_height;
    float content_height = fmaxf(0.0f, self->bounds.h -
                                 view->view_style.tab_height);
    for (size_t i = 0; i < view->tab_count; ++i) {
        view->tabs[i].content->bounds = (Rect) {
            .x = self->bounds.x,
            .y = content_y,
            .w = self->bounds.w,
            .h = content_height,
        };
        widget_layout(view->tabs[i].content);
    }
}

static void tab_view_destroy(Widget *self)
{
    TabView *view = (TabView *)self;
    for (size_t i = 0; i < view->tab_count; ++i) {
        free(view->tabs[i].title);
        widget_destroy((Widget *)view->tabs[i].close_button);
    }
    free(view->tabs);
}

static float tab_width_scale(const TabView *view)
{
    float total_width = 0.0f;
    for (size_t i = 0; i < view->tab_count; ++i) {
        total_width += view->tabs[i].width;
    }
    return total_width > view->base.bounds.w && total_width > 0.0f
        ? view->base.bounds.w / total_width : 1.0f;
}

static float tab_item_width(const TabView *view, const char *title, bool icon)
{
    float title_width = (float)strlen(title) *
                        view->view_style.tab_font_size * 0.55f;
    if (g_draw) {
        float measured_width = 0.0f;
        float measured_height = 0.0f;
        if (vk_draw_measure_text(g_draw, title, view->view_style.tab_font_size,
                                 0, &measured_width, &measured_height)) {
            title_width = measured_width;
        }
    }
    float icon_width = icon ? view->view_style.tab_icon_size + 7.0f : 0.0f;
    return fminf(view->view_style.tab_max_width,
        fmaxf(view->view_style.tab_min_width,
              title_width + icon_width +
                  view->view_style.horizontal_padding * 2.0f + 20.0f));
}

static Rect tab_item_bounds(const TabView *view, size_t index)
{
    float scale = tab_width_scale(view);
    float x = view->base.bounds.x;
    for (size_t i = 0; i < index; ++i) {
        x += view->tabs[i].width * scale;
    }
    return (Rect) {
        .x = x,
        .y = view->base.bounds.y,
        .w = view->tabs[index].width * scale,
        .h = view->view_style.tab_height,
    };
}

static Rect tab_close_button_bounds(const TabView *view, size_t index)
{
    Rect tab = tab_item_bounds(view, index);
    float size = fminf(20.0f, tab.w);
    float padding = fminf(view->view_style.horizontal_padding,
                          fmaxf(0.0f, tab.w - size));
    return (Rect) {
        .x = tab.x + tab.w - padding - size,
        .y = tab.y + (tab.h - size) * 0.5f,
        .w = size,
        .h = size,
    };
}

static void sync_close_button_bounds(TabView *view)
{
    for (size_t i = 0; i < view->tab_count; ++i) {
        view->tabs[i].close_button->base.bounds =
            tab_close_button_bounds(view, i);
    }
}

static void close_tab_clicked(Button *button, void *userdata)
{
    TabView *view = userdata;
    for (size_t i = 0; i < view->tab_count; ++i) {
        if (view->tabs[i].close_button == button) {
            Widget *content = tab_view_take_tab(view, i);
            widget_destroy(content);
            return;
        }
    }
}

static void tab_view_draw(Widget *self, struct VkContext *context)
{
    TabView *view = (TabView *)self;
    if (!g_draw) return;

    const Rect *bounds = &self->bounds;
    float tab_height = fminf(view->view_style.tab_height, bounds->h);
    vk_draw_rounded_rect(g_draw, bounds->x, bounds->y + tab_height,
                         bounds->w, fmaxf(0.0f, bounds->h - tab_height),
                         0.0f, 0.5f,
                         view->view_style.background);
    vk_draw_rounded_rect(g_draw, bounds->x, bounds->y, bounds->w, tab_height,
                         0.0f, 0.5f,
                         view->view_style.strip_background);

    if (view->tab_count > 0 &&
        vk_draw_clip_push(g_draw, bounds->x, bounds->y,
                          bounds->w, tab_height)) {
        float scale = tab_width_scale(view);
        float tab_x = bounds->x;
        sync_close_button_bounds(view);
        for (size_t i = 0; i < view->tab_count; ++i) {
            Rect tab = {
                .x = tab_x,
                .y = bounds->y,
                .w = view->tabs[i].width * scale,
                .h = view->view_style.tab_height,
            };
            tab_x += tab.w;
            bool active = i == view->active_index;
            const uint8_t *fill = active ? view->view_style.active_tab :
                (i == view->hovered_index ? view->view_style.hover_tab :
                                            view->view_style.inactive_tab);
            if (fill[3] > 0) {
                vk_draw_rounded_rect(g_draw, tab.x, tab.y,
                                     tab.w, tab.h,
                                     0.0f, 0.5f, fill);
            }
            float padding = view->view_style.horizontal_padding;
            Rect close_bounds = view->tabs[i].close_button->base.bounds;
            float text_x = tab.x + padding;
            if (view->tabs[i].icon) {
                float icon_size = fminf(view->view_style.tab_icon_size,
                                        tab.h - 4.0f);
                float icon_y = floorf(tab.y + (tab.h - icon_size) * 0.5f +
                                      0.5f);
                vk_draw_icon(g_draw, view->tabs[i].icon,
                             floorf(text_x + 0.5f), icon_y,
                             icon_size, icon_size,
                             active ? view->view_style.active_text :
                                      view->view_style.text);
                text_x += icon_size + 7.0f;
            }
            float text_width = fmaxf(
                0.0f, close_bounds.x - text_x - 6.0f);
            Rect text_clip = {
                text_x, tab.y, text_width, tab.h,
            };
            if (text_width > 0.0f &&
                vk_draw_clip_push(g_draw, text_clip.x, text_clip.y,
                                  text_clip.w, text_clip.h)) {
                (void)vk_draw_text_aligned_styled(
                    g_draw, view->tabs[i].title, text_x, tab.y,
                    text_width, tab.h, view->view_style.tab_font_size, 0,
                    active ? view->view_style.active_text :
                             view->view_style.text,
                    CGUI_ALIGN_LEFT);
                vk_draw_clip_pop(g_draw);
            }
            if (active || i == view->hovered_index) {
                Button *close_button = view->tabs[i].close_button;
                memcpy(close_button->base.style.text_color,
                       active ? view->view_style.active_text :
                                view->view_style.text, 4);
                button_draw((Widget *)close_button, context);
            }
            if (!active && i + 1 < view->tab_count) {
                vk_draw_rounded_rect(g_draw, tab.x + tab.w - 0.5f,
                                     tab.y + 8.0f, 1.0f,
                                     fmaxf(0.0f, tab.h - 16.0f),
                                     0.5f, 0.5f, view->view_style.border);
            }
        }
        vk_draw_clip_pop(g_draw);
    }
    if (view->active_index < view->tab_count) {
        float active_end = bounds->x;
        for (size_t i = 0; i <= view->active_index; ++i) {
            active_end += view->tabs[i].width * tab_width_scale(view);
        }
        float active_start = active_end -
            view->tabs[view->active_index].width * tab_width_scale(view);
        float line_y = bounds->y + tab_height - 1.0f;
        if (active_start > bounds->x) {
            vk_draw_rounded_rect(g_draw, bounds->x, line_y,
                                 active_start - bounds->x, 1.0f,
                                 0.5f, 0.5f, view->view_style.border);
        }
        if (active_end < bounds->x + bounds->w) {
            vk_draw_rounded_rect(g_draw, active_end, line_y,
                                 bounds->x + bounds->w - active_end, 1.0f,
                                 0.5f, 0.5f, view->view_style.border);
        }
    } else {
        vk_draw_rounded_rect(g_draw, bounds->x,
                             bounds->y + tab_height - 1.0f,
                             bounds->w, 1.0f, 0.5f, 0.5f,
                             view->view_style.border);
    }

    if (view->tab_count > 0 && view->active_index < view->tab_count) {
        Widget *content = view->tabs[view->active_index].content;
        if (content->draw &&
            vk_draw_clip_push(g_draw, bounds->x,
                              bounds->y + tab_height,
                              bounds->w,
                              fmaxf(0.0f, bounds->h - tab_height))) {
            content->draw(content, context);
            vk_draw_clip_pop(g_draw);
        }
    }
}

static void notify_selection(TabView *view)
{
    if (view->on_selection && view->tab_count > 0) {
        view->on_selection(view, view->active_index, view->userdata);
    }
}

static void tab_view_event(Widget *self, const void *event)
{
    TabView *view = (TabView *)self;
    const SDL_Event *ev = event;
    if (!ev) return;

    if (ev->type == SDL_EVENT_KEY_DOWN && view->focused &&
        (ev->key.mod & SDL_KMOD_CTRL) != 0 &&
        ev->key.key == SDLK_TAB && view->tab_count > 1) {
        size_t next = view->active_index;
        if ((ev->key.mod & SDL_KMOD_SHIFT) != 0) {
            next = next == 0 ? view->tab_count - 1 : next - 1;
        } else {
            next = (next + 1) % view->tab_count;
        }
        (void)tab_view_set_active(view, next);
        return;
    }

    sync_close_button_bounds(view);
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        for (size_t i = 0; i < view->tab_count; ++i) {
            button_event((Widget *)view->tabs[i].close_button, event);
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP &&
               ev->button.button == SDL_BUTTON_LEFT) {
        for (size_t i = 0; i < view->tab_count; ++i) {
            Button *button = view->tabs[i].close_button;
            bool was_pressed = button->pressed;
            if (was_pressed || point_in_rect((float)ev->button.x,
                                              (float)ev->button.y,
                                              &button->base.bounds)) {
                button_event((Widget *)button, event);
                if (was_pressed) return;
            }
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        float x = ev->motion.x;
        float y = ev->motion.y;
        view->hovered_index = SIZE_MAX;
        if (y >= self->bounds.y &&
            y < self->bounds.y + view->view_style.tab_height &&
            x >= self->bounds.x && x < self->bounds.x + self->bounds.w &&
            view->tab_count > 0) {
            float scale = tab_width_scale(view);
            float tab_x = self->bounds.x;
            for (size_t i = 0; i < view->tab_count; ++i) {
                Rect tab = {
                    .x = tab_x,
                    .y = self->bounds.y,
                    .w = view->tabs[i].width * scale,
                    .h = view->view_style.tab_height,
                };
                if (point_in_rect(x, y, &tab)) {
                    view->hovered_index = i;
                    break;
                }
                tab_x += tab.w;
            }
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        float x = (float)ev->button.x;
        float y = (float)ev->button.y;
        view->focused = point_in_rect(x, y, &self->bounds);
        for (size_t i = 0; i < view->tab_count; ++i) {
            button_event((Widget *)view->tabs[i].close_button, event);
        }
        if (y >= self->bounds.y &&
            y < self->bounds.y + view->view_style.tab_height &&
            x >= self->bounds.x &&
            x < self->bounds.x + self->bounds.w &&
            view->tab_count > 0) {
            float scale = tab_width_scale(view);
            float tab_x = self->bounds.x;
            size_t index = SIZE_MAX;
            for (size_t i = 0; i < view->tab_count; ++i) {
                Rect tab_bounds = {
                    .x = tab_x,
                    .y = self->bounds.y,
                    .w = view->tabs[i].width * scale,
                    .h = view->view_style.tab_height,
                };
                if (point_in_rect(x, y, &tab_bounds)) {
                    index = i;
                    break;
                }
                tab_x += tab_bounds.w;
            };
            if (index == SIZE_MAX) return;
            view->hovered_index = index;
            Rect close_bounds =
                view->tabs[index].close_button->base.bounds;
            if (point_in_rect(x, y, &close_bounds)) {
                return;
            } else {
                (void)tab_view_set_active(view, index);
            }
            return;
        }
    }

    if ((ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         ev->type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        !point_in_rect((float)ev->button.x, (float)ev->button.y,
                       &self->bounds)) {
        return;
    }

    if (view->tab_count > 0 && view->active_index < view->tab_count) {
        Widget *content = view->tabs[view->active_index].content;
        if (content->handle_event) content->handle_event(content, event);
    }
}

TabView *tab_view_create(Rect bounds, const TabViewStyle *style)
{
    if (!isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h) ||
        bounds.w < 0.0f || bounds.h < 0.0f) {
        return NULL;
    }
    TabViewStyle resolved = style ? *style : tab_view_style_default();
    if (!isfinite(resolved.tab_height) || resolved.tab_height <= 0.0f ||
        !isfinite(resolved.tab_min_width) || resolved.tab_min_width <= 0.0f ||
        !isfinite(resolved.tab_max_width) ||
        resolved.tab_max_width < resolved.tab_min_width ||
        !isfinite(resolved.tab_font_size) || resolved.tab_font_size <= 0.0f ||
        !isfinite(resolved.tab_icon_size) || resolved.tab_icon_size <= 0.0f ||
        !isfinite(resolved.horizontal_padding) ||
        resolved.horizontal_padding < 0.0f ||
        !isfinite(resolved.corner_radius) || resolved.corner_radius < 0.0f) {
        return NULL;
    }

    TabView *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->base.bounds = bounds;
    view->base.style = style_default();
    view->base.draw = tab_view_draw;
    view->base.handle_event = tab_view_event;
    view->base.destroy = tab_view_destroy;
    view->base.layout = tab_view_layout;
    view->view_style = resolved;
    view->hovered_index = SIZE_MAX;
    return view;
}

Widget *tab_view_widget(TabView *view)
{
    return view ? &view->base : NULL;
}

bool tab_view_add_tab(TabView *view, const char *title, Widget *content)
{
    if (!view || !title || !title[0] || !content || content->parent) {
        return false;
    }
    char *title_copy = copy_string(title);
    if (!title_copy) return false;
    float tab_width = tab_item_width(view, title, false);
    Style close_style = {
        .corner_radius = 10.0f,
        .bg = {0, 0, 0, 0},
        .border_color = {0, 0, 0, 0},
        .text_size = 16.0f,
        .text_color = {79, 81, 85, 255},
    };
    Button *close_button = button_create(
        (Rect){0.0f, 0.0f, 20.0f, 20.0f}, close_style, "×");
    if (!close_button) {
        free(title_copy);
        return false;
    }
    button_set_icon(close_button, view->close_icon);
    button_set_icon_size(close_button, 12.0f);
    const uint8_t hover_color[4] = {128, 132, 138, 45};
    const uint8_t press_color[4] = {128, 132, 138, 75};
    button_set_interaction_colors(close_button, hover_color, press_color);
    button_set_click_handler(close_button, close_tab_clicked, view);

    if (view->tab_count == view->tab_capacity) {
        size_t capacity = view->tab_capacity ? view->tab_capacity * 2 : 4;
        if (capacity < view->tab_capacity ||
            capacity > SIZE_MAX / sizeof(*view->tabs)) {
            widget_destroy((Widget *)close_button);
            free(title_copy);
            return false;
        }
        TabItem *tabs = realloc(view->tabs, capacity * sizeof(*tabs));
        if (!tabs) {
            widget_destroy((Widget *)close_button);
            free(title_copy);
            return false;
        }
        view->tabs = tabs;
        view->tab_capacity = capacity;
    }
    if (!widget_add_child(&view->base, content)) {
        widget_destroy((Widget *)close_button);
        free(title_copy);
        return false;
    }
    view->tabs[view->tab_count++] = (TabItem) {
        .title = title_copy,
        .content = content,
        .close_button = close_button,
        .width = tab_width,
    };
    if (view->tab_count == 1) view->active_index = 0;
    tab_view_layout(&view->base);
    return true;
}

bool tab_view_set_icon(TabView *view, size_t index, const VkDrawIcon *icon)
{
    if (!view || index >= view->tab_count) return false;
    view->tabs[index].icon = icon;
    view->tabs[index].width =
        tab_item_width(view, view->tabs[index].title, icon != NULL);
    return true;
}

const VkDrawIcon *tab_view_icon(const TabView *view, size_t index)
{
    return view && index < view->tab_count ? view->tabs[index].icon : NULL;
}

void tab_view_set_close_icon(TabView *view, const VkDrawIcon *icon)
{
    if (!view) return;
    view->close_icon = icon;
    for (size_t i = 0; i < view->tab_count; ++i) {
        button_set_icon(view->tabs[i].close_button, icon);
    }
}

Widget *tab_view_take_tab(TabView *view, size_t index)
{
    if (!view || index >= view->tab_count) return NULL;
    Widget *content = view->tabs[index].content;
    if (content->parent != &view->base) return NULL;

    bool removed_active = index == view->active_index;
    free(view->tabs[index].title);
    widget_destroy((Widget *)view->tabs[index].close_button);
    memmove(&view->tabs[index], &view->tabs[index + 1],
            (view->tab_count - index - 1) * sizeof(*view->tabs));
    --view->tab_count;
    if (view->tab_count == 0) {
        view->active_index = 0;
    } else if (removed_active) {
        if (view->active_index >= view->tab_count) {
            view->active_index = view->tab_count - 1;
        }
    } else if (index < view->active_index) {
        --view->active_index;
    }
    if (view->hovered_index == index) view->hovered_index = SIZE_MAX;
    else if (view->hovered_index != SIZE_MAX && view->hovered_index > index)
        --view->hovered_index;

    if (!widget_remove_child(&view->base, content)) return NULL;
    tab_view_layout(&view->base);
    notify_selection(view);
    return content;
}

bool tab_view_set_active(TabView *view, size_t index)
{
    if (!view || index >= view->tab_count) return false;
    if (view->active_index != index) {
        view->active_index = index;
        notify_selection(view);
    }
    return true;
}

size_t tab_view_tab_count(const TabView *view)
{
    return view ? view->tab_count : 0;
}

size_t tab_view_active_index(const TabView *view)
{
    return view && view->tab_count ? view->active_index : SIZE_MAX;
}

const char *tab_view_title(const TabView *view, size_t index)
{
    return view && index < view->tab_count ? view->tabs[index].title : NULL;
}

Widget *tab_view_content(const TabView *view, size_t index)
{
    return view && index < view->tab_count ? view->tabs[index].content : NULL;
}

void tab_view_set_selection_handler(TabView *view,
                                    TabViewSelectionFn callback,
                                    void *userdata)
{
    if (!view) return;
    view->on_selection = callback;
    view->userdata = userdata;
}

void tab_view_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
    button_set_draw_context(draw);
}
