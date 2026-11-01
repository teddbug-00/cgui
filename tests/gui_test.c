#include "gui.h"

#include <stdio.h>

int main(void)
{
    Widget *root = widget_create();
    Widget *child = widget_create();
    Widget *grandchild = widget_create();
    Widget *other_root = widget_create();
    if (!root || !child || !grandchild || !other_root) {
        widget_destroy(root);
        widget_destroy(child);
        widget_destroy(grandchild);
        widget_destroy(other_root);
        return 1;
    }

    if (!widget_add_child(root, child) ||
        !widget_add_child(child, grandchild) ||
        widget_add_child(root, root) ||
        widget_add_child(child, root) ||
        widget_add_child(other_root, child) ||
        child->parent != root || grandchild->parent != child) {
        fprintf(stderr, "Widget hierarchy accepted an invalid attachment\n");
        widget_destroy(root);
        widget_destroy(other_root);
        return 1;
    }

    if (!widget_remove_child(root, child) ||
        child->parent != NULL ||
        root->child_count != 0 ||
        widget_remove_child(root, child) ||
        !widget_add_child(other_root, child)) {
        fprintf(stderr, "Widget detach or reparenting failed\n");
        widget_destroy(root);
        widget_destroy(other_root);
        return 1;
    }

    widget_destroy(child);
    if (other_root->child_count != 0) {
        fprintf(stderr, "Destroying a child left a dangling parent reference\n");
        widget_destroy(root);
        widget_destroy(other_root);
        return 1;
    }

    widget_destroy(root);
    widget_destroy(other_root);
    puts("Widget hierarchy tests passed!");
    return 0;
}
