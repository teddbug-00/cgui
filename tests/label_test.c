#include "label.h"
#include "text.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    Style style = style_default();
    style.text_size = 18.0f;
    style.text_style = CGUI_TEXT_BOLD;

    Rect bounds = { 10.0f, 20.0f, 200.0f, 30.0f };
    Label *lbl = label_create(bounds, style, "Initial Title");
    if (!lbl) {
        fprintf(stderr, "Failed to create label\n");
        return 1;
    }

    if (strcmp(label_get_text(lbl), "Initial Title") != 0) {
        fprintf(stderr, "Unexpected label text: %s\n", label_get_text(lbl));
        widget_destroy((Widget *)lbl);
        return 1;
    }

    if (lbl->align != CGUI_ALIGN_LEFT) {
        fprintf(stderr, "Default alignment should be LEFT\n");
        widget_destroy((Widget *)lbl);
        return 1;
    }

    label_set_alignment(lbl, CGUI_ALIGN_CENTER);
    if (lbl->align != CGUI_ALIGN_CENTER) {
        fprintf(stderr, "Alignment update failed\n");
        widget_destroy((Widget *)lbl);
        return 1;
    }

    label_set_text(lbl, "Updated Label Header");
    if (strcmp(label_get_text(lbl), "Updated Label Header") != 0) {
        fprintf(stderr, "Text update failed: %s\n", label_get_text(lbl));
        widget_destroy((Widget *)lbl);
        return 1;
    }

    /* Test Widget hierarchy embedding */
    Widget *root = widget_create();
    if (!root) {
        widget_destroy((Widget *)lbl);
        return 1;
    }

    if (!widget_add_child(root, (Widget *)lbl)) {
        fprintf(stderr, "Failed to attach label to widget root\n");
        widget_destroy(root);
        return 1;
    }

    /* Destroying root also cleans up child label and its allocated text cleanly */
    widget_destroy(root);

    printf("Label tests passed!\n");
    return 0;
}
