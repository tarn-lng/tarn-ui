#define _GNU_SOURCE
#include "bridge.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct DevChannel DevChannel;
typedef struct {
    GtkWindow *window; /* explicit ref in addition to GTK's toplevel ref */
    gulong close_handler, map_handler;
    gboolean opened, closed, delivered_closed;
    GtkWidget *counter_label, *button; /* borrowed from the window's child tree */
    gulong click_handler;
    unsigned pending_increments; /* queue length, never application state */
    gboolean queue_full;
    uint64_t increment_tokens[64], enqueue_token, dispatched_token;
    unsigned increment_head;
    int32_t application_error;
#ifdef TARN_UI_DEVTOOLS
    DevChannel *dev;
#endif
} Session;

static Session *active;
static gboolean main_thread(void) { return syscall(SYS_gettid) == getpid(); }
#ifdef TARN_UI_DEVTOOLS
static int dev_start(Session *s);
static gboolean dev_pump(Session *s);
static void dev_complete(Session *s, int32_t status);
static void dev_dispose(Session *s, gboolean window_closed);
#define UI_ID(widget, id) g_object_set_data(G_OBJECT(widget), "tarn-ui-id", (gpointer)(id))
#else
#define UI_ID(widget, id) ((void)0)
#endif

#ifdef TARN_UI_TESTING
#include "../../../tests/gtk/test_hooks.h"
static TestStats stats;
static int fail_stage;
static unsigned dev_channels;
static void finalized(gpointer data, GObject *object) {
    (void)object;
    ++*(unsigned *)data;
}
#define FAIL_AT(stage) (fail_stage == (stage))
#else
#define FAIL_AT(stage) FALSE
#endif

uint32_t tarn_ui_gtk_abi(void) { return 4; }

static void clicked(GtkButton *button, gpointer data) {
    (void)button;
    Session *s = data;
    if (s->closed || s->queue_full) return;
    if (s->pending_increments == 64) s->queue_full = TRUE;
    else {
        s->increment_tokens[(s->increment_head + s->pending_increments) % 64] = s->enqueue_token;
        ++s->pending_increments;
    }
}

static void mapped(GtkWidget *widget, gpointer data) {
    (void)widget;
    Session *s = data;
    if (!s->closed) s->opened = TRUE;
}

static gboolean close_requested(GtkWindow *window, gpointer data) {
    (void)window;
    Session *s = data;
    s->closed = TRUE;
    s->opened = FALSE;
    s->pending_increments = 0;
    /* Queue closure only. Dispatch hides it; the runner owns destruction. */
    return TRUE;
}

static void dispose(Session *s) {
#ifdef TARN_UI_DEVTOOLS
    gboolean window_closed = s->closed;
#endif
    s->closed = TRUE;
    s->pending_increments = 0;
    if (s->click_handler) {
        g_signal_handler_disconnect(s->button, s->click_handler);
#ifdef TARN_UI_TESTING
        ++stats.disconnected;
#endif
    }
    if (s->window) {
        if (s->close_handler) {
            g_signal_handler_disconnect(s->window, s->close_handler);
#ifdef TARN_UI_TESTING
            ++stats.disconnected;
#endif
        }
        if (s->map_handler) {
            g_signal_handler_disconnect(s->window, s->map_handler);
#ifdef TARN_UI_TESTING
            ++stats.disconnected;
#endif
        }
        gtk_window_destroy(s->window);
        g_object_unref(s->window);
    }
#ifdef TARN_UI_DEVTOOLS
    dev_dispose(s, window_closed); /* close success follows native destruction */
#endif
    active = NULL;
    free(s);
#ifdef TARN_UI_TESTING
    ++stats.freed;
#endif
}

int32_t tarn_ui_gtk_create(const char *title, const char *heading, void **out) {
    if (!main_thread()) return 2;
    if (!out) return 1;
    *out = NULL;
    if (!title || !heading || !g_utf8_validate(title, -1, NULL) ||
        !g_utf8_validate(heading, -1, NULL)) return 1;
    if (active) return 3;
    if (!gtk_init_check()) return 4;
    Session *s = calloc(1, sizeof(*s));
    if (!s) return 5;
    active = s;
#ifdef TARN_UI_TESTING
    ++stats.allocated;
#endif
    if (FAIL_AT(1)) goto fail;
    s->window = GTK_WINDOW(gtk_window_new());
    UI_ID(s->window, "main");
    g_object_ref(s->window);
#ifdef TARN_UI_TESTING
    ++stats.windows;
    g_object_weak_ref(G_OBJECT(s->window), finalized, &stats.windows_finalized);
#endif
    if (FAIL_AT(2)) goto fail;
    gtk_window_set_title(s->window, title);
    gtk_window_set_default_size(s->window, 480, 240);
    GtkWidget *label = gtk_label_new(heading);
    UI_ID(label, "heading");
    gtk_widget_add_css_class(label, "title-1");
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);
    gtk_widget_set_margin_top(label, 24);
    gtk_widget_set_margin_bottom(label, 24);
    /* Parent sinks the child's floating ref; no extra child owner. */
    gtk_window_set_child(s->window, label);
#ifdef TARN_UI_TESTING
    ++stats.headings;
    g_object_weak_ref(G_OBJECT(label), finalized, &stats.headings_finalized);
#endif
    if (FAIL_AT(3)) goto fail;
    s->close_handler = g_signal_connect(s->window, "close-request",
                                       G_CALLBACK(close_requested), s);
    s->map_handler = g_signal_connect(s->window, "map", G_CALLBACK(mapped), s);
#ifdef TARN_UI_TESTING
    stats.connected += 2;
#endif
    gtk_window_present(s->window);
    *out = s;
    return 0;
fail:
    dispose(s);
    return 5;
}

static int32_t validate(void *raw) {
    if (!main_thread()) return 2;
    if (!raw || raw != active) return 1;
    return 0;
}

int32_t tarn_ui_gtk_next(void *raw) {
    int32_t status = validate(raw);
    if (status) return -status;
    Session *s = raw;
    for (;;) {
#ifdef TARN_UI_DEVTOOLS
        if (dev_pump(s)) continue;
#endif
        if (s->closed) {
            if (s->delivered_closed) return -6;
            gtk_widget_set_visible(GTK_WIDGET(s->window), FALSE);
            s->delivered_closed = TRUE;
            return 2;
        }
        if (s->queue_full) return -8;
        if (s->opened) {
            s->opened = FALSE;
            return 1;
        }
        if (s->pending_increments) {
            s->dispatched_token = s->increment_tokens[s->increment_head];
            s->increment_head = (s->increment_head + 1) % 64;
            --s->pending_increments;
            return 3;
        }
        g_main_context_iteration(NULL, TRUE);
    }
}

int32_t tarn_ui_gtk_request_close(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    gtk_window_close(s->window);
    return 0;
}

int32_t tarn_ui_gtk_release(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    dispose(raw);
    return 0;
}

int32_t tarn_ui_gtk_finish(void *raw, int32_t application_error) {
    int32_t status = validate(raw);
    if (status) return status;
    ((Session *)raw)->application_error = application_error;
    dispose(raw);
    return 0;
}

int32_t tarn_ui_gtk_set_counter(void *raw, int64_t value) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->counter_label) return 1;
    char text[32];
    snprintf(text, sizeof(text), "%" PRId64, value);
    gtk_label_set_text(GTK_LABEL(s->counter_label), text);
    return 0;
}

int32_t tarn_ui_gtk_complete_event(void *raw, int32_t status) {
    int32_t invalid = validate(raw);
    if (invalid) return invalid;
#ifdef TARN_UI_DEVTOOLS
    dev_complete(raw, status);
#else
    (void)status;
#endif
    ((Session *)raw)->dispatched_token = 0;
    return 0;
}

int32_t tarn_ui_gtk_enable_counter(void *raw, int64_t value) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->counter_label) return 1;
    GtkWidget *heading = gtk_window_get_child(s->window);
    g_object_ref(heading); /* keep alive while moving between parents */
    gtk_window_set_child(s->window, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    UI_ID(box, "content");
    gtk_window_set_child(s->window, box);
    gtk_box_append(GTK_BOX(box), heading);
    g_object_unref(heading);
    s->counter_label = gtk_label_new("");
    UI_ID(s->counter_label, "counter");
    gtk_box_append(GTK_BOX(box), s->counter_label);
    s->button = gtk_button_new_with_label("Incrementar");
    UI_ID(s->button, "increment");
    gtk_widget_set_margin_start(s->button, 24);
    gtk_widget_set_margin_end(s->button, 24);
    gtk_widget_set_margin_bottom(s->button, 24);
    gtk_box_append(GTK_BOX(box), s->button);
#ifdef TARN_UI_TESTING
    stats.counter_widgets += 3;
    g_object_weak_ref(G_OBJECT(box), finalized, &stats.counter_widgets_finalized);
    g_object_weak_ref(G_OBJECT(s->counter_label), finalized, &stats.counter_widgets_finalized);
    g_object_weak_ref(G_OBJECT(s->button), finalized, &stats.counter_widgets_finalized);
#endif
    s->click_handler = g_signal_connect(s->button, "clicked", G_CALLBACK(clicked), s);
#ifdef TARN_UI_TESTING
    ++stats.connected;
#endif
    if (FAIL_AT(4)) return 5; /* caller must release the still-owned session */
    status = tarn_ui_gtk_set_counter(raw, value);
#ifdef TARN_UI_DEVTOOLS
#ifdef TARN_UI_TESTING
    if (g_strcmp0(g_getenv("TARN_UI_TEST_MODE"), "duplicate") == 0) UI_ID(s->button, "counter");
#endif
    if (!status) status = dev_start(s);
#endif
    return status;
}

#ifdef TARN_UI_TESTING
TestStats test_stats(void) { return stats; }
void test_fail_at(int stage) { fail_stage = stage; }
GtkWindow *test_window(void *raw) {
    return validate(raw) == 0 ? ((Session *)raw)->window : NULL;
}
int32_t test_clean(void) {
    return !active && dev_channels == 0 && stats.allocated == stats.freed &&
        stats.windows == stats.windows_finalized &&
        stats.headings == stats.headings_finalized &&
        stats.counter_widgets == stats.counter_widgets_finalized &&
        stats.connected == stats.disconnected;
}
int32_t test_activate(int32_t count) {
    if (!main_thread() || !active || !active->button || count < 0) return 0;
    /* Invoke the actual native clicked signal, not the bridge's handler. */
    for (int32_t i = 0; i < count; ++i) g_signal_emit_by_name(active->button, "clicked");
    return 1;
}
int32_t test_activate_native(void) {
    if (!main_thread() || !active || !active->button) return 0;
    return gtk_widget_activate(active->button);
}
int32_t test_label_equals(int64_t value) {
    if (!main_thread() || !active || !active->counter_label) return 0;
    char text[32];
    snprintf(text, sizeof(text), "%" PRId64, value);
    return g_str_equal(text, gtk_label_get_text(GTK_LABEL(active->counter_label)));
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
    if (g_strcmp0(mode, "disabled") == 0) gtk_widget_set_sensitive(active->button, FALSE);
    if (g_strcmp0(mode, "hidden") == 0) gtk_widget_set_visible(active->button, FALSE);
    if (g_strcmp0(mode, "ancestor-disabled") == 0) gtk_widget_set_sensitive(gtk_window_get_child(active->window), FALSE);
    if (g_strcmp0(mode, "native-text") == 0) gtk_label_set_text(GTK_LABEL(active->counter_label), "777");
    if (g_strcmp0(mode, "full-events") == 0) test_activate(64);
}
#endif

#ifdef TARN_UI_DEVTOOLS
#include "devtools.inc"
#endif
