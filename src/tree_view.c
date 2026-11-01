/**
 * tree_view.c - Hierarchical tree view with customizable row rendering.
 */

#include "tree_view.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

static VkDraw *g_draw;
static void clamp_scroll(TreeView *tree);

struct TreeView {
    Widget base;
    TreeViewNode **roots;
    size_t root_count;
    size_t root_capacity;
    TreeViewNode *selected;
    TreeViewNode *hovered;
    float scroll_offset;
    bool focused;
    TreeViewStyle view_style;
    TreeViewSelectionFn on_selection;
    void *selection_userdata;
    TreeViewRowDrawFn draw_row;
    void *draw_row_userdata;
};

struct TreeViewNode {
    TreeView *tree;
    TreeViewNode *parent;
    TreeViewNode **children;
    size_t child_count;
    size_t child_capacity;
    char *label;
    char *icon;
    const VkDrawIcon *icon_image;
    void *userdata;
    TreeViewNodeDataDestroyFn userdata_destroy;
    bool expanded;
};

static bool point_in_rect(float x, float y, const Rect *rect)
{
    return x >= rect->x && y >= rect->y &&
           x < rect->x + rect->w && y < rect->y + rect->h;
}

TreeViewStyle tree_view_style_default(void)
{
    return (TreeViewStyle) {
        .row_height = 30.0f,
        .indent_width = 16.0f,
        .disclosure_width = 18.0f,
        .horizontal_padding = 10.0f,
        .font_size = 14.0f,
        .text_style = CGUI_TEXT_REGULAR,
        .background = { 255, 255, 255, 255 },
        .border = { 220, 220, 225, 255 },
        .text = { 35, 35, 38, 255 },
        .secondary_text = { 112, 112, 118, 255 },
        .hover = { 242, 242, 245, 255 },
        .selection = { 0, 122, 255, 255 },
        .selected_text = { 255, 255, 255, 255 },
        .disclosure = { 112, 112, 118, 255 },
        .border_width = 1.0f,
        .corner_radius = 8.0f,
    };
}

static void tree_node_destroy(TreeViewNode *node)
{
    if (!node) return;
    for (size_t i = 0; i < node->child_count; ++i) {
        tree_node_destroy(node->children[i]);
    }
    if (node->userdata_destroy) node->userdata_destroy(node->userdata);
    free(node->children);
    free(node->label);
    free(node->icon);
    free(node);
}

static void tree_view_destroy(Widget *self)
{
    TreeView *tree = (TreeView *)self;
    for (size_t i = 0; i < tree->root_count; ++i) {
        tree_node_destroy(tree->roots[i]);
    }
    free(tree->roots);
}

TreeView *tree_view_create(Rect bounds, Style style)
{
    TreeView *tree = calloc(1, sizeof(*tree));
    if (!tree) return NULL;

    tree->base.bounds = bounds;
    tree->base.style = style;
    tree->base.draw = tree_view_draw;
    tree->base.handle_event = tree_view_event;
    tree->base.destroy = tree_view_destroy;
    tree->view_style = tree_view_style_default();
    return tree;
}

static bool append_node(TreeViewNode ***items, size_t *count, size_t *capacity,
                        TreeViewNode *node)
{
    if (*count == *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 4;
        if (new_capacity < *capacity ||
            new_capacity > SIZE_MAX / sizeof(**items)) {
            return false;
        }
        TreeViewNode **new_items = realloc(*items,
                                           new_capacity * sizeof(**items));
        if (!new_items) return false;
        *items = new_items;
        *capacity = new_capacity;
    }
    (*items)[(*count)++] = node;
    return true;
}

static TreeViewNode *create_node(TreeView *tree, TreeViewNode *parent,
                                 const char *label)
{
    if (!tree || !label) return NULL;
    TreeViewNode *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->label = strdup(label);
    if (!node->label) {
        free(node);
        return NULL;
    }
    node->tree = tree;
    node->parent = parent;
    return node;
}

TreeViewNode *tree_view_add_root(TreeView *tree, const char *label)
{
    if (!tree) return NULL;
    TreeViewNode *node = create_node(tree, NULL, label);
    if (!node) return NULL;
    if (!append_node(&tree->roots, &tree->root_count, &tree->root_capacity,
                     node)) {
        tree_node_destroy(node);
        return NULL;
    }
    return node;
}

TreeViewNode *tree_view_add_child(TreeViewNode *parent, const char *label)
{
    if (!parent || !parent->tree) return NULL;
    TreeViewNode *node = create_node(parent->tree, parent, label);
    if (!node) return NULL;
    if (!append_node(&parent->children, &parent->child_count,
                     &parent->child_capacity, node)) {
        tree_node_destroy(node);
        return NULL;
    }
    return node;
}

static bool node_contains(const TreeViewNode *root, const TreeViewNode *target)
{
    if (root == target) return true;
    for (size_t i = 0; i < root->child_count; ++i) {
        if (node_contains(root->children[i], target)) return true;
    }
    return false;
}

bool tree_view_remove_node(TreeView *tree, TreeViewNode *node)
{
    if (!tree || !node || node->tree != tree) return false;

    TreeViewNode ***siblings = node->parent
        ? &node->parent->children : &tree->roots;
    size_t *count = node->parent
        ? &node->parent->child_count : &tree->root_count;
    size_t index = 0;
    while (index < *count && (*siblings)[index] != node) ++index;
    if (index == *count) return false;

    bool selection_removed = tree->selected &&
                             node_contains(node, tree->selected);
    if (selection_removed) tree->selected = NULL;
    if (tree->hovered && node_contains(node, tree->hovered)) {
        tree->hovered = NULL;
    }
    memmove(&(*siblings)[index], &(*siblings)[index + 1],
            (*count - index - 1) * sizeof(**siblings));
    --(*count);
    if (node->parent && node->parent->child_count == 0) {
        node->parent->expanded = false;
    }
    tree_node_destroy(node);
    clamp_scroll(tree);
    if (selection_removed && tree->on_selection) {
        tree->on_selection(tree, NULL, tree->selection_userdata);
    }
    return true;
}

void tree_view_clear(TreeView *tree)
{
    if (!tree) return;
    bool had_selection = tree->selected != NULL;
    tree->selected = NULL;
    tree->hovered = NULL;
    for (size_t i = 0; i < tree->root_count; ++i) {
        tree_node_destroy(tree->roots[i]);
    }
    tree->root_count = 0;
    tree->scroll_offset = 0.0f;
    if (had_selection && tree->on_selection) {
        tree->on_selection(tree, NULL, tree->selection_userdata);
    }
}

static bool replace_string(char **destination, const char *value)
{
    char *replacement = value ? strdup(value) : NULL;
    if (value && !replacement) return false;
    free(*destination);
    *destination = replacement;
    return true;
}

bool tree_view_set_label(TreeViewNode *node, const char *label)
{
    return node && label && replace_string(&node->label, label);
}

bool tree_view_set_icon(TreeViewNode *node, const char *icon)
{
    return node && replace_string(&node->icon, icon);
}

bool tree_view_set_icon_image(TreeViewNode *node, const VkDrawIcon *icon)
{
    if (!node) return false;
    node->icon_image = icon;
    return true;
}

const VkDrawIcon *tree_view_node_icon_image(const TreeViewNode *node)
{
    return node ? node->icon_image : NULL;
}

void tree_view_set_node_data(TreeViewNode *node, void *data,
                             TreeViewNodeDataDestroyFn destroy)
{
    if (!node) return;
    if (node->userdata == data) {
        node->userdata_destroy = destroy;
        return;
    }
    if (node->userdata_destroy) node->userdata_destroy(node->userdata);
    node->userdata = data;
    node->userdata_destroy = destroy;
}

void *tree_view_node_data(const TreeViewNode *node)
{
    return node ? node->userdata : NULL;
}

const char *tree_view_node_label(const TreeViewNode *node)
{
    return node ? node->label : NULL;
}

const char *tree_view_node_icon(const TreeViewNode *node)
{
    return node ? node->icon : NULL;
}

TreeViewNode *tree_view_node_parent(const TreeViewNode *node)
{
    return node ? node->parent : NULL;
}

size_t tree_view_node_child_count(const TreeViewNode *node)
{
    return node ? node->child_count : 0;
}

TreeViewNode *tree_view_node_child(const TreeViewNode *node, size_t index)
{
    return node && index < node->child_count ? node->children[index] : NULL;
}

bool tree_view_node_is_expanded(const TreeViewNode *node)
{
    return node && node->expanded;
}

static size_t visible_node_count(TreeViewNode *const nodes[], size_t count)
{
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (total < SIZE_MAX) ++total;
        if (nodes[i]->expanded) {
            size_t children = visible_node_count(nodes[i]->children,
                                                 nodes[i]->child_count);
            total = children > SIZE_MAX - total ? SIZE_MAX : total + children;
        }
    }
    return total;
}

static TreeViewNode *visible_node_at(TreeViewNode *const nodes[], size_t count,
                                     size_t index)
{
    for (size_t i = 0; i < count; ++i) {
        if (index == 0) return nodes[i];
        --index;
        if (nodes[i]->expanded) {
            size_t child_count = visible_node_count(nodes[i]->children,
                                                    nodes[i]->child_count);
            if (index < child_count) {
                return visible_node_at(nodes[i]->children,
                                       nodes[i]->child_count, index);
            }
            index -= child_count;
        }
    }
    return NULL;
}

static size_t visible_index_of(TreeView *tree, TreeViewNode *target)
{
    size_t count = visible_node_count(tree->roots, tree->root_count);
    for (size_t i = 0; i < count; ++i) {
        if (visible_node_at(tree->roots, tree->root_count, i) == target) {
            return i;
        }
    }
    return SIZE_MAX;
}

void tree_view_set_expanded(TreeViewNode *node, bool expanded)
{
    if (!node || !node->tree) return;
    node->expanded = expanded && node->child_count > 0;
    if (!node->expanded && node->tree->selected) {
        for (TreeViewNode *parent = node->tree->selected->parent;
             parent; parent = parent->parent) {
            if (parent == node) {
                node->tree->selected = node;
                break;
            }
        }
    }
}

bool tree_view_set_style(TreeView *tree, const TreeViewStyle *style)
{
    if (!tree || !style ||
        !isfinite(style->row_height) || style->row_height <= 0.0f ||
        !isfinite(style->indent_width) || style->indent_width < 0.0f ||
        !isfinite(style->disclosure_width) || style->disclosure_width < 0.0f ||
        !isfinite(style->horizontal_padding) ||
        style->horizontal_padding < 0.0f ||
        !isfinite(style->font_size) || style->font_size <= 0.0f ||
        !isfinite(style->border_width) || style->border_width < 0.0f ||
        !isfinite(style->corner_radius) || style->corner_radius < 0.0f) {
        return false;
    }
    tree->view_style = *style;
    clamp_scroll(tree);
    return true;
}

void tree_view_set_selection_handler(TreeView *tree,
                                     TreeViewSelectionFn callback,
                                     void *userdata)
{
    if (!tree) return;
    tree->on_selection = callback;
    tree->selection_userdata = userdata;
}

void tree_view_set_row_draw_callback(TreeView *tree, TreeViewRowDrawFn callback,
                                     void *userdata)
{
    if (!tree) return;
    tree->draw_row = callback;
    tree->draw_row_userdata = userdata;
}

void tree_view_set_selected(TreeView *tree, TreeViewNode *node)
{
    if (!tree) return;
    if (node && node->tree != tree) return;
    for (TreeViewNode *parent = node ? node->parent : NULL;
         parent; parent = parent->parent) {
        parent->expanded = true;
    }
    tree->selected = node;
    if (node) {
        size_t index = visible_index_of(tree, node);
        if (index != SIZE_MAX) {
            float row_top = (float)index * tree->view_style.row_height;
            float row_bottom = row_top + tree->view_style.row_height;
            float view_height = tree->base.bounds.h;
            if (row_top < tree->scroll_offset) {
                tree->scroll_offset = row_top;
            } else if (row_bottom > tree->scroll_offset + view_height) {
                tree->scroll_offset = row_bottom - view_height;
            }
        }
    }
}

TreeViewNode *tree_view_selected(const TreeView *tree)
{
    return tree ? tree->selected : NULL;
}

static void draw_node(TreeView *tree, TreeViewNode *node, size_t depth,
                      size_t *row_index, const Rect *bounds)
{
    const TreeViewStyle *style = &tree->view_style;
    float row_height = style->row_height;
    float row_y = bounds->y + (float)(*row_index) * row_height -
                  tree->scroll_offset;
    ++(*row_index);

    bool visible = row_y >= bounds->y &&
                   row_y + row_height <= bounds->y + bounds->h;
    if (visible) {
        Rect row = { bounds->x, row_y, bounds->w, row_height };
        bool selected = node == tree->selected;
        bool hovered = tree->hovered == node;
        if (selected) {
            vk_draw_rounded_rect(g_draw, row.x, row.y, row.w, row.h,
                                 style->corner_radius, 0.5f, style->selection);
        } else if (hovered) {
            vk_draw_rounded_rect(g_draw, row.x, row.y, row.w, row.h,
                                 style->corner_radius, 0.5f, style->hover);
        }

        float disclosure_x = row.x + style->horizontal_padding +
                             (float)depth * style->indent_width;
        if (node->child_count > 0) {
            const char *disclosure = node->expanded ? "▾" : "▸";
            (void)vk_draw_text(g_draw, disclosure, disclosure_x,
                               row.y + (row.h - style->font_size) * 0.5f,
                               style->font_size, style->disclosure);
        }
        float text_x = disclosure_x + style->disclosure_width;
        if (node->icon_image) {
            uint8_t tint[4];
            memcpy(tint, selected ? style->selected_text
                                  : style->secondary_text, sizeof(tint));
            float icon_size = style->font_size + 2.0f;
            vk_draw_icon(g_draw, node->icon_image, text_x,
                         row.y + (row.h - icon_size) * 0.5f,
                         icon_size, icon_size, tint);
            text_x += icon_size + 5.0f;
        } else if (node->icon && node->icon[0]) {
            (void)vk_draw_text(g_draw, node->icon, text_x,
                               row.y + (row.h - style->font_size) * 0.5f,
                               style->font_size,
                               selected ? style->selected_text : style->secondary_text);
            text_x += style->font_size + 5.0f;
        }
        if (tree->draw_row) {
            tree->draw_row(tree, node, g_draw, row, selected, hovered,
                           tree->draw_row_userdata);
        } else {
            (void)vk_draw_text(g_draw, node->label, text_x,
                               row.y + (row.h - style->font_size) * 0.5f,
                               style->font_size,
                               selected ? style->selected_text : style->text);
        }
    }

    if (node->expanded) {
        for (size_t i = 0; i < node->child_count; ++i) {
            draw_node(tree, node->children[i], depth + 1, row_index, bounds);
        }
    }
}

void tree_view_draw(Widget *self, struct VkContext *vk)
{
    (void)vk;
    if (!self || !g_draw) return;
    TreeView *tree = (TreeView *)self;
    const Rect *bounds = &self->bounds;
    const TreeViewStyle *style = &tree->view_style;
    if (bounds->w <= 0.0f || bounds->h <= 0.0f || style->row_height <= 0.0f) {
        return;
    }

    Style box = style_default();
    memcpy(box.bg, style->background, 4);
    memcpy(box.border_color, style->border, 4);
    box.border_width = style->border_width;
    box.corner_radius = style->corner_radius;
    box.shadow_color[3] = 0;
    vk_draw_style_box(g_draw, bounds->x, bounds->y, bounds->w, bounds->h, &box);

    size_t row_index = 0;
    for (size_t i = 0; i < tree->root_count; ++i) {
        draw_node(tree, tree->roots[i], 0, &row_index, bounds);
    }
}

static void clamp_scroll(TreeView *tree)
{
    float row_height = tree->view_style.row_height;
    if (row_height <= 0.0f) {
        tree->scroll_offset = 0.0f;
        return;
    }
    size_t count = visible_node_count(tree->roots, tree->root_count);
    float content_height = (float)count * row_height;
    float max_scroll = content_height > tree->base.bounds.h
        ? content_height - tree->base.bounds.h : 0.0f;
    if (tree->scroll_offset < 0.0f) tree->scroll_offset = 0.0f;
    if (tree->scroll_offset > max_scroll) tree->scroll_offset = max_scroll;
}

static void select_visible_index(TreeView *tree, size_t index)
{
    TreeViewNode *node = visible_node_at(tree->roots, tree->root_count, index);
    if (node && node != tree->selected) {
        tree->selected = node;
        if (tree->on_selection) {
            tree->on_selection(tree, node, tree->selection_userdata);
        }
    }
}

static void tree_view_key_event(TreeView *tree, SDL_Keycode key)
{
    size_t count = visible_node_count(tree->roots, tree->root_count);
    size_t selected_index = tree->selected
        ? visible_index_of(tree, tree->selected) : SIZE_MAX;

    switch (key) {
    case SDLK_UP:
        if (count > 0) {
            select_visible_index(tree, selected_index == SIZE_MAX ||
                                 selected_index == 0
                                     ? 0 : selected_index - 1);
        }
        break;
    case SDLK_DOWN:
        if (count > 0) {
            select_visible_index(tree, selected_index == SIZE_MAX
                                     ? 0
                                     : selected_index + 1 < count
                                           ? selected_index + 1 : count - 1);
        }
        break;
    case SDLK_LEFT:
        if (tree->selected && tree->selected->expanded) {
            tree_view_set_expanded(tree->selected, false);
        } else if (tree->selected && tree->selected->parent) {
            select_visible_index(tree, visible_index_of(
                tree, tree->selected->parent));
        }
        break;
    case SDLK_RIGHT:
        if (tree->selected && tree->selected->child_count > 0) {
            if (!tree->selected->expanded) {
                tree_view_set_expanded(tree->selected, true);
            } else {
                select_visible_index(tree, visible_index_of(tree,
                                                            tree->selected) + 1);
            }
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (tree->selected && tree->selected->child_count > 0) {
            tree_view_set_expanded(tree->selected,
                                   !tree->selected->expanded);
        }
        break;
    default:
        break;
    }
    if (tree->selected) tree_view_set_selected(tree, tree->selected);
    clamp_scroll(tree);
}

void tree_view_event(Widget *self, const void *event)
{
    if (!self || !event) return;
    TreeView *tree = (TreeView *)self;
    const SDL_Event *ev = event;
    const Rect *bounds = &self->bounds;

    if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        float x = (float)ev->motion.x;
        float y = (float)ev->motion.y;
        tree->hovered = NULL;
        if (point_in_rect(x, y, bounds) && tree->view_style.row_height > 0.0f) {
            size_t index = (size_t)((y - bounds->y + tree->scroll_offset) /
                                    tree->view_style.row_height);
            tree->hovered = visible_node_at(tree->roots, tree->root_count, index);
        }
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               ev->button.button == SDL_BUTTON_LEFT) {
        float x = (float)ev->button.x;
        float y = (float)ev->button.y;
        tree->focused = point_in_rect(x, y, bounds);
        if (tree->focused && tree->view_style.row_height > 0.0f) {
            size_t index = (size_t)((y - bounds->y + tree->scroll_offset) /
                                    tree->view_style.row_height);
            TreeViewNode *node = visible_node_at(tree->roots,
                                                  tree->root_count, index);
            if (node) {
                size_t depth = 0;
                for (TreeViewNode *parent = node->parent; parent;
                     parent = parent->parent) ++depth;
                float disclosure_x = bounds->x +
                    tree->view_style.horizontal_padding +
                    (float)depth * tree->view_style.indent_width;
                bool disclosure_hit = node->child_count > 0 &&
                    x < disclosure_x + tree->view_style.disclosure_width;
                if (disclosure_hit) {
                    tree_view_set_expanded(node, !node->expanded);
                } else if (node != tree->selected) {
                    tree->selected = node;
                    if (tree->on_selection) {
                        tree->on_selection(tree, node,
                                           tree->selection_userdata);
                    }
                }
            }
        }
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL &&
               point_in_rect((float)ev->wheel.mouse_x,
                             (float)ev->wheel.mouse_y, bounds)) {
        tree->scroll_offset -= (float)ev->wheel.y *
                               tree->view_style.row_height * 3.0f;
        clamp_scroll(tree);
    } else if (ev->type == SDL_EVENT_KEY_DOWN && tree->focused &&
               !ev->key.repeat) {
        tree_view_key_event(tree, ev->key.key);
    }
}

void tree_view_set_draw_context(VkDraw *draw)
{
    g_draw = draw;
}
