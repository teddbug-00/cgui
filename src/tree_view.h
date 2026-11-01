/**
 * tree_view.h - Customizable hierarchical tree view widget.
 */

#ifndef TREE_VIEW_H
#define TREE_VIEW_H

#include "gui.h"
#include "vk_draw.h"

typedef struct TreeView TreeView;
typedef struct TreeViewNode TreeViewNode;

/** Row colors, metrics, and typography; start from tree_view_style_default(). */
typedef struct TreeViewStyle {
    float row_height;
    float indent_width;
    float disclosure_width;
    float horizontal_padding;
    float font_size;
    uint32_t text_style;
    uint8_t background[4];
    uint8_t border[4];
    uint8_t text[4];
    uint8_t secondary_text[4];
    uint8_t hover[4];
    uint8_t selection[4];
    uint8_t selected_text[4];
    uint8_t disclosure[4];
    float border_width;
    float corner_radius;
} TreeViewStyle;

typedef void (*TreeViewSelectionFn)(TreeView *tree, TreeViewNode *node,
                                    void *userdata);
typedef void (*TreeViewNodeDataDestroyFn)(void *data);
/** Draw custom row content after the selection/hover background and icon. */
typedef void (*TreeViewRowDrawFn)(TreeView *tree, TreeViewNode *node,
                                  VkDraw *draw, Rect row_bounds,
                                  bool selected, bool hovered,
                                  void *userdata);

/** Return the macOS-like light sidebar defaults. */
TreeViewStyle tree_view_style_default(void);

TreeView *tree_view_create(Rect bounds, Style style);
TreeViewNode *tree_view_add_root(TreeView *tree, const char *label);
TreeViewNode *tree_view_add_child(TreeViewNode *parent, const char *label);
bool tree_view_remove_node(TreeView *tree, TreeViewNode *node);
void tree_view_clear(TreeView *tree);
bool tree_view_set_label(TreeViewNode *node, const char *label);
/** Set a text-symbol icon; image icons take precedence when both are set. */
bool tree_view_set_icon(TreeViewNode *node, const char *icon);
/** Set a renderer-owned image icon loaded with vk_draw_load_icon(). */
bool tree_view_set_icon_image(TreeViewNode *node, const VkDrawIcon *icon);
const VkDrawIcon *tree_view_node_icon_image(const TreeViewNode *node);
/** The tree owns data and invokes destroy when replaced, removed, or destroyed. */
void tree_view_set_node_data(TreeViewNode *node, void *data,
                             TreeViewNodeDataDestroyFn destroy);
void *tree_view_node_data(const TreeViewNode *node);
const char *tree_view_node_label(const TreeViewNode *node);
const char *tree_view_node_icon(const TreeViewNode *node);
TreeViewNode *tree_view_node_parent(const TreeViewNode *node);
size_t tree_view_node_child_count(const TreeViewNode *node);
TreeViewNode *tree_view_node_child(const TreeViewNode *node, size_t index);
bool tree_view_node_is_expanded(const TreeViewNode *node);
void tree_view_set_expanded(TreeViewNode *node, bool expanded);

/** Returns false if the supplied metrics contain invalid values. */
bool tree_view_set_style(TreeView *tree, const TreeViewStyle *style);
void tree_view_set_selection_handler(TreeView *tree,
                                     TreeViewSelectionFn callback,
                                     void *userdata);
void tree_view_set_row_draw_callback(TreeView *tree, TreeViewRowDrawFn callback,
                                     void *userdata);
void tree_view_set_selected(TreeView *tree, TreeViewNode *node);
TreeViewNode *tree_view_selected(const TreeView *tree);

void tree_view_draw(Widget *self, struct VkContext *vk);
void tree_view_event(Widget *self, const void *event);
void tree_view_set_draw_context(VkDraw *draw);

#endif
