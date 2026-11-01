#include "popover_menu.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Selection {
    size_t count;
    size_t index;
} Selection;

static void menu_selected(PopoverMenu *menu, size_t index, void *userdata)
{
    (void)menu;
    Selection *selection = userdata;
    ++selection->count;
    selection->index = index;
}

static void send_key(PopoverMenu *menu, SDL_Keycode key)
{
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    popover_menu_widget(menu)->handle_event(
        popover_menu_widget(menu), &event);
}

static void send_mouse(PopoverMenu *menu, Uint32 type, int x, int y)
{
    SDL_Event event = {0};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.x = (float)x;
        event.motion.y = (float)y;
    } else {
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = x;
        event.button.y = y;
    }
    popover_menu_widget(menu)->handle_event(
        popover_menu_widget(menu), &event);
}

int main(void)
{
    PopoverMenu *menu = popover_menu_create(
        (Rect){40.0f, 50.0f, 220.0f, 1.0f}, NULL);
    Selection selection = {0};
    if (!menu ||
        !popover_menu_add_item(menu, "Open", true) ||
        !popover_menu_add_item(menu, "Unavailable", false) ||
        !popover_menu_add_item(menu, "Duplicate", true) ||
        !popover_menu_add_item(menu, "Remove", false)) {
        fprintf(stderr, "Failed to create popover menu items\n");
        widget_destroy(popover_menu_widget(menu));
        return 1;
    }
    popover_menu_set_select_handler(menu, menu_selected, &selection);
    Widget *widget = popover_menu_widget(menu);
    if (popover_menu_item_count(menu) != 4 ||
        fabsf(widget->bounds.h - 132.0f) > 0.01f ||
        popover_menu_add_item(menu, "", true) ||
        popover_menu_add_item(NULL, "Invalid", true)) {
        fprintf(stderr, "Popover menu sizing or item validation failed\n");
        widget_destroy(widget);
        return 1;
    }

    popover_menu_set_open(menu, true);
    send_key(menu, SDLK_DOWN);
    send_key(menu, SDLK_DOWN);
    send_key(menu, SDLK_DOWN);
    send_key(menu, SDLK_RETURN);
    if (selection.count != 1 || selection.index != 0 ||
        popover_menu_is_open(menu)) {
        fprintf(stderr, "Keyboard navigation did not skip disabled items\n");
        widget_destroy(widget);
        return 1;
    }

    popover_menu_set_open(menu, true);
    send_key(menu, SDLK_END);
    send_key(menu, SDLK_RETURN);
    if (selection.count != 2 || selection.index != 2) {
        fprintf(stderr, "End/Enter did not activate the last enabled item\n");
        widget_destroy(widget);
        return 1;
    }

    popover_menu_set_open(menu, true);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_DOWN, 60, 125);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_UP, 300, 180);
    if (!popover_menu_is_open(menu) || selection.count != 2) {
        fprintf(stderr, "Releasing outside activated or closed the menu\n");
        widget_destroy(widget);
        return 1;
    }
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_DOWN, 60, 125);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_UP, 60, 125);
    if (selection.count != 3 || selection.index != 2 ||
        popover_menu_is_open(menu)) {
        fprintf(stderr, "Pointer click did not activate the selected item\n");
        widget_destroy(widget);
        return 1;
    }

    popover_menu_set_open(menu, true);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_DOWN, 60, 95);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_UP, 60, 95);
    if (!popover_menu_is_open(menu) || selection.count != 3) {
        fprintf(stderr, "Disabled item was activated\n");
        widget_destroy(widget);
        return 1;
    }
    send_key(menu, SDLK_ESCAPE);
    if (popover_menu_is_open(menu)) {
        fprintf(stderr, "Escape did not dismiss the menu\n");
        widget_destroy(widget);
        return 1;
    }

    popover_menu_set_open(menu, true);
    send_mouse(menu, SDL_EVENT_MOUSE_BUTTON_DOWN, 300, 300);
    if (popover_menu_is_open(menu)) {
        fprintf(stderr, "Outside click did not dismiss the menu\n");
        widget_destroy(widget);
        return 1;
    }
    widget_destroy(widget);

    PopoverMenuStyle invalid = popover_menu_style_default();
    invalid.row_height = NAN;
    if (popover_menu_create((Rect){0.0f, 0.0f, 100.0f, 100.0f},
                            &invalid) != NULL ||
        popover_menu_create((Rect){0.0f, 0.0f, -1.0f, 100.0f},
                            NULL) != NULL) {
        fprintf(stderr, "Popover menu accepted invalid geometry or style\n");
        return 1;
    }
    puts("Popover menu tests passed!");
    return 0;
}
