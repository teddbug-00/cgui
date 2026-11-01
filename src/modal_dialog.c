/**
 * modal_dialog.c - Centered modal dialog with a blocking backdrop.
 */

#include "modal_dialog.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "button.h"

#define MODAL_DIALOG_ANIMATION_NS UINT64_C(180000000)

typedef struct ModalDialogAction {
    char *label;
    Button *button;
} ModalDialogAction;

struct ModalDialog {
    Widget base;
    ModalDialogStyle dialog_style;
    char *title;
    char *message;
    Widget *content;
    float content_height;
    ModalDialogAction *actions;
    size_t action_count;
    size_t action_capacity;
    bool open;
    bool dismissible;
    Uint64 opened_at_ns;
    ModalDialogActionFn on_action;
    void *userdata;
};

static VkDraw *g_draw;

static char *copy_string(const char *text)
{
    if (!text) text = "";
    size_t length = strlen(text);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1);
    if (copy) memcpy(copy, text, length + 1);
    return copy;
}

ModalDialogStyle modal_dialog_style_default(void)
{
    return (ModalDialogStyle) {
        .width = 420.0f,
        .padding = 24.0f,
        .corner_radius = 12.0f,
        .title_size = 18.0f,
        .message_size = 14.0f,
        .button_height = 34.0f,
        .button_gap = 10.0f,
        .backdrop = {0, 0, 0, 102},
        .background = {255, 255, 255, 255},
        .border = {0, 0, 0, 24},
        .title = {35, 37, 41, 255},
        .message = {94, 98, 106, 255},
        .button_background = {235, 237, 241, 255},
        .button_hover = {221, 224, 230, 255},
        .button_text = {38, 40, 44, 255},
        .primary_background = {45, 105, 220, 255},
        .primary_hover = {35, 88, 194, 255},
        .primary_text = {255, 255, 255, 255},
    };
}

static float dialog_fixed_height(const ModalDialog *dialog)
{
    float height = dialog->dialog_style.padding * 2.0f +
                   dialog->dialog_style.title_size + 8.0f;
    if (dialog->message[0] != '\0')
        height += dialog->dialog_style.message_size + 12.0f;
    if (dialog->content)
        height += 16.0f + dialog->content_height;
    if (dialog->action_count > 0) {
        if (dialog->content || dialog->message[0] != '\0') height += 18.0f;
        height += dialog->dialog_style.button_height;
    }
    return height;
}

static float dialog_animation_progress(const ModalDialog *dialog)
{
    if (!dialog->open) return 1.0f;
    Uint64 elapsed = SDL_GetTicksNS() - dialog->opened_at_ns;
    float progress = (float)elapsed / (float)MODAL_DIALOG_ANIMATION_NS;
    if (progress >= 1.0f) return 1.0f;
    return progress * progress * (3.0f - 2.0f * progress);
}

static Rect dialog_panel_bounds(const ModalDialog *dialog)
{
    const Rect *viewport = &dialog->base.bounds;
    float width = fminf(dialog->dialog_style.width,
                        fmaxf(0.0f, viewport->w - 32.0f));
    float height = fminf(dialog_fixed_height(dialog),
                         fmaxf(0.0f, viewport->h - 32.0f));
    float scale = 0.98f + 0.02f * dialog_animation_progress(dialog);
    width *= scale;
    height *= scale;
    return (Rect) {
        .x = viewport->x + (viewport->w - width) * 0.5f,
        .y = viewport->y + (viewport->h - height) * 0.5f,
        .w = width,
        .h = height,
    };
}

static void modal_dialog_layout(Widget *self)
{
    ModalDialog *dialog = (ModalDialog *)self;
    Rect panel = dialog_panel_bounds(dialog);
    float x = panel.x + dialog->dialog_style.padding;
    float content_width = fmaxf(
        0.0f, panel.w - dialog->dialog_style.padding * 2.0f);
    if (dialog->content) {
        float content_y = panel.y + dialog->dialog_style.padding +
                          dialog->dialog_style.title_size + 8.0f;
        if (dialog->message[0] != '\0')
            content_y += dialog->dialog_style.message_size + 12.0f;
        float content_bottom = panel.y + panel.h -
                               dialog->dialog_style.padding;
        if (dialog->action_count > 0)
            content_bottom -= dialog->dialog_style.button_height + 18.0f;
        dialog->content->bounds = (Rect) {
            .x = x,
            .y = content_y,
            .w = content_width,
            .h = fmaxf(0.0f, fminf(dialog->content_height,
                                   content_bottom - content_y)),
        };
        widget_layout(dialog->content);
    }

    float button_width = dialog->action_count > 0
        ? fminf(120.0f, fmaxf(0.0f,
            (content_width - dialog->dialog_style.button_gap *
             (float)(dialog->action_count - 1)) /
             (float)dialog->action_count))
        : 0.0f;
    float buttons_width = button_width * (float)dialog->action_count +
        dialog->dialog_style.button_gap *
        (float)(dialog->action_count > 0 ? dialog->action_count - 1 : 0);
    float button_x = panel.x + panel.w - dialog->dialog_style.padding -
                     buttons_width;
    float button_y = panel.y + panel.h - dialog->dialog_style.padding -
                     dialog->dialog_style.button_height;
    for (size_t i = 0; i < dialog->action_count; ++i) {
        dialog->actions[i].button->base.bounds = (Rect) {
            .x = button_x + (float)i *
                 (button_width + dialog->dialog_style.button_gap),
            .y = button_y,
            .w = button_width,
            .h = dialog->dialog_style.button_height,
        };
    }
}

static void modal_dialog_destroy(Widget *self)
{
    ModalDialog *dialog = (ModalDialog *)self;
    free(dialog->title);
    free(dialog->message);
    for (size_t i = 0; i < dialog->action_count; ++i)
        free(dialog->actions[i].label);
    free(dialog->actions);
}

static void modal_dialog_draw(Widget *self, struct VkContext *context)
{
    (void)context;
    ModalDialog *dialog = (ModalDialog *)self;
    if (!dialog->open || !g_draw) return;

    float progress = dialog_animation_progress(dialog);
    uint8_t backdrop[4];
    memcpy(backdrop, dialog->dialog_style.backdrop, sizeof(backdrop));
    backdrop[3] = (uint8_t)((float)backdrop[3] * progress);
    vk_draw_rounded_rect(g_draw, self->bounds.x, self->bounds.y,
                         self->bounds.w, self->bounds.h, 0.0f, 0.5f,
                         backdrop);
    Rect panel = dialog_panel_bounds(dialog);
    Style box = style_default();
    box.corner_radius = dialog->dialog_style.corner_radius;
    box.border_width = 0.5f;
    box.shadow_offset_y = 8.0f;
    box.shadow_blur = 24.0f;
    box.shadow_color[3] = (uint8_t)(60.0f * progress);
    memcpy(box.bg, dialog->dialog_style.background, 4);
    memcpy(box.border_color, dialog->dialog_style.border, 4);
    box.bg[3] = (uint8_t)((float)box.bg[3] * progress);
    box.border_color[3] = (uint8_t)((float)box.border_color[3] * progress);
    vk_draw_style_box(g_draw, panel.x, panel.y, panel.w, panel.h, &box);

    float x = panel.x + dialog->dialog_style.padding;
    float y = panel.y + dialog->dialog_style.padding;
    uint8_t title_color[4];
    memcpy(title_color, dialog->dialog_style.title, sizeof(title_color));
    title_color[3] = (uint8_t)((float)title_color[3] * progress);
    (void)vk_draw_text(g_draw, dialog->title, x, y,
                       dialog->dialog_style.title_size,
                       title_color);
    y += dialog->dialog_style.title_size + 8.0f;
    if (dialog->message[0] != '\0') {
        uint8_t message_color[4];
        memcpy(message_color, dialog->dialog_style.message,
               sizeof(message_color));
        message_color[3] = (uint8_t)((float)message_color[3] * progress);
        (void)vk_draw_text(g_draw, dialog->message, x, y,
                           dialog->dialog_style.message_size,
                           message_color);
    }

    if (dialog->content && dialog->content->draw) {
        Rect bounds = dialog->content->bounds;
        if (vk_draw_clip_push(g_draw, bounds.x, bounds.y, bounds.w, bounds.h)) {
            dialog->content->draw(dialog->content, context);
            vk_draw_clip_pop(g_draw);
        }
    }
    for (size_t i = 0; i < dialog->action_count; ++i) {
        Widget *button = (Widget *)dialog->actions[i].button;
        if (button->draw) button->draw(button, context);
    }
}

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

static void modal_action_clicked(Button *button, void *userdata)
{
    ModalDialog *dialog = userdata;
    size_t index = 0;
    while (index < dialog->action_count &&
           dialog->actions[index].button != button) {
        ++index;
    }
    if (index == dialog->action_count) return;
    ModalDialogActionFn callback = dialog->on_action;
    void *callback_userdata = dialog->userdata;
    dialog->open = false;
    if (callback) callback(dialog, index, callback_userdata);
}

static void modal_dialog_event(Widget *self, const void *event)
{
    ModalDialog *dialog = (ModalDialog *)self;
    const SDL_Event *ev = event;
    if (!ev || !dialog->open) return;

    if (ev->type == SDL_EVENT_KEY_DOWN) {
        if (ev->key.key == SDLK_ESCAPE && dialog->dismissible) {
            dialog->open = false;
            return;
        }
        if (dialog->content && dialog->content->handle_event)
            dialog->content->handle_event(dialog->content, event);
        return;
    }

    float x;
    float y;
    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        x = (float)ev->motion.x;
        y = (float)ev->motion.y;
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        x = (float)ev->button.x;
        y = (float)ev->button.y;
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        x = ev->wheel.mouse_x;
        y = ev->wheel.mouse_y;
        if (dialog->content &&
            point_in_rect(x, y, &dialog->content->bounds) &&
            dialog->content->handle_event) {
            dialog->content->handle_event(dialog->content, event);
        }
        return;
    } else {
        return;
    }

    Rect panel = dialog_panel_bounds(dialog);
    if (!point_in_rect(x, y, &panel)) {
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            ev->button.button == SDL_BUTTON_LEFT && dialog->dismissible) {
            dialog->open = false;
        }
    } else if (dialog->content &&
               point_in_rect(x, y, &dialog->content->bounds) &&
               dialog->content->handle_event) {
        dialog->content->handle_event(dialog->content, event);
    }

    for (size_t i = 0; i < dialog->action_count; ++i) {
        Widget *button = (Widget *)dialog->actions[i].button;
        if (button->handle_event) button->handle_event(button, event);
    }
}

static bool valid_style(ModalDialogStyle style)
{
    return isfinite(style.width) && style.width > 0.0f &&
           isfinite(style.padding) && style.padding >= 0.0f &&
           isfinite(style.corner_radius) && style.corner_radius >= 0.0f &&
           isfinite(style.title_size) && style.title_size > 0.0f &&
           isfinite(style.message_size) && style.message_size > 0.0f &&
           isfinite(style.button_height) && style.button_height > 0.0f &&
           isfinite(style.button_gap) && style.button_gap >= 0.0f;
}

ModalDialog *modal_dialog_create(Rect viewport, const char *title,
                                 const char *message,
                                 const ModalDialogStyle *style)
{
    if (!isfinite(viewport.x) || !isfinite(viewport.y) ||
        !isfinite(viewport.w) || !isfinite(viewport.h) ||
        viewport.w < 0.0f || viewport.h < 0.0f) {
        return NULL;
    }
    ModalDialogStyle resolved = style ? *style : modal_dialog_style_default();
    if (!valid_style(resolved)) return NULL;

    ModalDialog *dialog = calloc(1, sizeof(*dialog));
    if (!dialog) return NULL;
    dialog->title = copy_string(title);
    dialog->message = copy_string(message);
    if (!dialog->title || !dialog->message) {
        free(dialog->title);
        free(dialog->message);
        free(dialog);
        return NULL;
    }
    dialog->base.bounds = viewport;
    dialog->base.style = style_default();
    dialog->base.draw = modal_dialog_draw;
    dialog->base.handle_event = modal_dialog_event;
    dialog->base.destroy = modal_dialog_destroy;
    dialog->base.layout = modal_dialog_layout;
    dialog->dialog_style = resolved;
    dialog->dismissible = true;
    modal_dialog_layout(&dialog->base);
    return dialog;
}

Widget *modal_dialog_widget(ModalDialog *dialog)
{
    return dialog ? &dialog->base : NULL;
}

bool modal_dialog_set_content(ModalDialog *dialog, Widget *content,
                              float preferred_height)
{
    if (!dialog || !content || dialog->content || content->parent ||
        !isfinite(preferred_height) || preferred_height < 0.0f) {
        return false;
    }
    if (!widget_add_child(&dialog->base, content)) return false;
    dialog->content = content;
    dialog->content_height = preferred_height;
    modal_dialog_layout(&dialog->base);
    return true;
}

bool modal_dialog_add_action(ModalDialog *dialog, const char *label,
                             bool primary)
{
    if (!dialog || !label || label[0] == '\0' ||
        dialog->action_count == SIZE_MAX ||
        dialog->action_count >= SIZE_MAX / sizeof(*dialog->actions)) {
        return false;
    }
    char *label_copy = copy_string(label);
    if (!label_copy) return false;
    if (dialog->action_count == dialog->action_capacity) {
        size_t capacity = dialog->action_capacity == 0
            ? 2 : dialog->action_capacity * 2;
        if (capacity < dialog->action_capacity ||
            capacity > SIZE_MAX / sizeof(*dialog->actions)) {
            free(label_copy);
            return false;
        }
        ModalDialogAction *actions = realloc(
            dialog->actions, capacity * sizeof(*dialog->actions));
        if (!actions) {
            free(label_copy);
            return false;
        }
        dialog->actions = actions;
        dialog->action_capacity = capacity;
    }

    static const uint8_t transparent[4] = {0, 0, 0, 0};
    Style style = style_default();
    style.corner_radius = 7.0f;
    style.border_width = 0.0f;
    style.shadow_blur = 0.0f;
    const uint8_t *background = primary
        ? dialog->dialog_style.primary_background
        : dialog->dialog_style.button_background;
    const uint8_t *text = primary
        ? dialog->dialog_style.primary_text : dialog->dialog_style.button_text;
    memcpy(style.bg, background, 4);
    memcpy(style.text_color, text, 4);
    style.text_size = 13.0f;
    Button *button = button_create((Rect){0}, style, label_copy);
    if (!button) {
        free(label_copy);
        return false;
    }
    button_set_interaction_colors(
        button,
        primary ? dialog->dialog_style.primary_hover
                : dialog->dialog_style.button_hover,
        transparent);
    button_set_click_handler(button, modal_action_clicked, dialog);
    if (!widget_add_child(&dialog->base, (Widget *)button)) {
        widget_destroy((Widget *)button);
        free(label_copy);
        return false;
    }
    dialog->actions[dialog->action_count++] = (ModalDialogAction) {
        .label = label_copy,
        .button = button,
    };
    modal_dialog_layout(&dialog->base);
    return true;
}

size_t modal_dialog_action_count(const ModalDialog *dialog)
{
    return dialog ? dialog->action_count : 0;
}

void modal_dialog_set_action_handler(ModalDialog *dialog,
                                     ModalDialogActionFn callback,
                                     void *userdata)
{
    if (!dialog) return;
    dialog->on_action = callback;
    dialog->userdata = userdata;
}

void modal_dialog_set_dismissible(ModalDialog *dialog, bool dismissible)
{
    if (dialog) dialog->dismissible = dismissible;
}

void modal_dialog_open(ModalDialog *dialog)
{
    if (dialog && !dialog->open) {
        dialog->opened_at_ns = SDL_GetTicksNS();
        dialog->open = true;
    }
}

void modal_dialog_close(ModalDialog *dialog)
{
    if (dialog) dialog->open = false;
}

bool modal_dialog_is_open(const ModalDialog *dialog)
{
    return dialog && dialog->open;
}

void modal_dialog_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
