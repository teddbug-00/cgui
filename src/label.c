/**
 * label.c – Standalone text label implementation
 */

#include "label.h"

#include <stdlib.h>
#include <string.h>

static VkDraw *g_draw = NULL;

void label_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

static void label_destroy(Widget *self)
{
    if (!self) {
        return;
    }
    Label *lbl = (Label *)self;
    if (lbl->owns_text) {
        free(lbl->text);
        lbl->text = NULL;
    }
}

void label_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) {
        return;
    }

    Label *lbl = (Label *)self;
    const Rect *b = &self->bounds;

    /* If label has a background or border defined, draw container box */
    if (self->style.bg[3] > 0 ||
        (self->style.border_width > 0.0f && self->style.border_color[3] > 0)) {
        vk_draw_style_box(g_draw, b->x, b->y, b->w, b->h, &self->style);
    }

    if (lbl->text && lbl->text[0] != '\0') {
        (void)vk_draw_text_aligned_styled(g_draw, lbl->text,
                                          b->x, b->y, b->w, b->h,
                                          self->style.text_size, self->style.text_style,
                                          self->style.text_color, lbl->align);
    }

    for (size_t i = 0; i < self->child_count; ++i) {
        Widget *child = self->children[i];
        if (child && child->draw) {
            child->draw(child, vk);
        }
    }
}

Label *label_create(Rect bounds, Style style, const char *text)
{
    Label *l = calloc(1, sizeof(Label));
    if (!l) {
        return NULL;
    }

    l->base.bounds       = bounds;
    l->base.style        = style;
    l->base.draw         = label_draw;
    l->base.handle_event = widget_event_noop;
    l->base.destroy      = label_destroy;
    l->align             = CGUI_ALIGN_LEFT;

    if (text) {
        l->text = strdup(text);
        l->owns_text = (l->text != NULL);
    }

    return l;
}

void label_set_text(Label *label, const char *text)
{
    if (!label) {
        return;
    }
    if (label->owns_text) {
        free(label->text);
        label->text = NULL;
        label->owns_text = false;
    }
    if (text) {
        label->text = strdup(text);
        label->owns_text = (label->text != NULL);
    }
}

void label_set_alignment(Label *label, CguiTextAlign align)
{
    if (!label) {
        return;
    }
    label->align = align;
}

const char *label_get_text(const Label *label)
{
    return label ? label->text : NULL;
}
