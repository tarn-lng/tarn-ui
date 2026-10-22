#include "../../backends/linux/gtk/bridge.h"
#include "test_hooks.h"
#include <string.h>

static gboolean captured;
static const char *png;

static void finish_process_diagnostic(void) {
    if (g_strcmp0(g_getenv("TARN_UI_TEST_CLOSE_DISPLAY"), "1") != 0) return;
    /* Process-end dependency diagnosis only; never a per-session operation. */
    GdkDisplay *display = gdk_display_get_default();
    gdk_display_sync(display);
    gdk_gl_context_clear_current();
    gdk_display_close(display);
    g_print("diagnostic display close completed\n");
}

static gboolean deadline(gpointer data) {
    (void)data;
    g_error("GTK test deadline exceeded");
    return G_SOURCE_REMOVE;
}

static void after_paint(GdkFrameClock *clock, gpointer data) {
    (void)clock;
    if (captured) return;
    GtkWidget *window = data;
    int width = gtk_widget_get_width(window);
    int height = gtk_widget_get_height(window);
    if (width <= 0 || height <= 0) return;
    GdkPaintable *paintable = gtk_widget_paintable_new(window);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    g_assert_nonnull(node);
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(window));
    g_assert_nonnull(renderer);
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node, NULL);
    g_assert_nonnull(texture);
    g_assert_true(gdk_texture_save_to_png(texture, png));
    g_print("rendered=%dx%d\n", width, height);
    g_object_unref(texture);
    gsk_render_node_unref(node);
    g_object_unref(paintable);
    captured = TRUE;
}

static gpointer wrong_thread(gpointer data) {
    void *out = NULL;
    g_assert_cmpint(tarn_ui_gtk_create("thread", "thread", &out), ==, 2);
    g_assert_null(out);
    g_assert_cmpint(tarn_ui_gtk_next(data), ==, -2);
    g_assert_cmpint(tarn_ui_gtk_request_close(data), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_release(data), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_counter(data, 999), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_set_heading(data, "wrong thread"), ==, 2);
    g_assert_cmpint(tarn_ui_gtk_enable_counter(data, 999), ==, 2);
    return NULL;
}

int main(int argc, char **argv) {
    gboolean control = (argc == 3 || argc == 4) && strcmp(argv[1], "--gtk-control") == 0;
    g_assert_true(argc == 2 || control);
    png = argv[control ? 2 : 1];
    if (control) {
        gint64 cycles = argc == 4 ? g_ascii_strtoll(argv[3], NULL, 10) : 16;
        g_assert_true(cycles >= 1 && cycles <= 64);
        /* Same native widgets/render path, without a bridge session or handlers.
         * Compare process-global LSan findings against this GTK-only control. */
        g_assert_true(gtk_init_check());
        guint watchdog = g_timeout_add_seconds(20, deadline, NULL);
        for (int i = 0; i < cycles; ++i) {
            GtkWindow *window = GTK_WINDOW(gtk_window_new());
            g_object_ref(window);
            gtk_window_set_title(window, "Tarn UI");
            gtk_window_set_default_size(window, 480, 240);
            GtkWidget *label = gtk_label_new("Hola desde Tarn");
            gtk_widget_add_css_class(label, "title-1");
            gtk_widget_set_margin_start(label, 24);
            gtk_widget_set_margin_end(label, 24);
            gtk_widget_set_margin_top(label, 24);
            gtk_widget_set_margin_bottom(label, 24);
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
            gtk_window_set_child(window, box);
            gtk_box_append(GTK_BOX(box), label);
            gtk_box_append(GTK_BOX(box), gtk_label_new("0"));
            GtkWidget *button = gtk_button_new_with_label("Incrementar");
            gtk_widget_set_margin_start(button, 24);
            gtk_widget_set_margin_end(button, 24);
            gtk_widget_set_margin_bottom(button, 24);
            gtk_box_append(GTK_BOX(box), button);
            gtk_window_present(window);
            if (i == 0) {
                GdkFrameClock *clock = gtk_widget_get_frame_clock(GTK_WIDGET(window));
                g_assert_nonnull(clock);
                gulong handler = g_signal_connect(clock, "after-paint", G_CALLBACK(after_paint), window);
                while (!captured) g_main_context_iteration(NULL, TRUE);
                g_signal_handler_disconnect(clock, handler);
            }
            gtk_window_destroy(window);
            g_object_unref(window);
        }
        g_source_remove(watchdog);
        g_print("GTK-only control: %" G_GINT64_FORMAT " windows created/destroyed\n", cycles);
        finish_process_diagnostic();
        return 0;
    }
    g_assert_cmpuint(tarn_ui_gtk_abi(), ==, 6);
    void *s = NULL;
    g_assert_cmpint(tarn_ui_gtk_create(NULL, "x", &s), ==, 1);
    g_assert_null(s);
    g_assert_cmpint(tarn_ui_gtk_create("\xff", "x", &s), ==, 1);
    /* The watchdog is a deadline, not a delay used to settle the UI. */
    guint watchdog = g_timeout_add_seconds(20, deadline, NULL);
    for (int stage = 1; stage <= 3; ++stage) {
        test_fail_at(stage);
        g_assert_cmpint(tarn_ui_gtk_create("partial", "heading", &s), ==, 5);
        g_assert_null(s);
        g_assert_true(test_clean());
    }
    test_fail_at(0);
    g_assert_cmpint(tarn_ui_gtk_create("partial counter", "heading", &s), ==, 0);
    test_fail_at(4);
    g_assert_cmpint(tarn_ui_gtk_enable_counter(s, 0), ==, 5);
    g_assert_cmpint(tarn_ui_gtk_release(s), ==, 0);
    g_assert_true(test_clean());
    test_fail_at(0);
    for (int i = 0; i < 16; ++i) {
        g_assert_cmpint(tarn_ui_gtk_create("Tarn UI", "Hola desde Tarn", &s), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_set_counter(s, 0), ==, 1);
        g_assert_cmpint(tarn_ui_gtk_enable_counter(s, 0), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_enable_counter(s, 0), ==, 1);
        void *nested = NULL;
        g_assert_cmpint(tarn_ui_gtk_create("nested", "x", &nested), ==, 3);
        g_assert_null(nested);
        GtkWindow *window = test_window(s);
        g_assert_cmpstr(gtk_window_get_title(window), ==, "Tarn UI");
        GtkWidget *heading = gtk_widget_get_first_child(gtk_window_get_child(window));
        g_assert_true(GTK_IS_LABEL(heading));
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(heading)), ==, "Hola desde Tarn");
        g_assert_cmpint(tarn_ui_gtk_set_heading(s, "Estado derivado"), ==, 0);
        g_assert_true(heading == gtk_widget_get_first_child(gtk_window_get_child(window)));
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(heading)), ==, "Estado derivado");
        g_assert_cmpint(tarn_ui_gtk_set_heading(s, "\xff"), ==, 1);
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(heading)), ==, "Estado derivado");
        g_assert_cmpint(tarn_ui_gtk_set_heading(s, "Hola desde Tarn"), ==, 0);
        if (i == 0) {
            GThread *thread = g_thread_new("wrong-ui-thread", wrong_thread, s);
            g_thread_join(thread);
            g_assert_cmpint(tarn_ui_gtk_next(s), ==, 1);
            g_assert_true(gtk_widget_get_mapped(GTK_WIDGET(window)));
            GdkFrameClock *clock = gtk_widget_get_frame_clock(GTK_WIDGET(window));
            g_assert_nonnull(clock);
            gulong handler = g_signal_connect(clock, "after-paint", G_CALLBACK(after_paint), window);
            gtk_widget_queue_draw(GTK_WIDGET(window));
            while (!captured) g_main_context_iteration(NULL, TRUE);
            g_signal_handler_disconnect(clock, handler);
        }
        g_assert_cmpint(test_activate(64), ==, 1);
        g_assert_true(test_label_equals(0)); /* callbacks do not change the label */
        if (i == 0) {
            for (int n = 0; n < 64; ++n) g_assert_cmpint(tarn_ui_gtk_next(s), ==, 3);
            g_assert_cmpint(test_activate(65), ==, 1);
            g_assert_cmpint(tarn_ui_gtk_next(s), ==, -8);
            g_assert_cmpint(tarn_ui_gtk_set_counter(s, 99), ==, 8);
        }
        /* On later iterations close supersedes an unconsumed Opened event. */
        g_assert_cmpint(tarn_ui_gtk_request_close(s), ==, 0);
        g_assert_cmpint(tarn_ui_gtk_request_close(s), ==, 6);
        g_assert_cmpint(tarn_ui_gtk_next(s), ==, 2);
        g_assert_cmpint(tarn_ui_gtk_next(s), ==, -6);
        g_assert_cmpint(test_activate(1), ==, 1); /* late signals cannot revive events */
        g_assert_cmpint(tarn_ui_gtk_set_counter(s, 99), ==, 6);
        g_assert_cmpint(tarn_ui_gtk_set_heading(s, "closed"), ==, 6);
        g_assert_cmpint(tarn_ui_gtk_next(s), ==, -6);
        g_assert_cmpint(tarn_ui_gtk_release(s), ==, 0);
        g_assert_true(test_clean());
    }
    g_source_remove(watchdog);
    TestStats stats = test_stats();
    g_print("sessions=%u/%u windows=%u/%u headings=%u/%u handlers=%u/%u\n",
            stats.allocated, stats.freed, stats.windows, stats.windows_finalized,
            stats.headings, stats.headings_finalized, stats.connected, stats.disconnected);
    g_print("counter-widgets=%u/%u\n", stats.counter_widgets, stats.counter_widgets_finalized);
    finish_process_diagnostic();
    return 0;
}
