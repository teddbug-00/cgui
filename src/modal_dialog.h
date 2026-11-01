/**
 * modal_dialog.h - Centered modal dialog with a blocking backdrop.
 */

#ifndef CGUI_MODAL_DIALOG_H
#define CGUI_MODAL_DIALOG_H

#include "gui.h"
#include "vk_draw.h"

typedef struct ModalDialog ModalDialog;

typedef struct ModalDialogStyle {
    float width;
    float padding;
    float corner_radius;
    float title_size;
    float message_size;
    float button_height;
    float button_gap;
    uint8_t backdrop[4];
    uint8_t background[4];
    uint8_t border[4];
    uint8_t title[4];
    uint8_t message[4];
    uint8_t button_background[4];
    uint8_t button_hover[4];
    uint8_t button_text[4];
    uint8_t primary_background[4];
    uint8_t primary_hover[4];
    uint8_t primary_text[4];
} ModalDialogStyle;

typedef void (*ModalDialogActionFn)(ModalDialog *dialog, size_t index,
                                    void *userdata);

ModalDialogStyle modal_dialog_style_default(void);
ModalDialog *modal_dialog_create(Rect viewport, const char *title,
                                 const char *message,
                                 const ModalDialogStyle *style);
Widget *modal_dialog_widget(ModalDialog *dialog);
/** Attach one unparented content widget and assign its preferred height. */
bool modal_dialog_set_content(ModalDialog *dialog, Widget *content,
                              float preferred_height);
bool modal_dialog_add_action(ModalDialog *dialog, const char *label,
                             bool primary);
size_t modal_dialog_action_count(const ModalDialog *dialog);
void modal_dialog_set_action_handler(ModalDialog *dialog,
                                     ModalDialogActionFn callback,
                                     void *userdata);
void modal_dialog_set_dismissible(ModalDialog *dialog, bool dismissible);
void modal_dialog_open(ModalDialog *dialog);
void modal_dialog_close(ModalDialog *dialog);
bool modal_dialog_is_open(const ModalDialog *dialog);
void modal_dialog_set_draw_context(VkDraw *draw);

#endif
