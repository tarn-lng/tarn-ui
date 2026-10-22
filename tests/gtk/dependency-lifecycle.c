/* Standalone dependency reproducer: no Tarn bridge or session. */
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <string.h>

static gboolean painted;
static unsigned finalized;

static void gone(gpointer data, GObject *object) {
    (void)data;
    (void)object;
    ++finalized;
}

static gboolean deadline(gpointer data) {
    (void)data;
    g_error("dependency lifecycle deadline exceeded");
    return G_SOURCE_REMOVE;
}

static void after_paint(GdkFrameClock *clock, gpointer data) {
    (void)clock;
    (void)data;
    painted = TRUE;
}

int main(int argc, char **argv) {
    gboolean release_map = argc == 2 && strcmp(argv[1], "--release-font-map") == 0;
    g_assert_true(argc == 1 || release_map);
    g_assert_true(gtk_init_check());
    guint watchdog = g_timeout_add_seconds(10, deadline, NULL);
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    g_object_ref(window);
    GtkWidget *label = gtk_label_new("Hola desde Tarn");
    g_object_weak_ref(G_OBJECT(window), gone, NULL);
    g_object_weak_ref(G_OBJECT(label), gone, NULL);
    gtk_window_set_default_size(window, 480, 240);
    gtk_window_set_child(window, label);
    gtk_window_present(window);
    GdkFrameClock *clock = gtk_widget_get_frame_clock(GTK_WIDGET(window));
    gulong handler = g_signal_connect(clock, "after-paint", G_CALLBACK(after_paint), NULL);
    while (!painted) g_main_context_iteration(NULL, TRUE);
    g_signal_handler_disconnect(clock, handler);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    g_object_weak_ref(G_OBJECT(surface), gone, NULL);
    gtk_window_destroy(window);
    g_object_unref(window);
    while (finalized < 3) g_main_context_iteration(NULL, TRUE);
    g_source_remove(watchdog);
    g_print("window, label and surface finalized\n");
    if (release_map) {
        /* Diagnostic at process end only; never session-level production cleanup. */
        pango_cairo_font_map_set_default(NULL);
        g_print("default font map released\n");
    }
    return 0;
}
