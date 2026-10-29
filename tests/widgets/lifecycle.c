#include "../../backends/linux/gtk/bridge.h"
#include "../gtk/test_hooks.h"

static int32_t next_event(void *session) {
    int32_t event = tarn_ui_gtk_next(session);
    return event == 1 ? tarn_ui_gtk_next(session) : event;
}

int main(void) {
    g_setenv("GTK_A11Y", "test", TRUE);
    void *session = NULL;
    g_assert_cmpint(tarn_ui_gtk_create("Controls", "Controls", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "on", 0, "Enabled", 1, 1, 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "check", 1, "Check", 2, 0, 0, 0), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "level", 2, "Level", 3, 5, 0, 10), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "count", 3, "Count", 4, -2, -5, 5), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "pick", 4, "Pick\nA\nB\nC", 5, 2, 0, 0), ==, 0);
    /* Invalid kinds, ranges, actions, captions and option lists. */
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 5, "Bad", 6, 0, 0, 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 2, "Bad", 6, 11, 0, 10), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 2, "Bad", 6, 0, 10, 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 3, "Bad", 0, 0, 0, 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 3, "Bad", 6, 0, 0, 2000000000), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 0, "Two\nlines", 6, 0, 0, 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 4, "Only caption", 6, 0, 0, 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 4, "Pick\nA\n\nC", 6, 0, 0, 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "bad", 4, "Pick\nA", 6, 1, 0, 0), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "on", 0, "Duplicate", 6, 0, 0, 1), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_show_page(session, "home"), ==, 0);
    GtkWidget *root = gtk_window_get_child(test_window(session));
    GtkWidget *viewport = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(gtk_widget_get_last_child(root)));
    GtkWidget *home = gtk_stack_get_child_by_name(GTK_STACK(gtk_viewport_get_child(GTK_VIEWPORT(viewport))), "home");
    GtkWidget *row = gtk_widget_get_first_child(home);
    GtkWidget *caption = gtk_widget_get_first_child(row);
    GtkWidget *on = gtk_widget_get_last_child(row);
    GtkWidget *check = gtk_widget_get_next_sibling(row);
    GtkWidget *level = gtk_widget_get_last_child(gtk_widget_get_next_sibling(check));
    GtkWidget *count_row = gtk_widget_get_next_sibling(gtk_widget_get_next_sibling(check));
    GtkWidget *count = gtk_widget_get_last_child(count_row);
    GtkWidget *pick = gtk_widget_get_last_child(gtk_widget_get_next_sibling(count_row));
    g_assert_true(GTK_IS_SWITCH(on) && GTK_IS_CHECK_BUTTON(check) && GTK_IS_SCALE(level));
    g_assert_true(GTK_IS_SPIN_BUTTON(count) && GTK_IS_DROP_DOWN(pick));
    g_assert_true(gtk_switch_get_active(GTK_SWITCH(on)));
    g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(level)), ==, 5);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(count)), ==, -2);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(pick)), ==, 2);
    gtk_test_accessible_assert_role(on, GTK_ACCESSIBLE_ROLE_SWITCH);
    gtk_test_accessible_assert_role(check, GTK_ACCESSIBLE_ROLE_CHECKBOX);
    gtk_test_accessible_assert_relation(on, GTK_ACCESSIBLE_RELATION_LABELLED_BY, caption, NULL);
    /* Model writes: no event, range checked, invalid text rejected. */
    for (int i = 0; i < 20; ++i) {
        g_assert_cmpint(tarn_ui_gtk_set_input(session, "level", i % 2 ? "10" : "0"), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_set_input(session, "on", i % 2 ? "1" : "0"), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_set_input(session, "pick", i % 2 ? "0" : "1"), ==, 0);
    }
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "level", "11"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "count", "-6"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "pick", "3"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "level", "4x"), ==, 1);
    g_assert_cmpint(tarn_ui_gtk_set_input(session, "level", ""), ==, 1);
    g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(level)), ==, 10);
    /* User changes queue the action with the new value, in order. */
    gtk_switch_set_active(GTK_SWITCH(on), FALSE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
    gtk_range_set_value(GTK_RANGE(level), 7);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(count), 4);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(pick), 2);
    const struct { int32_t event; const char *text; } expected[] = {
        {1001, "0"}, {1002, "1"}, {1003, "7"}, {1004, "4"}, {1005, "2"}};
    for (unsigned i = 0; i < 5; ++i) {
        g_assert_cmpint(i ? tarn_ui_gtk_next(session) : next_event(session), ==, expected[i].event);
        g_assert_cmpstr(tarn_ui_gtk_event_text(session), ==, expected[i].text);
        g_assert_cmpint(tarn_ui_gtk_complete_event(session, 0), ==, 0);
    }
    /* Keyboard activation of a check toggles it through GTK's own binding. */
    g_assert_true(gtk_widget_activate(check));
    g_assert_cmpint(tarn_ui_gtk_next(session), ==, 1002);
    g_assert_cmpstr(tarn_ui_gtk_event_text(session), ==, "0");
    /* Controls retained beyond the session emit nothing after release. */
    g_object_ref(level);
    g_assert_cmpint(tarn_ui_gtk_release(session), ==, 0);
    gtk_range_set_value(GTK_RANGE(level), 1);
    g_object_unref(level);
    g_assert_true(test_clean());
    TestStats stats = test_stats();
    g_print("controls=%u/%u handlers=%u/%u; values, ranges, roles, events and cleanup checked\n",
            stats.components, stats.components_finalized, stats.connected, stats.disconnected);

    /* Partial mounts with controls are released on error. */
    g_assert_cmpint(tarn_ui_gtk_create("Partial", "Partial", &session), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_page(session, "home"), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_add_control(session, "home", "level", 2, "Level", 3, 5, 0, 10), ==, 0);
    g_assert_cmpint(tarn_ui_gtk_finish(session, 1), ==, 0);
    g_assert_true(test_clean());
    g_print("partial control mount cleaned\n");
    return 0;
}
