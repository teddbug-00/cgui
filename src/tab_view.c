/**
 * tab_view.c - Editor-style tabs with per-tab widget content.
 */

#include "tab_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct TabItem {
    char *title;
    Widget *content;
} TabItem;

struct TabView {
    Widget base;
    TabViewStyle view_style;
    TabItem *tabs;
    size_t tab_count;
    size_t tab_capacity;
    size_t active_index;
    size_t hovered_index;
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
        .tab_height = 36.0f,
        .corner_radius = 7.0f,
        .background = {255, 255, 255, 255},
        .strip_background = {239, 240, 243, 255},
        .active_tab = {255, 255, 255, 255},
        .inactive_tab = {230, 232, 236, 255},
        .hover_tab = {246, 247, 249, 255},
        .border = {210, 213, 219, 255},
        .text = {89, 94, 103, 255},
        .active_text = {35, 39, 46, 255},
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
    }
    free(view->tabs);
}

static Rect tab_bounds(const TabView *view, size_t index)
{
    float width = view->tab_count
        ? view->base.bounds.w / (float)view->tab_count : 0.0f;
    return (Rect) {
        .x = view->base.bounds.x + width * (float)index,
        .y = view->base.bounds.y,
        .w = width,
        .h = view->view_style.tab_height,
    };
}

static void tab_view_draw(Widget *self, struct VkContext *context)
{
    TabView *view = (TabView *)self;
    if (!g_draw) return;

    const Rect *bounds = &self->bounds;
    float tab_height = fminf(view->view_style.tab_height, bounds->h);
    vk_draw_rounded_rect(g_draw, bounds->x, bounds->y + tab_height,
                         bounds->w, fmaxf(0.0f, bounds->h - tab_height),
                         view->view_style.corner_radius, 0.5f,
                         view->view_style.background);
    vk_draw_rounded_rect(g_draw, bounds->x, bounds->y, bounds->w, tab_height,
                         view->view_style.corner_radius, 0.5f,
                         view->view_style.strip_background);
    vk_draw_rounded_rect(g_draw, bounds->x,
                         bounds->y + tab_height - 1.0f,
                         bounds->w, 1.0f, 0.5f, 0.5f,
                         view->view_style.border);

    for (size_t i = 0; i < view->tab_count; ++i) {
        Rect tab = tab_bounds(view, i);
        bool active = i == view->active_index;
        const uint8_t *fill = active ? view->view_style.active_tab :
            (i == view->hovered_index ? view->view_style.hover_tab :
                                        view->view_style.inactive_tab);
        vk_draw_rounded_rect(g_draw, tab.x + 2.0f, tab.y + 3.0f,
                             fmaxf(0.0f, tab.w - 4.0f),
                             fmaxf(0.0f, tab.h - 3.0f),
                             view->view_style.corner_radius, 0.5f, fill);
        float close_width = fminf(24.0f, tab.w * 0.3f);
        float text_x = tab.x + 12.0f;
        float text_width = fmaxf(0.0f, tab.w - close_width - 20.0f);
        Rect text_clip = { text_x, tab.y, text_width, tab.h };
        if (text_width > 0.0f &&
            vk_draw_clip_push(g_draw, text_clip.x, text_clip.y,
                              text_clip.w, text_clip.h)) {
            (void)vk_draw_text(g_draw, view->tabs[i].title,
                               text_x, tab.y + (tab.h - 14.0f) * 0.5f,
                               14.0f, active ? view->view_style.active_text :
                                               view->view_style.text);
            vk_draw_clip_pop(g_draw);
        }
        float close_x = tab.x + tab.w - close_width * 0.5f;
        float close_y = tab.y + tab.h * 0.5f;
        if (active || i == view->hovered_index) {
            (void)vk_draw_text(g_draw, "×", close_x - 5.0f, close_y - 8.0f,
                               16.0f, view->view_style.text);
        }
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

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        float x = ev->motion.x;
        float y = ev->motion.y;
        view->hovered_index = SIZE_MAX;
        if (y >= self->bounds.y &&
            y < self->bounds.y + view->view_style.tab_height &&
            x >= self->bounds.x && x < self->bounds.x + self->bounds.w &&
            view->tab_count > 0) {
            float tab_width = self->bounds.w / (float)view->tab_count;
            if (tab_width > 0.0f) {
                size_t index = (size_t)((x - self->bounds.x) / tab_width);
                if (index < view->tab_count) view->hovered_index = index;
            }
        }
    }

    if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        float x = (float)ev->button.x;
        float y = (float)ev->button.y;
        view->focused = point_in_rect(x, y, &self->bounds);
        if (y >= self->bounds.y &&
            y < self->bounds.y + view->view_style.tab_height &&
            x >= self->bounds.x &&
            x < self->bounds.x + self->bounds.w &&
            view->tab_count > 0) {
            float tab_width = self->bounds.w / (float)view->tab_count;
            size_t index = (size_t)((x - self->bounds.x) / tab_width);
            if (index >= view->tab_count) return;
            Rect tab = tab_bounds(view, index);
            float close_width = fminf(24.0f, tab.w * 0.3f);
            if (x >= tab.x + tab.w - close_width) {
                Widget *removed = tab_view_take_tab(view, index);
                widget_destroy(removed);
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

    if (view->tab_count == view->tab_capacity) {
        size_t capacity = view->tab_capacity ? view->tab_capacity * 2 : 4;
        if (capacity < view->tab_capacity ||
            capacity > SIZE_MAX / sizeof(*view->tabs)) {
            free(title_copy);
            return false;
        }
        TabItem *tabs = realloc(view->tabs, capacity * sizeof(*tabs));
        if (!tabs) {
            free(title_copy);
            return false;
        }
        view->tabs = tabs;
        view->tab_capacity = capacity;
    }
    if (!widget_add_child(&view->base, content)) {
        free(title_copy);
        return false;
    }
    view->tabs[view->tab_count++] = (TabItem) {
        .title = title_copy,
        .content = content,
    };
    if (view->tab_count == 1) view->active_index = 0;
    tab_view_layout(&view->base);
    return true;
}

Widget *tab_view_take_tab(TabView *view, size_t index)
{
    if (!view || index >= view->tab_count) return NULL;
    Widget *content = view->tabs[index].content;
    if (content->parent != &view->base) return NULL;

    bool removed_active = index == view->active_index;
    free(view->tabs[index].title);
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
}
