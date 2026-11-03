/* Test hooks for the Tarn GTK backend: the same symbols the Tarn fixtures
 * (tests/devtools, tests/reactive, tests/navigation) call on the C bridge's
 * TARN_UI_TESTING build. Counters are registered under GObject type qdata;
 * native.tarn finds them there (see STATS_KEY). Never linked into apps. */
#include <gtk/gtk.h>
#include <stdint.h>

enum { COUNTER_UPDATES, HEADING_UPDATES, TEXT_CALLS, PAGE_CALLS, ALLOCATED, FREED,
       CONNECTED, DISCONNECTED, WINDOWS, WINDOWS_FINALIZED, DEV_CHANNELS, STATS };
static int32_t stats[STATS];

__attribute__((constructor)) static void install(void) {
    g_type_set_qdata(G_TYPE_OBJECT, g_quark_from_static_string("tarn-ui-test-stats"), stats);
}

static GtkWidget *session_window(void) {
    GListModel *windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); ++i) {
        GtkWidget *window = g_list_model_get_item(windows, i);
        g_object_unref(window);
        if (g_object_get_data(G_OBJECT(window), "tarn-ui-session")) return window;
    }
    return NULL;
}
static GtkWidget *find(GtkWidget *widget, const char *id) {
    if (!widget) return NULL;
    if (g_strcmp0(g_object_get_data(G_OBJECT(widget), "tarn-ui-id"), id) == 0) return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find(child, id);
        if (found) return found;
    }
    return NULL;
}

int32_t test_clean(void) {
    return !session_window() && stats[DEV_CHANNELS] == 0 && stats[ALLOCATED] == stats[FREED] &&
        stats[WINDOWS] == stats[WINDOWS_FINALIZED] && stats[CONNECTED] == stats[DISCONNECTED];
}
uint32_t test_counter_updates(void) { return (uint32_t)stats[COUNTER_UPDATES]; }
uint32_t test_heading_updates(void) { return (uint32_t)stats[HEADING_UPDATES]; }
uint32_t test_component_text_calls(void) { return (uint32_t)stats[TEXT_CALLS]; }
uint32_t test_page_calls(void) { return (uint32_t)stats[PAGE_CALLS]; }

int32_t test_entry_edits(void) {
    GtkWidget *entry = find(session_window(), "input");
    if (!GTK_IS_ENTRY(entry)) return 0;
    int position = 0;
    gtk_editable_insert_text(GTK_EDITABLE(entry), "Ana", -1, &position);
    gtk_editable_insert_text(GTK_EDITABLE(entry), " María", -1, &position);
    return 1;
}

int32_t test_dev_mode(void) {
    const char *mode = g_getenv("TARN_UI_TEST_MODE");
    if (g_strcmp0(mode, "handler-error") == 0) return 6;
    if (g_strcmp0(mode, "close-pending") == 0) return 7;
    if (g_strcmp0(mode, "timeout") == 0) return 8;
    if (g_strcmp0(mode, "late-handler") == 0) return 9;
    if (g_strcmp0(mode, "close-return") == 0) return 10;
    return 0;
}
void test_dev_delay(void) {
    /* Deliberately block the fixture handler past its deadline, not a UI settle wait. */
    g_usleep(2100000);
}
void test_dev_prepare(void) {
    const char *mode = g_getenv("TARN_UI_TEST_MODE");
    GtkWidget *window = session_window();
    GtkWidget *button = find(window, "increment");
    if (g_strcmp0(mode, "disabled") == 0) gtk_widget_set_sensitive(button, FALSE);
    if (g_strcmp0(mode, "hidden") == 0) gtk_widget_set_visible(button, FALSE);
    if (g_strcmp0(mode, "ancestor-disabled") == 0) gtk_widget_set_sensitive(gtk_window_get_child(GTK_WINDOW(window)), FALSE);
    if (g_strcmp0(mode, "native-text") == 0) gtk_label_set_text(GTK_LABEL(find(window, "counter")), "777");
    if (g_strcmp0(mode, "full-events") == 0)
        for (int i = 0; i < 64; ++i) g_signal_emit_by_name(button, "clicked");
}

/* Counter smoke (tests/gtk/counter-smoke.tarn). */
int32_t test_activate(int32_t count) {
    GtkWidget *button = find(session_window(), "increment");
    if (!button || count < 0) return 0;
    /* Invoke the actual native clicked signal, not the backend's handler. */
    for (int32_t i = 0; i < count; ++i) g_signal_emit_by_name(button, "clicked");
    return 1;
}
int32_t test_activate_native(void) {
    GtkWidget *button = find(session_window(), "increment");
    return button ? gtk_widget_activate(button) : 0;
}
int32_t test_label_equals(int64_t value) {
    GtkWidget *label = find(session_window(), "counter");
    if (!GTK_IS_LABEL(label)) return 0;
    char text[32];
    g_snprintf(text, sizeof(text), "%" G_GINT64_FORMAT, value);
    return g_str_equal(text, gtk_label_get_text(GTK_LABEL(label)));
}
