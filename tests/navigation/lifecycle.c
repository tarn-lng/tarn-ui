#include "../../backends/linux/gtk/bridge.h"
#include "../gtk/test_hooks.h"

static gpointer wrong_thread(gpointer session) {
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "wrong"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_add_button(session, "home", "wrong", "Wrong", 1), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Wrong"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 2);
    return NULL;
}

int main(void) {
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
    GtkWidget *stack = gtk_widget_get_last_child(root);
    GtkWidget *home = gtk_stack_get_child_by_name(GTK_STACK(stack), "home");
    GtkWidget *details = gtk_stack_get_child_by_name(GTK_STACK(stack), "details");
    GtkWidget *label = gtk_widget_get_first_child(home);
    GtkWidget *button = gtk_widget_get_last_child(home);
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
    g_assert_cmpint(tarn_ui_gtk_set_text(session, "home-value", "Closed"), ==, 6);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    g_assert_true(test_clean());
    TestStats stats = test_stats();
    g_print("components=%u/%u handlers=%u/%u; identity retained across 20 round trips\n",
            stats.components, stats.components_finalized, stats.connected, stats.disconnected);

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
    return 0;
}
