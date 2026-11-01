#include "tab_view.h"
#include "button.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestContent {
    Widget base;
    int events;
} TestContent;

static void content_event(Widget *widget, const void *event)
{
    (void)event;
    ++((TestContent *)widget)->events;
}

static TestContent *content_create(void)
{
    TestContent *content = calloc(1, sizeof(*content));
    if (!content) return NULL;
    content->base.style = style_default();
    content->base.draw = widget_draw_noop;
    content->base.handle_event = content_event;
    content->base.destroy = widget_destroy_noop;
    return content;
}

static void selection_changed(TabView *view, size_t index, void *userdata)
{
    (void)view;
    (void)index;
    ++*(int *)userdata;
}

static void button_clicked(Button *button, void *userdata)
{
    (void)button;
    ++*(int *)userdata;
}

int main(void)
{
    int button_clicks = 0;
    const VkDrawIcon *fake_icon =
        (const VkDrawIcon *)(const void *)&button_clicks;
    Button *icon_button = button_create_icon(
        (Rect){0.0f, 0.0f, 24.0f, 24.0f}, style_default(), fake_icon);
    if (!icon_button || icon_button->icon != fake_icon) {
        fprintf(stderr, "Failed to create an icon button\n");
        widget_destroy((Widget *)icon_button);
        return 1;
    }
    button_set_click_handler(icon_button, button_clicked, &button_clicks);
    SDL_Event button_ev = {0};
    button_ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button_ev.button.button = SDL_BUTTON_LEFT;
    button_ev.button.x = 12;
    button_ev.button.y = 12;
    button_event((Widget *)icon_button, &button_ev);
    button_ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button_ev.button.x = 40;
    button_event((Widget *)icon_button, &button_ev);
    if (button_clicks != 0) {
        fprintf(stderr, "Icon button activated after release outside\n");
        widget_destroy((Widget *)icon_button);
        return 1;
    }
    button_ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button_ev.button.x = 12;
    button_event((Widget *)icon_button, &button_ev);
    button_ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    button_event((Widget *)icon_button, &button_ev);
    if (button_clicks != 1) {
        fprintf(stderr, "Icon button did not activate on a complete click\n");
        widget_destroy((Widget *)icon_button);
        return 1;
    }
    widget_destroy((Widget *)icon_button);

    TabView *view = tab_view_create((Rect){10.0f, 20.0f, 300.0f, 220.0f},
                                    NULL);
    TestContent *first = content_create();
    TestContent *second = content_create();
    TestContent *third = content_create();
    int selection_count = 0;
    if (!view || !first || !second || !third) {
        fprintf(stderr, "Failed to create TabView fixtures\n");
        widget_destroy(tab_view_widget(view));
        widget_destroy((Widget *)first);
        widget_destroy((Widget *)second);
        widget_destroy((Widget *)third);
        return 1;
    }

    tab_view_set_selection_handler(view, selection_changed, &selection_count);
    if (!tab_view_add_tab(view, "main.c", (Widget *)first) ||
        !tab_view_add_tab(view, "layout.c", (Widget *)second) ||
        !tab_view_add_tab(view, "scroll_view.c", (Widget *)third)) {
        fprintf(stderr, "Failed to add editor tabs\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }
    if (tab_view_tab_count(view) != 3 ||
        tab_view_active_index(view) != 0 ||
        tab_view_content(view, 1) != (Widget *)second ||
        strcmp(tab_view_title(view, 2), "scroll_view.c") != 0 ||
        first->base.bounds.x != 10.0f || first->base.bounds.y != 54.0f ||
        first->base.bounds.w != 300.0f || first->base.bounds.h != 186.0f) {
        fprintf(stderr, "Initial tab state or content layout is incorrect\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }
    const VkDrawIcon *test_icon = (const VkDrawIcon *)(const void *)first;
    if (tab_view_icon(view, 1) != NULL ||
        !tab_view_set_icon(view, 1, test_icon) ||
        tab_view_icon(view, 1) != test_icon ||
        !tab_view_set_icon(view, 1, NULL) ||
        tab_view_icon(view, 1) != NULL ||
        tab_view_set_icon(view, 3, test_icon)) {
        fprintf(stderr, "Per-tab icon assignment did not preserve overrides\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }

    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 150;
    event.button.y = 35;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    if (tab_view_active_index(view) != 1 || selection_count != 1 ||
        first->events != 0 || second->events != 0) {
        fprintf(stderr, "Clicking a tab did not switch its active content\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }

    event.type = SDL_EVENT_KEY_DOWN;
    event.key.mod = SDL_KMOD_CTRL;
    event.key.key = SDLK_TAB;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    if (tab_view_active_index(view) != 2 || selection_count != 2) {
        fprintf(stderr, "Ctrl+Tab did not advance to the next tab\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }
    event.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    if (tab_view_active_index(view) != 1 || selection_count != 3) {
        fprintf(stderr, "Ctrl+Shift+Tab did not select the previous tab\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 150;
    event.button.y = 80;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    if (second->events != 1 || first->events != 0 || third->events != 0) {
        fprintf(stderr, "Events were not routed to only the active tab\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }

    if (!tab_view_set_active(view, 2) || tab_view_active_index(view) != 2 ||
        tab_view_set_active(view, 3) || tab_view_title(view, 3) != NULL) {
        fprintf(stderr, "Programmatic tab selection accepted invalid state\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }
    Widget *taken = tab_view_take_tab(view, 0);
    if (taken != (Widget *)first || taken->parent ||
        tab_view_tab_count(view) != 2 || tab_view_active_index(view) != 1 ||
        strcmp(tab_view_title(view, 0), "layout.c") != 0 ||
        selection_count != 5) {
        fprintf(stderr, "Taking a tab did not preserve selection and ownership\n");
        widget_destroy(tab_view_widget(view));
        widget_destroy(taken);
        return 1;
    }
    widget_destroy(taken);

    widget_set_bounds(tab_view_widget(view),
                      (Rect){0.0f, 0.0f, 400.0f, 300.0f});
    if (second->base.bounds.x != 0.0f || second->base.bounds.y != 34.0f ||
        second->base.bounds.w != 400.0f || second->base.bounds.h != 266.0f) {
        fprintf(stderr, "Resizing the TabView did not relayout content\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }

    taken = tab_view_take_tab(view, 0);
    if (taken != (Widget *)second || tab_view_tab_count(view) != 1 ||
        tab_view_active_index(view) != 0 || selection_count != 6) {
        fprintf(stderr, "Removing the active tab did not select its neighbor\n");
        widget_destroy(tab_view_widget(view));
        widget_destroy(taken);
        return 1;
    }
    widget_destroy(taken);
    taken = tab_view_take_tab(view, 0);
    if (taken != (Widget *)third || tab_view_tab_count(view) != 0 ||
        tab_view_active_index(view) != SIZE_MAX ||
        tab_view_take_tab(view, 0) != NULL || selection_count != 6) {
        fprintf(stderr, "Removing the final tab did not clear the selection\n");
        widget_destroy(tab_view_widget(view));
        widget_destroy(taken);
        return 1;
    }
    widget_destroy(taken);
    widget_destroy(tab_view_widget(view));

    view = tab_view_create((Rect){0.0f, 0.0f, 200.0f, 100.0f}, NULL);
    first = content_create();
    if (!view || !first ||
        !tab_view_add_tab(view, "close-me.c", (Widget *)first)) {
        fprintf(stderr, "Failed to create closable tab fixture\n");
        widget_destroy(tab_view_widget(view));
        widget_destroy((Widget *)first);
        return 1;
    }
    event = (SDL_Event){0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 88;
    event.button.y = 12;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    tab_view_widget(view)->handle_event(tab_view_widget(view), &event);
    if (tab_view_tab_count(view) != 0 ||
        tab_view_active_index(view) != SIZE_MAX) {
        fprintf(stderr, "Clicking the close control did not close the tab\n");
        widget_destroy(tab_view_widget(view));
        return 1;
    }
    widget_destroy(tab_view_widget(view));

    puts("TabView tests passed!");
    return 0;
}
