/**
 * label.h – Standalone text label widget with alignment support
 */

#ifndef LABEL_H
#define LABEL_H

#include "gui.h"
#include "vk_draw.h"

typedef struct Label Label;

struct Label {
    Widget base;            /* MUST be first */
    char *text;             /* Owned string */
    CguiTextAlign align;
    bool owns_text;
};

/** Create a label widget with the given bounds, style, and text. */
Label *label_create(Rect bounds, Style style, const char *text);

/** Update label text (makes an internal copy). */
void label_set_text(Label *label, const char *text);

/** Set text alignment (CGUI_ALIGN_LEFT, CGUI_ALIGN_CENTER, CGUI_ALIGN_RIGHT). */
void label_set_alignment(Label *label, CguiTextAlign align);

/** Get current label text. */
const char *label_get_text(const Label *label);

/** Draw callback for label widget. */
void label_draw(Widget *self, struct VkContext *vk);

/** Set the active draw context for all labels. */
void label_set_draw_context(VkDraw *draw);

#endif /* LABEL_H */
