#include "modal_dialog.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TestContent {
    Widget base;
    size_t events;
} TestContent;

typedef struct ActionResult {
    size_t count;
    size_t index;
} ActionResult;

static void content_event(Widget *widget, const void *event)
{
    const SDL_Event *ev = event;
    if (ev && ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        ++((TestContent *)widget)->events;
}

static void action_selected(ModalDialog *dialog, size_t index, void *userdata)
{
    (void)dialog;
    ActionResult *result = userdata;
    ++result->count;
    result->index = index;
}

static void send_mouse(ModalDialog *dialog, Uint32 type, int x, int y)
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
    modal_dialog_widget(dialog)->handle_event(
        modal_dialog_widget(dialog), &event);
}

int main(void)
{
    ModalDialog *dialog = modal_dialog_create(
        (Rect){0.0f, 0.0f, 800.0f, 600.0f},
        "Delete document?", "This action cannot be undone.", NULL);
    TestContent *content = calloc(1, sizeof(*content));
    ActionResult result = {0};
    if (!dialog || !content) {
        fprintf(stderr, "Failed to create modal dialog fixtures\n");
        widget_destroy(modal_dialog_widget(dialog));
        free(content);
        return 1;
    }
    content->base.style = style_default();
    content->base.draw = widget_draw_noop;
    content->base.handle_event = content_event;
    content->base.destroy = widget_destroy_noop;

    if (!modal_dialog_set_content(dialog, (Widget *)content, 60.0f) ||
        !modal_dialog_add_action(dialog, "Cancel", false) ||
        !modal_dialog_add_action(dialog, "Delete", true)) {
        fprintf(stderr, "Failed to configure modal dialog\n");
        widget_destroy(modal_dialog_widget(dialog));
        return 1;
    }
    modal_dialog_set_action_handler(dialog, action_selected, &result);
    Widget *widget = modal_dialog_widget(dialog);
    if (modal_dialog_action_count(dialog) != 2 ||
        fabsf(content->base.bounds.w - 372.0f) > 0.01f ||
        fabsf(content->base.bounds.h - 60.0f) > 0.01f ||
        fabsf(content->base.bounds.x - 214.0f) > 0.01f) {
        fprintf(stderr, "Modal panel or content layout is incorrect\n");
        widget_destroy(widget);
        return 1;
    }

    modal_dialog_open(dialog);
    SDL_Event key = {0};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_ESCAPE;
    widget->handle_event(widget, &key);
    if (modal_dialog_is_open(dialog)) {
        fprintf(stderr, "Escape did not dismiss a dismissible dialog\n");
        widget_destroy(widget);
        return 1;
    }

    modal_dialog_set_dismissible(dialog, false);
    modal_dialog_open(dialog);
    widget->handle_event(widget, &key);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_DOWN, 20, 20);
    if (!modal_dialog_is_open(dialog) || result.count != 0) {
        fprintf(stderr, "Non-dismissible modal allowed dismissal or action\n");
        widget_destroy(widget);
        return 1;
    }

    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_DOWN, 220, 310);
    if (content->events != 1) {
        fprintf(stderr, "Modal content did not receive input inside its bounds\n");
        widget_destroy(widget);
        return 1;
    }

    modal_dialog_set_dismissible(dialog, true);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_DOWN, 20, 20);
    if (modal_dialog_is_open(dialog) || result.count != 0) {
        fprintf(stderr, "Backdrop click did not dismiss without activating content\n");
        widget_destroy(widget);
        return 1;
    }

    modal_dialog_open(dialog);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_DOWN, 526, 370);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_UP, 526, 370);
    if (result.count != 1 || result.index != 1 ||
        modal_dialog_is_open(dialog)) {
        fprintf(stderr, "Modal action did not report and close on selection\n");
        widget_destroy(widget);
        return 1;
    }

    modal_dialog_open(dialog);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_DOWN, 396, 370);
    send_mouse(dialog, SDL_EVENT_MOUSE_BUTTON_UP, 396, 370);
    if (result.count != 2 || result.index != 0 ||
        modal_dialog_is_open(dialog)) {
        fprintf(stderr, "Secondary dialog action failed\n");
        widget_destroy(widget);
        return 1;
    }

    widget_set_bounds(widget, (Rect){10.0f, 20.0f, 500.0f, 400.0f});
    if (fabsf(content->base.bounds.x - 74.0f) > 0.01f ||
        fabsf(content->base.bounds.y - 182.0f) > 0.01f) {
        fprintf(stderr, "Viewport resize did not recenter modal content\n");
        widget_destroy(widget);
        return 1;
    }
    widget_destroy(widget);

    ModalDialogStyle invalid = modal_dialog_style_default();
    invalid.width = NAN;
    if (modal_dialog_create((Rect){0.0f, 0.0f, 100.0f, 100.0f},
                            "Title", "Message", &invalid) != NULL ||
        modal_dialog_create((Rect){0.0f, 0.0f, -1.0f, 100.0f},
                            "Title", "Message", NULL) != NULL) {
        fprintf(stderr, "Modal dialog accepted invalid style or geometry\n");
        return 1;
    }
    puts("Modal dialog tests passed!");
    return 0;
}
