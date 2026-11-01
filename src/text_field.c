/**
 * text_field.c – macOS-style single-line text input field widget
 */

#include "text_field.h"
#include "text.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static VkDraw *g_draw = NULL;

void text_field_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}

static bool point_in_rect(float px, float py, const Rect *r)
{
    return px >= r->x && px <= r->x + r->w &&
           py >= r->y && py <= r->y + r->h;
}

static size_t utf8_prev_codepoint(const char *s, size_t pos)
{
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

static size_t utf8_next_codepoint(const char *s, size_t pos, size_t len)
{
    if (pos >= len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0) == 0x80) {
        pos++;
    }
    return pos;
}

static void ensure_capacity(TextField *tf, size_t needed)
{
    if (needed + 1 <= tf->capacity) return;

    size_t new_cap = tf->capacity ? tf->capacity * 2 : 64;
    while (new_cap < needed + 1) {
        new_cap *= 2;
    }

    char *new_buf = realloc(tf->text, new_cap);
    if (!new_buf) return;

    tf->text = new_buf;
    tf->capacity = new_cap;
}

static void insert_text_at_cursor(TextField *tf, const char *str)
{
    if (!str || !str[0]) return;

    size_t add_len = strlen(str);
    ensure_capacity(tf, tf->length + add_len);
    if (!tf->text) return;

    memmove(&tf->text[tf->cursor_pos + add_len],
            &tf->text[tf->cursor_pos],
            tf->length - tf->cursor_pos + 1);

    memcpy(&tf->text[tf->cursor_pos], str, add_len);

    tf->cursor_pos += add_len;
    tf->length += add_len;

    tf->blink_timer = 0.0f;
    tf->cursor_visible = true;

    if (tf->on_change) {
        tf->on_change(tf, tf->text, tf->on_change_userdata);
    }
}

static void delete_before_cursor(TextField *tf)
{
    if (tf->cursor_pos == 0 || tf->length == 0) return;

    size_t prev_pos = utf8_prev_codepoint(tf->text, tf->cursor_pos);
    size_t byte_count = tf->cursor_pos - prev_pos;

    memmove(&tf->text[prev_pos],
            &tf->text[tf->cursor_pos],
            tf->length - tf->cursor_pos + 1);

    tf->length -= byte_count;
    tf->cursor_pos = prev_pos;

    tf->blink_timer = 0.0f;
    tf->cursor_visible = true;

    if (tf->on_change) {
        tf->on_change(tf, tf->text, tf->on_change_userdata);
    }
}

static void delete_after_cursor(TextField *tf)
{
    if (tf->cursor_pos >= tf->length) return;

    size_t next_pos = utf8_next_codepoint(tf->text, tf->cursor_pos, tf->length);
    size_t byte_count = next_pos - tf->cursor_pos;

    memmove(&tf->text[tf->cursor_pos],
            &tf->text[next_pos],
            tf->length - next_pos + 1);

    tf->length -= byte_count;

    tf->blink_timer = 0.0f;
    tf->cursor_visible = true;

    if (tf->on_change) {
        tf->on_change(tf, tf->text, tf->on_change_userdata);
    }
}

TextField *text_field_create(Rect bounds, Style style, const char *placeholder)
{
    TextField *tf = calloc(1, sizeof(*tf));
    if (!tf) return NULL;

    tf->base.bounds       = bounds;
    tf->base.style        = style;
    tf->base.draw         = text_field_draw;
    tf->base.handle_event = text_field_event;
    tf->base.destroy      = text_field_destroy_fn;

    tf->capacity = 64;
    tf->text = calloc(1, tf->capacity);
    if (!tf->text) {
        free(tf);
        return NULL;
    }
    tf->text[0] = '\0';
    tf->length = 0;
    tf->cursor_pos = 0;

    if (placeholder) {
        tf->placeholder = strdup(placeholder);
    }

    /* macOS typography and metrics */
    tf->corner_radius  = 8.0f;
    tf->font_size      = 14.0f;
    tf->padding_x      = 12.0f;
    tf->cursor_visible = true;

    /* macOS system color palette */
    memcpy(tf->bg_color,          (uint8_t[]){255, 255, 255, 255}, 4); /* Pure white surface */
    memcpy(tf->border_normal,     (uint8_t[]){215, 215, 220, 255}, 4); /* Soft border */
    memcpy(tf->border_focused,    (uint8_t[]){0,   122, 255, 255}, 4); /* macOS system blue */
    memcpy(tf->text_color,        (uint8_t[]){28,  28,  30,  255}, 4); /* Dark text */
    memcpy(tf->placeholder_color, (uint8_t[]){142, 142, 147, 255}, 4); /* Apple system gray */
    memcpy(tf->cursor_color,      (uint8_t[]){0,   122, 255, 255}, 4); /* Blue caret */

    return tf;
}

void text_field_set_text(TextField *tf, const char *text)
{
    if (!tf) return;

    if (!text) text = "";
    size_t new_len = strlen(text);
    ensure_capacity(tf, new_len);
    if (!tf->text) return;

    memcpy(tf->text, text, new_len + 1);
    tf->length = new_len;
    tf->cursor_pos = new_len;
    tf->scroll_x = 0.0f;
    tf->blink_timer = 0.0f;
    tf->cursor_visible = true;

    if (tf->on_change) {
        tf->on_change(tf, tf->text, tf->on_change_userdata);
    }
}

const char *text_field_get_text(const TextField *tf)
{
    if (!tf || !tf->text) return "";
    return tf->text;
}

void text_field_set_placeholder(TextField *tf, const char *placeholder)
{
    if (!tf) return;
    free(tf->placeholder);
    tf->placeholder = placeholder ? strdup(placeholder) : NULL;
}

void text_field_set_focused(TextField *tf, bool focused)
{
    if (!tf) return;
    if (tf->focused != focused) {
        tf->focused = focused;
        tf->blink_timer = 0.0f;
        tf->cursor_visible = focused;
    }
}

bool text_field_is_focused(const TextField *tf)
{
    return tf ? tf->focused : false;
}

void text_field_set_change_handler(TextField *tf, TextFieldChangeFn fn, void *userdata)
{
    if (!tf) return;
    tf->on_change = fn;
    tf->on_change_userdata = userdata;
}

void text_field_set_submit_handler(TextField *tf, TextFieldSubmitFn fn, void *userdata)
{
    if (!tf) return;
    tf->on_submit = fn;
    tf->on_submit_userdata = userdata;
}

void text_field_update(TextField *tf, float dt)
{
    if (!tf || !tf->focused) {
        if (tf) tf->cursor_visible = false;
        return;
    }

    tf->blink_timer += dt;
    /* 1.0s cycle: 0.53s ON, 0.47s OFF */
    tf->cursor_visible = (fmodf(tf->blink_timer, 1.0f) < 0.53f);
}

static float measure_substring_width(const TextField *tf, size_t byte_len)
{
    if (!g_draw || !tf || !tf->text || byte_len == 0) return 0.0f;

    char saved = tf->text[byte_len];
    ((char *)tf->text)[byte_len] = '\0';

    float w = 0.0f, h = 0.0f;
    vk_draw_measure_text(g_draw, tf->text, tf->font_size, CGUI_TEXT_REGULAR, &w, &h);

    ((char *)tf->text)[byte_len] = saved;
    return w;
}

/*
 * Draw only the portion of text visible within [scroll_x, scroll_x + avail_w].
 *
 * Strategy: walk codepoints to find start_byte (first codepoint that is not
 * fully scrolled off the left) and end_byte (last codepoint boundary that fits
 * inside the right edge). Render text[start_byte..end_byte] at the correctly
 * adjusted screen X so it lines up with the rest of the field.
 *
 * This avoids needing a Vulkan scissor rect — no GPU state changes required.
 */
static void draw_visible_text(const TextField *tf,
                               float field_inner_x,  /* b->x + padding_x */
                               float text_y,
                               float avail_w)
{
    if (!g_draw || !tf->text || tf->length == 0) return;

    float scroll    = tf->scroll_x;
    float right_lim = scroll + avail_w;

    /* --- Find start_byte: first codepoint NOT fully scrolled off left --- */
    size_t start_byte = 0;
    float  start_w    = 0.0f;   /* width of text[0..start_byte] */
    {
        size_t pos = 0;
        while (pos < tf->length) {
            size_t next = utf8_next_codepoint(tf->text, pos, tf->length);
            float w     = measure_substring_width(tf, next);
            if (w > scroll) break;   /* this codepoint starts crossing scroll_x */
            start_byte = next;
            start_w    = w;
            pos        = next;
        }
    }

    /* --- Find end_byte: include all codepoints that at least partly fit --- */
    size_t end_byte = start_byte;
    {
        size_t pos = start_byte;
        while (pos < tf->length) {
            float w_start_of_char = measure_substring_width(tf, pos);
            if (w_start_of_char >= right_lim) break;  /* fully past right edge */
            size_t next = utf8_next_codepoint(tf->text, pos, tf->length);
            end_byte = next;
            pos      = next;
        }
    }

    if (start_byte >= end_byte) return;

    /*
     * Render text[start_byte..end_byte].
     * The character at start_byte has its left edge at:
     *   screen_x = field_inner_x + (start_w - scroll)
     * which may be slightly left of field_inner_x when partially in view.
     */
    float render_x = field_inner_x + (start_w - scroll);

    char saved = tf->text[end_byte];
    ((char *)tf->text)[end_byte] = '\0';

    vk_draw_text_styled(g_draw, &tf->text[start_byte],
                        render_x, text_y,
                        tf->font_size, CGUI_TEXT_REGULAR, tf->text_color);

    ((char *)tf->text)[end_byte] = saved;
}

static void position_cursor_at_click(TextField *tf, float click_x)
{
    if (!tf || !tf->text || tf->length == 0 || !g_draw) {
        tf->cursor_pos = 0;
        return;
    }

    float text_start_x = tf->base.bounds.x + tf->padding_x - tf->scroll_x;
    float rel_x = click_x - text_start_x;

    if (rel_x <= 0.0f) {
        tf->cursor_pos = 0;
        return;
    }

    size_t best_pos = 0;
    float best_dist = fabsf(rel_x);
    size_t curr = 0;

    while (curr < tf->length) {
        curr = utf8_next_codepoint(tf->text, curr, tf->length);
        float w = measure_substring_width(tf, curr);
        float dist = fabsf(rel_x - w);
        if (dist < best_dist) {
            best_dist = dist;
            best_pos = curr;
        } else {
            /* Since width is monotonically increasing, break once distance grows */
            break;
        }
    }

    tf->cursor_pos = best_pos;
}

void text_field_event(Widget *self, const void *ev)
{
    if (!self || !ev) return;
    TextField *tf = (TextField *)self;
    const SDL_Event *e = (const SDL_Event *)ev;

    switch (e->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            tf->hovered = point_in_rect(e->motion.x, e->motion.y, &self->bounds);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            if (e->button.button == SDL_BUTTON_LEFT) {
                bool inside = point_in_rect(e->button.x, e->button.y, &self->bounds);
                if (inside) {
                    text_field_set_focused(tf, true);
                    position_cursor_at_click(tf, e->button.x);
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                } else {
                    text_field_set_focused(tf, false);
                }
            }
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
        {
            if (tf->focused && e->text.text) {
                insert_text_at_cursor(tf, e->text.text);
            }
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        {
            if (!tf->focused) break;

            SDL_Keycode key = e->key.key;
            SDL_Keymod mod = e->key.mod;

            bool ctrl = (mod & SDL_KMOD_CTRL) || (mod & SDL_KMOD_GUI);

            if (ctrl) {
                if (key == SDLK_V) {
                    char *clip = SDL_GetClipboardText();
                    if (clip) {
                        insert_text_at_cursor(tf, clip);
                        SDL_free(clip);
                    }
                    break;
                } else if (key == SDLK_C) {
                    if (tf->text) {
                        SDL_SetClipboardText(tf->text);
                    }
                    break;
                } else if (key == SDLK_A) {
                    /* Place cursor at end */
                    tf->cursor_pos = tf->length;
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                    break;
                }
            }

            switch (key)
            {
                case SDLK_BACKSPACE:
                    delete_before_cursor(tf);
                    break;

                case SDLK_DELETE:
                    delete_after_cursor(tf);
                    break;

                case SDLK_LEFT:
                    tf->cursor_pos = utf8_prev_codepoint(tf->text, tf->cursor_pos);
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                    break;

                case SDLK_RIGHT:
                    tf->cursor_pos = utf8_next_codepoint(tf->text, tf->cursor_pos, tf->length);
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                    break;

                case SDLK_HOME:
                    tf->cursor_pos = 0;
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                    break;

                case SDLK_END:
                    tf->cursor_pos = tf->length;
                    tf->blink_timer = 0.0f;
                    tf->cursor_visible = true;
                    break;

                case SDLK_RETURN:
                case SDLK_RETURN2:
                    if (tf->on_submit) {
                        tf->on_submit(tf, tf->text, tf->on_submit_userdata);
                    }
                    break;

                case SDLK_ESCAPE:
                    text_field_set_focused(tf, false);
                    break;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }
}

void text_field_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) return;

    TextField *tf = (TextField *)self;
    const Rect *b = &self->bounds;

    /* 1. macOS Background & Focus Ring Box */
    Style s = {0};
    s.corner_radius = tf->corner_radius;
    memcpy(s.bg, tf->bg_color, 4);

    if (tf->focused) {
        s.border_width = 1.5f;
        memcpy(s.border_color, tf->border_focused, 4);
        s.shadow_blur = 6.0f;
        s.shadow_offset_y = 0.0f;
        memcpy(s.shadow_color, (uint8_t[]){0, 122, 255, 75}, 4);
    } else {
        s.border_width = 1.0f;
        memcpy(s.border_color, tf->border_normal, 4);
        s.shadow_blur = 3.0f;
        s.shadow_offset_y = 1.0f;
        memcpy(s.shadow_color, (uint8_t[]){0, 0, 0, 15}, 4);
    }

    vk_draw_style_box(g_draw, b->x, b->y, b->w, b->h, &s);

    /* 2. Compute Cursor X and Horizontal Scroll Offset */
    float avail_w = b->w - 2.0f * tf->padding_x;
    if (avail_w < 10.0f) avail_w = 10.0f;

    float cursor_rel_x = 0.0f;
    if (tf->cursor_pos > 0 && tf->length > 0) {
        cursor_rel_x = measure_substring_width(tf, tf->cursor_pos);
    }

    if (cursor_rel_x - tf->scroll_x > avail_w - 4.0f) {
        tf->scroll_x = cursor_rel_x - avail_w + 4.0f;
    }
    if (cursor_rel_x - tf->scroll_x < 0.0f) {
        tf->scroll_x = cursor_rel_x;
    }
    if (tf->scroll_x < 0.0f) {
        tf->scroll_x = 0.0f;
    }

    float field_inner_x = b->x + tf->padding_x;
    float text_y        = b->y + (b->h - tf->font_size) * 0.5f - 1.0f;

    /* 3. Text or Placeholder — clipped to [field_inner_x, field_inner_x + avail_w] */
    if (tf->length > 0) {
        /* Render only the visible substring so text never bleeds outside the field */
        draw_visible_text(tf, field_inner_x, text_y, avail_w);
    } else if (tf->placeholder && tf->placeholder[0]) {
        vk_draw_text_styled(g_draw, tf->placeholder, field_inner_x, text_y,
                            tf->font_size, CGUI_TEXT_REGULAR, tf->placeholder_color);
    }

    /* 4. Caret / Cursor */
    if (tf->focused && tf->cursor_visible) {
        float cursor_screen_x = b->x + tf->padding_x + cursor_rel_x - tf->scroll_x;
        float min_x = b->x + tf->padding_x - 1.0f;
        float max_x = b->x + b->w - tf->padding_x + 1.0f;

        if (cursor_screen_x >= min_x && cursor_screen_x <= max_x) {
            float cursor_h = tf->font_size * 1.25f;
            float cursor_y = b->y + (b->h - cursor_h) * 0.5f;
            vk_draw_rounded_rect(g_draw, cursor_screen_x, cursor_y, 1.8f, cursor_h, 0.9f, 0.5f, tf->cursor_color);
        }
    }
}

void text_field_destroy_fn(Widget *self)
{
    if (!self) return;
    TextField *tf = (TextField *)self;
    free(tf->text);
    tf->text = NULL;
    free(tf->placeholder);
    tf->placeholder = NULL;
}
