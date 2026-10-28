#include "../../backends/linux/gtk/bridge.h"
#include "../gtk/test_hooks.h"

/* Pages live in a GtkStack inside the vertical GtkScrolledWindow's viewport. */
static GtkWidget *stack_of(GtkWidget *root) {
    GtkWidget *scroll = gtk_widget_get_last_child(root);
    g_assert_true(GTK_IS_SCROLLED_WINDOW(scroll));
    GtkWidget *viewport = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scroll));
    return gtk_viewport_get_child(GTK_VIEWPORT(viewport));
}

/* Run the main loop until GTK has allocated and painted pending layout. */
static void settle(void) {
    gint64 deadline = g_get_monotonic_time() + 300000;
    while (g_get_monotonic_time() < deadline) {
        while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
        g_usleep(5000);
    }
}

/* True when the widget is entirely inside the scrolled viewport. */
static gboolean in_viewport(GtkWidget *widget, GtkWidget *scroll) {
    graphene_rect_t rect;
    if (!gtk_widget_compute_bounds(widget, scroll, &rect)) return FALSE;
    return rect.origin.y >= 0 && rect.origin.y + rect.size.height <= gtk_widget_get_height(scroll) &&
        rect.origin.x >= 0 && rect.origin.x + rect.size.width <= gtk_widget_get_width(scroll);
}

static gboolean focus_within(GtkWindow *window, GtkWidget *widget) {
    GtkWidget *focus = gtk_window_get_focus(window);
    return focus && (focus == widget || gtk_widget_is_ancestor(focus, widget));
}

static gpointer wrong_thread(gpointer session) {
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 0), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 0), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "add", 0), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "name", "wrong"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "wrong"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "wrong", "Wrong", 1), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Wrong"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 2);
    return NULL;
}

int main(void) {
    g_setenv("GTK_A11Y", "test", TRUE);
    void *session = NULL;
    g_assert_cmpint(tarn_ui_gtk_create("Components", "Navigation", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "main"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "details"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "home-value", "0"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "add", "Add", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "details", "back", "Back", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "add", "bad-parent", "0"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "add", "Duplicate"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "bad", "Bad", 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "bad", "Bad", 1025), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "bad", "\xff"), ==, 1);
    GtkWidget *root = gtk_window_get_child(test_window(session));
    GtkWidget *stack = stack_of(root);
    GtkWidget *home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    GtkWidget *details = gtk_stack_get_child_by_name(GTK_STACK(stack), "details");
    GtkWidget *label = gtk_widget_get_first_child(home);
    GtkWidget *button = gtk_widget_get_last_child(home);
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "add", 2), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "missing", 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "add", 0), ==, 0);
    g_assert_false(gtk_widget_is_sensitive(button));
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "add", 1), ==, 0);
    g_assert_true(gtk_widget_is_sensitive(button));
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "late", "Late"), ==, 1);
    GThread *thread = g_thread_new("wrong-thread", wrong_thread, session);
    g_thread_join(thread);
    for (int i = 0; i < 20; ++i) {
        g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Updated"), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_show_page(session, "details"), ==, 0);
        g_assert_true(gtk_stack_get_visible_child(GTK_STACK(stack)) == details);
        g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
        g_assert_true(gtk_stack_get_visible_child(GTK_STACK(stack)) == home);
        g_assert_true(gtk_widget_get_first_child(home) == label);
        g_assert_true(gtk_widget_get_last_child(home) == button);
    }
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "missing"), ==, 1);
    g_assert_true(gtk_stack_get_visible_child(GTK_STACK(stack)) == home);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "\xff"), ==, 1);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, "Updated");
    g_signal_emit_by_name(button, "clicked");
    int32_t event = tarn_ui_gtk_next(session);
    if (event == 1) event = tarn_ui_gtk_next(session);
    g_assert_cmpint(event, ==, 1001);
    g_assert_cmpint(tarn_ui_gtk_complete_event(session, 0), ==, 0);
    GtkWidget *back = gtk_widget_get_last_child(details);
    g_signal_emit_by_name(button, "clicked");
    g_signal_emit_by_name(back, "clicked");
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, 1001);
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, 1002);
    for (int i = 0; i < 65; ++i) g_signal_emit_by_name(button, "clicked");
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, -8);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Blocked"), ==, 8);
    g_assert_cmpint(tarn_ui_gtk_request_close(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "details"), ==, 6);
    g_assert_cmpint(tarn_ui_gtk_set_enabled(session, "add", 1), ==, 6);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Closed"), ==, 6);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    TestStats stats = test_stats();
    g_print("components=%u/%u handlers=%u/%u; identity retained across 20 round trips\n",
            stats.components, stats.components_finalized, stats.connected, stats.disconnected);

    /* Nested layout and real accessibility properties use GTK's test AT context. */
    g_assert_cmpint(tarn_ui_gtk_create("Presentation", "Presentation", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "row", 1, 12), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "invalid", 2, 12), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "invalid", 0, 65), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_text(session, "row", "title", "Title", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "row", "action", "Action", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_set_primary(session, "action", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_set_primary(session, "title", 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_accessible(session, "action", "Accessible action", "Description"), ==, 0);
    root = gtk_window_get_child(test_window(session));
    stack = stack_of(root);
    home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    GtkWidget *row = gtk_widget_get_first_child(home);
    GtkWidget *title = gtk_widget_get_first_child(row);
    button = gtk_widget_get_last_child(row);
    g_assert_cmpint(gtk_orientable_get_orientation(GTK_ORIENTABLE(row)), ==, GTK_ORIENTATION_HORIZONTAL);
    g_assert_cmpint(gtk_box_get_spacing(GTK_BOX(row)), ==, 12);
    gtk_test_accessible_assert_role(title, GTK_ACCESSIBLE_ROLE_HEADING);
    gtk_test_accessible_assert_property(title, GTK_ACCESSIBLE_PROPERTY_LEVEL, 2);
    gtk_test_accessible_assert_property(button, GTK_ACCESSIBLE_PROPERTY_LABEL, "Accessible action");
    gtk_test_accessible_assert_property(button, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Description");
    g_assert_true(gtk_widget_has_css_class(button, "suggested-action"));
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_focus(session, "title"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_focus(session, "action"), ==, 0);
    g_assert_true(gtk_window_get_focus(test_window(session)) == button);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("nested layout, semantic role, accessible properties and focus checked\n");

    /* Changes retain ordered snapshots even when the native field has moved on. */
    g_assert_cmpint(tarn_ui_gtk_create("Input", "Input", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "form"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_entry(session, "form", "name", "", 4), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_entry(session, "form", "bad", "", 0), ==, 1);
    root = gtk_window_get_child(test_window(session));
    stack = stack_of(root);
    GtkWidget *form = gtk_stack_get_child_by_name(GTK_STACK(stack), "form");
    GtkWidget *entry = gtk_widget_get_first_child(form);
    g_assert_cmpint(gtk_entry_get_max_length(GTK_ENTRY(entry)), ==, 256);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "form"), ==, 0);
    for (int i = 0; i < 100; ++i) {
        g_assert_cmpint(tarn_ui_gtk_set_input(session, "name", "Desde el modelo"), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_set_input(session, "name", ""), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "form", "Wrong type"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "name", "\xff"), ==, 1);
    char oversized[258];
    memset(oversized, 'x', 257);
    oversized[257] = 0;
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "name", oversized), ==, 1);
    g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, "");
    int position = 0;
    gtk_editable_insert_text(GTK_EDITABLE(entry), "Ana", -1, &position);
    gtk_editable_insert_text(GTK_EDITABLE(entry), " María", -1, &position);
    event = tarn_ui_gtk_next(session);
    if (event == 1) event = tarn_ui_gtk_next(session);
    g_assert_cmpint(event, ==, 1004);
    g_assert_cmpstr(tarn_ui_gtk_event_text(session), ==, "Ana");
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, 1004);
    g_assert_cmpstr(tarn_ui_gtk_event_text(session), ==, "Ana María");
    g_assert_cmpstr(gtk_editable_get_text(GTK_EDITABLE(entry)), ==, "Ana María");
    for (int i = 0; i < 65; ++i) g_signal_emit_by_name(entry, "changed");
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, -8);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("ordered UTF-8 input snapshots and overflow cleanup checked\n");

    /* Dynamic keyed rows preserve native identity, order and signal lifetime. */
    g_assert_cmpint(tarn_ui_gtk_create("Dynamic", "Dynamic", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "details"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "rows", 0, 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_item(session, "alpha", "Alpha", 21), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_item(session, "beta", "Beta", 22), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
    root = gtk_window_get_child(test_window(session));
    stack = stack_of(root);
    home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    GtkWidget *rows = gtk_widget_get_first_child(home);
    GtkWidget *alpha = gtk_widget_get_first_child(rows);
    GtkWidget *beta = gtk_widget_get_last_child(rows);
    g_object_ref(alpha); /* Retain deliberately to test signal disconnection. */
    for (int i = 0; i < 20; ++i) {
        g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 2), ==, 0);
        if (i % 2 == 0) {
            g_assert_cmpint(tarn_ui_gtk_list_item(session, "beta", "Edited", 22), ==, 0);
            g_assert_cmpint(tarn_ui_gtk_list_item(session, "alpha", "Alpha", 21), ==, 0);
        } else {
            g_assert_cmpint(tarn_ui_gtk_list_item(session, "alpha", "Alpha", 21), ==, 0);
            g_assert_cmpint(tarn_ui_gtk_list_item(session, "beta", "Edited", 22), ==, 0);
        }
        g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
        g_assert_true(gtk_widget_get_first_child(rows) == (i % 2 == 0 ? beta : alpha));
        g_assert_true(gtk_widget_get_last_child(rows) == (i % 2 == 0 ? alpha : beta));
        g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(beta)), ==, "Edited");
    }
    g_assert_cmpint(tarn_ui_gtk_focus(session, "alpha"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "details"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_item(session, "beta", "Edited", 22), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
    g_assert_null(gtk_widget_get_parent(alpha));
    g_assert_cmpint(tarn_ui_gtk_focus(session, "alpha"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    g_assert_true(gtk_window_get_focus(test_window(session)) == beta);
    g_signal_emit_by_name(alpha, "clicked");
    g_signal_emit_by_name(beta, "clicked");
    event = tarn_ui_gtk_next(session);
    if (event == 1) event = tarn_ui_gtk_next(session);
    g_assert_cmpint(event, ==, 1022); /* Removed alpha emits no stale callback. */
    g_object_unref(alpha);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
    g_assert_null(gtk_widget_get_first_child(rows));
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "home", 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 257), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("dynamic identity, order, hidden-page focus, removed handlers and empty list cleaned\n");

    g_assert_cmpint(tarn_ui_gtk_create("Partial dynamic", "Dynamic", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "rows", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_item(session, "row", "Value", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_item(session, "row", "Duplicate", 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_finish(session, 1), ==, 0);
    g_assert_true(test_clean());

    /* Replacing a full region may temporarily hold both generations. */
    g_assert_cmpint(tarn_ui_gtk_create("Full dynamic", "Dynamic", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "rows", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 255), ==, 1);
    for (unsigned generation = 0; generation < 2; ++generation) {
        g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 254), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "interleaved", "No"), ==, 1);
        for (unsigned i = 0; i < 254; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "gen-%u-row-%u", generation, i);
            g_assert_cmpint(tarn_ui_gtk_list_item(session, key, "Value", 1), ==, 0);
        }
        g_assert_cmpint(tarn_ui_gtk_list_item(session, "extra", "Overflow", 1), ==, 1);
        g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "rows", "foreign", "No"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "gen-0-row-0", "Gone"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "gen-1-row-0", "Retained"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_request_close(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 0), ==, 6);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("full dynamic replacement and capacity bounds cleaned\n");

    /* Mixed conditional subtree: retain shared nodes, dispose every descendant. */
    g_assert_cmpint(tarn_ui_gtk_create("Mixed", "Mixed", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "details"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "region", 0, 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "summary", 0, "Shared", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "group", 6, "", 0, 8), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "title", 1, "Heading", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "field", 5, "Initial", 31, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "actions", 7, "", 0, 4), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "actions", "apply", 4, "Apply", 32, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    root = gtk_window_get_child(test_window(session));
    stack = stack_of(root);
    home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    GtkWidget *region = gtk_widget_get_first_child(home);
    GtkWidget *summary = gtk_widget_get_first_child(region);
    GtkWidget *group = gtk_widget_get_last_child(region);
    title = gtk_widget_get_first_child(group);
    entry = gtk_widget_get_next_sibling(title);
    GtkWidget *actions = gtk_widget_get_last_child(group);
    button = gtk_widget_get_first_child(actions);
    gtk_test_accessible_assert_role(title, GTK_ACCESSIBLE_ROLE_HEADING);
    gtk_test_accessible_assert_property(title, GTK_ACCESSIBLE_PROPERTY_LEVEL, 2);
    g_assert_true(gtk_widget_has_css_class(button, "suggested-action"));
    g_object_ref(entry);
    g_object_ref(button);
    for (int i = 0; i < 20; ++i) {
        g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 6), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "summary", 0, "Updated", 0, 0), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "group", 6, "", 0, 12), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "title", 1, "Heading", 0, 0), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "actions", 7, "", 0, 8), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "actions", "apply", 4, "Apply", 32, 0), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "field", 5, "Model update", 31, 0), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
        g_assert_true(gtk_widget_get_first_child(region) == summary);
        g_assert_true(gtk_widget_get_last_child(group) == entry);
        g_assert_true(gtk_widget_get_first_child(actions) == button);
    }
    g_assert_cmpint(gtk_box_get_spacing(GTK_BOX(group)), ==, 12);
    g_assert_cmpint(tarn_ui_gtk_focus(session, "field"), ==, 0); /* Entry delegates focus to GtkText. */
    g_assert_true(gtk_widget_grab_focus(entry));
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "details"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "summary", 0, "Closed", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_true(gtk_widget_get_first_child(region) == summary);
    g_assert_true(gtk_widget_get_last_child(region) == summary);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "field", "Gone"), ==, 1);
    gtk_editable_set_text(GTK_EDITABLE(entry), "Detached");
    g_signal_emit_by_name(button, "clicked");
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "summary", 0, "Reopened", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "field", 5, "Restored", 31, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_true(gtk_widget_get_last_child(region) != entry);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    /* Only a deliberately queued retained entry event may be delivered. */
    GtkWidget *replacement = gtk_widget_get_last_child(region);
    int edit_position = 8;
    gtk_editable_insert_text(GTK_EDITABLE(replacement), "!", -1, &edit_position);
    event = tarn_ui_gtk_next(session);
    if (event == 1) event = tarn_ui_gtk_next(session);
    g_assert_cmpint(event, ==, 1031);
    g_assert_cmpstr(tarn_ui_gtk_event_text(session), ==, "Restored!");
    g_object_unref(entry);
    g_object_unref(button);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("mixed subtree identity, roles, model writes, removal and retained signal disconnection checked\n");

    g_assert_cmpint(tarn_ui_gtk_create("Tree capacity", "Mixed", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "region", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 255), ==, 1);
    for (unsigned generation = 0; generation < 2; ++generation) {
        char group_key[32];
        snprintf(group_key, sizeof(group_key), "group-%u", generation);
        g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 254), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", group_key, 6, "", 0, 0), ==, 0);
        for (unsigned i = 0; i < 253; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "tree-%u-leaf-%u", generation, i);
            g_assert_cmpint(tarn_ui_gtk_tree_node(session, group_key, key, 0, "Label", 0, 0), ==, 0);
        }
        g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_tree_begin(session, group_key, 0), ==, 1);
        g_assert_cmpint(tarn_ui_gtk_list_begin(session, group_key, 0), ==, 1);
    }
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "tree-0-leaf-0", "Gone"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "region", 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("whole-subtree replacement at capacity and exclusive ownership checked\n");

    g_assert_cmpint(tarn_ui_gtk_create("Partial tree", "Mixed", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "region", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "group", 6, "", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "field", 5, "Value", 1, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 2), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "group", 6, "", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "field", 5, "Move parent", 1, 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_finish(session, 1), ==, 0);
    g_assert_true(test_clean());

    /* Partial mount must also be released on ordinary error return. */
    g_assert_cmpint(tarn_ui_gtk_create("Partial", "Partial", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "partial"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "partial", "button", "Button", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "partial"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_finish(session, 1), ==, 0);
    g_assert_true(test_clean());
    g_print("partial mount cleaned\n");

    g_assert_cmpint(tarn_ui_gtk_create("Bounds", "Bounds", &session), ==, 0);
    for (unsigned i = 0; i < 32; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "page-%u", i);
        g_assert_cmpint(tarn_ui_gtk_add_page(session, key), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "page-overflow"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_assert_cmpint(tarn_ui_gtk_create("Bounds", "Bounds", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    for (unsigned i = 0; i < 255; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "label-%u", i);
        g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", key, "Value"), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_add_label(session, "home", "overflow", "Value"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("page/widget bounds and mixed action ordering checked\n");
    /* Long pages scroll; keyboard traversal reaches and reveals every control. */
    g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, NULL);
    g_assert_cmpint(tarn_ui_gtk_create("Keyboard", "Keyboard", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "rows", 0, 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_box(session, "home", "region", 0, 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "back", "Back", 99), ==, 0);
    char long_text[257];
    memset(long_text, 'W', 256);
    long_text[256] = 0;
    g_assert_cmpint(tarn_ui_gtk_list_begin(session, "rows", 32), ==, 0);
    for (unsigned i = 0; i < 32; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "row-%u", i);
        g_assert_cmpint(tarn_ui_gtk_list_item(session, key, i == 0 ? long_text : "Row", 100 + i), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_list_end(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 4), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "group", 6, "", 0, 6), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "field-title", 1, "Options", 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "field", 5, "Note", 200, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "group", "apply", 4, "Apply", 201, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_set_accessible(session, "field", "Note field", "Kept while open."), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_set_accessible(session, "apply", "Apply note", "Saves the note."), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    settle();
    GtkWindow *window = test_window(session);
    root = gtk_window_get_child(window);
    GtkWidget *scroll = gtk_widget_get_last_child(root);
    stack = stack_of(root);
    home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    rows = gtk_widget_get_first_child(home);
    region = gtk_widget_get_next_sibling(rows);
    GtkWidget *back_button = gtk_widget_get_last_child(home);
    group = gtk_widget_get_first_child(region);
    entry = gtk_widget_get_next_sibling(gtk_widget_get_first_child(group));
    GtkWidget *apply = gtk_widget_get_last_child(group);
    /* Bounded window: 32 rows and a 256-character label neither widen nor lengthen it. */
    g_assert_cmpint(gtk_widget_get_width(GTK_WIDGET(window)), <=, 480);
    g_assert_cmpint(gtk_widget_get_height(GTK_WIDGET(window)), <=, 720);
    g_assert_cmpint(gtk_widget_get_width(gtk_widget_get_first_child(rows)), <=, 480);
    GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    g_assert_cmpfloat(gtk_adjustment_get_upper(vadjustment), >, gtk_adjustment_get_page_size(vadjustment));
    g_assert_false(in_viewport(back_button, scroll));
    /* Real accessible properties and roles of dynamic controls. */
    gtk_test_accessible_assert_role(entry, GTK_ACCESSIBLE_ROLE_TEXT_BOX);
    gtk_test_accessible_assert_property(entry, GTK_ACCESSIBLE_PROPERTY_LABEL, "Note field");
    gtk_test_accessible_assert_property(entry, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Kept while open.");
    gtk_test_accessible_assert_role(apply, GTK_ACCESSIBLE_ROLE_BUTTON);
    gtk_test_accessible_assert_property(apply, GTK_ACCESSIBLE_PROPERTY_LABEL, "Apply note");
    gtk_test_accessible_assert_role(gtk_widget_get_first_child(group), GTK_ACCESSIBLE_ROLE_HEADING);
    /* Tab: rows in model order, then field, apply and back; each one revealed. */
    g_assert_cmpint(tarn_ui_gtk_focus(session, "row-0"), ==, 0);
    settle();
    row = gtk_widget_get_first_child(rows);
    for (unsigned i = 0; i < 32; ++i, row = gtk_widget_get_next_sibling(row)) {
        if (i) {
            g_assert_true(gtk_widget_child_focus(GTK_WIDGET(window), GTK_DIR_TAB_FORWARD));
            settle();
        }
        g_assert_true(gtk_window_get_focus(window) == row);
        g_assert_true(in_viewport(row, scroll));
    }
    GtkWidget *forward[] = {entry, apply, back_button};
    for (unsigned i = 0; i < 3; ++i) {
        g_assert_true(gtk_widget_child_focus(GTK_WIDGET(window), GTK_DIR_TAB_FORWARD));
        settle();
        g_assert_true(focus_within(window, forward[i]));
        g_assert_true(in_viewport(forward[i], scroll));
    }
    g_assert_cmpfloat(gtk_adjustment_get_value(vadjustment), >, 0);
    /* Shift+Tab walks the same order backwards. */
    GtkWidget *backward[] = {apply, entry, gtk_widget_get_last_child(rows)};
    for (unsigned i = 0; i < 3; ++i) {
        g_assert_true(gtk_widget_child_focus(GTK_WIDGET(window), GTK_DIR_TAB_BACKWARD));
        settle();
        g_assert_true(focus_within(window, backward[i]));
        g_assert_true(in_viewport(backward[i], scroll));
    }
    /* Keyboard activation uses the native activate binding, not a direct emit. */
    g_assert_true(gtk_widget_activate(gtk_window_get_focus(window)));
    event = tarn_ui_gtk_next(session);
    if (event == 1) event = tarn_ui_gtk_next(session);
    g_assert_cmpint(event, ==, 1131);
    g_assert_cmpint(tarn_ui_gtk_complete_event(session, 0), ==, 0);
    /* Removing the focused branch moves focus to a visible control on the page. */
    g_assert_cmpint(tarn_ui_gtk_focus(session, "field"), ==, 0);
    settle();
    g_assert_true(focus_within(window, entry));
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    settle();
    g_assert_null(gtk_widget_get_first_child(region));
    g_assert_true(gtk_window_get_focus(window) == gtk_widget_get_first_child(rows));
    g_assert_true(in_viewport(gtk_widget_get_first_child(rows), scroll));
    /* A remounted field is a new widget; its properties must be applied again. */
    g_assert_cmpint(tarn_ui_gtk_tree_begin(session, "region", 1), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_node(session, "region", "field", 5, "Note", 200, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_tree_end(session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_set_accessible(session, "field", "Note field", "Restored."), ==, 0);
    gtk_test_accessible_assert_property(gtk_widget_get_first_child(region), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, "Restored.");
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    g_print("bounded scrolling, wrapped long labels, Tab/Shift+Tab order, keyboard activation, focused-branch removal and dynamic accessibility checked\n");
    return 0;
}
