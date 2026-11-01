#include "tree_view.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t selection_count;
static TreeViewNode *last_selection;
static size_t userdata_destroy_count;

static void destroy_node_data(void *data)
{
    ++userdata_destroy_count;
    free(data);
}

static void on_selection(TreeView *tree, TreeViewNode *node, void *userdata)
{
    (void)tree;
    (void)userdata;
    ++selection_count;
    last_selection = node;
}

int main(void)
{
    Style widget_style = style_default();
    TreeView *tree = tree_view_create((Rect){10.0f, 20.0f, 220.0f, 90.0f},
                                      widget_style);
    if (!tree) {
        fprintf(stderr, "Failed to create tree view\n");
        return 1;
    }

    TreeViewStyle view_style = tree_view_style_default();
    view_style.row_height = 24.0f;
    if (!tree_view_set_style(tree, &view_style)) {
        fprintf(stderr, "Failed to set valid tree view style\n");
        widget_destroy((Widget *)tree);
        return 1;
    }
    TreeViewStyle invalid_style = view_style;
    invalid_style.row_height = 0.0f;
    if (tree_view_set_style(tree, &invalid_style)) {
        fprintf(stderr, "Accepted an invalid row height\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    TreeViewNode *files = tree_view_add_root(tree, "Files");
    TreeViewNode *documents = tree_view_add_child(files, "Documents");
    TreeViewNode *downloads = tree_view_add_child(files, "Downloads");
    TreeViewNode *favorites = tree_view_add_root(tree, "Favorites");
    if (!files || !documents || !downloads || !favorites ||
        tree_view_node_child_count(files) != 2 ||
        tree_view_node_child(files, 0) != documents ||
        tree_view_node_parent(documents) != files ||
        strcmp(tree_view_node_label(files), "Files") != 0) {
        fprintf(stderr, "Tree structure or node accessors failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    int *node_data = malloc(sizeof(*node_data));
    if (!node_data) {
        widget_destroy((Widget *)tree);
        return 1;
    }
    *node_data = 42;
    tree_view_set_node_data(downloads, node_data, destroy_node_data);
    tree_view_set_node_data(downloads, node_data, destroy_node_data);
    if (tree_view_node_data(downloads) != node_data ||
        userdata_destroy_count != 0) {
        fprintf(stderr, "Node data ownership failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    if (!tree_view_set_label(documents, "My Documents") ||
        strcmp(tree_view_node_label(documents), "My Documents") != 0 ||
        !tree_view_set_icon(documents, "▤") ||
        strcmp(tree_view_node_icon(documents), "▤") != 0) {
        fprintf(stderr, "Node customization failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    tree_view_set_expanded(files, true);
    if (!tree_view_node_is_expanded(files)) {
        fprintf(stderr, "Node expansion failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    tree_view_set_selection_handler(tree, on_selection, NULL);
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 80.0f;
    event.button.y = 20.0f + 24.0f + 12.0f;
    tree_view_event((Widget *)tree, &event);
    if (tree_view_selected(tree) != documents ||
        selection_count != 1 || last_selection != documents) {
        fprintf(stderr, "Mouse row selection or callback failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_DOWN;
    event.key.repeat = false;
    tree_view_event((Widget *)tree, &event);
    if (tree_view_selected(tree) != downloads ||
        selection_count != 2 || last_selection != downloads) {
        fprintf(stderr, "Keyboard navigation failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    event.key.key = SDLK_LEFT;
    tree_view_event((Widget *)tree, &event);
    if (tree_view_selected(tree) != files ||
        selection_count != 3 || last_selection != files) {
        fprintf(stderr, "Keyboard parent navigation failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }
    event.key.key = SDLK_LEFT;
    tree_view_event((Widget *)tree, &event);
    if (tree_view_node_is_expanded(files)) {
        fprintf(stderr, "Keyboard collapse failed\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    tree_view_set_selected(tree, documents);
    if (tree_view_selected(tree) != documents ||
        !tree_view_node_is_expanded(files)) {
        fprintf(stderr, "Programmatic selection did not reveal the node\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 20.0f;
    event.button.y = 32.0f;
    tree_view_event((Widget *)tree, &event);
    if (tree_view_node_is_expanded(files) ||
        tree_view_selected(tree) != files) {
        fprintf(stderr, "Disclosure click should collapse without deselecting\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    if (!tree_view_remove_node(tree, files) ||
        tree_view_selected(tree) != NULL ||
        last_selection != NULL || selection_count != 4 ||
        userdata_destroy_count != 1) {
        fprintf(stderr, "Removing a selected subtree did not clean up correctly\n");
        widget_destroy((Widget *)tree);
        return 1;
    }
    tree_view_clear(tree);
    if (!tree_view_add_root(tree, "After clear")) {
        fprintf(stderr, "Tree could not be reused after clearing\n");
        widget_destroy((Widget *)tree);
        return 1;
    }

    widget_destroy((Widget *)tree);
    if (userdata_destroy_count != 1) {
        fprintf(stderr, "Node data destructor did not run exactly once\n");
        return 1;
    }
    puts("Tree view tests passed!");
    return 0;
}
