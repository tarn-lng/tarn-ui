#define _GNU_SOURCE
#include "bridge.h"
#include <gtk/gtk.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef struct DevChannel DevChannel;
typedef struct {
    GtkWindow *window; /* explicit ref in addition to GTK's toplevel ref */
    gulong close_handler, map_handler;
    gboolean opened, closed, delivered_closed;
    gboolean background, hidden, woken; /* hide-on-close mode; coalesced wake from fd watches */
    GtkWidget *heading, *counter_label, *button; /* borrowed from the window's child tree */
    GtkWidget *pages;
    GHashTable *components; /* per-session borrowed widgets; the native tree owns them */
    unsigned page_count;
    GtkWidget *list_parent, *list_previous;
    GHashTable *list_seen;
    gboolean list_focus_removed;
    unsigned list_expected;
    gboolean tree_mode;
    GtkWidget *forced_parent; /* region reconciliation reuses the static creators */
    GHashTable *tree_last;
    gboolean pages_presented;
    gulong click_handler;
    unsigned pending_increments; /* queue length, never application state */
    gboolean queue_full;
    uint64_t increment_tokens[64], enqueue_token, dispatched_token;
    int32_t event_codes[64];
    char *event_texts[64], *dispatched_text;
    unsigned increment_head;
    int32_t application_error;
#ifdef TARN_UI_DEVTOOLS
    DevChannel *dev;
#endif
} Session;

static Session *active;
/* Form controls, defined after the core component registry. */
static gboolean is_control(GtkWidget *widget);
static gboolean is_indicator(GtkWidget *widget);
static void text_changed(GtkTextBuffer *buffer, gpointer data);
static char *text_view_text(GtkWidget *view);
static void text_view_write(GtkWidget *view, Session *s, const char *text);
static void control_enqueue(GtkWidget *widget, Session *s);
static void control_disconnect(GtkWidget *widget, Session *s, unsigned *count);
static int32_t control_write(GtkWidget *widget, Session *s, int64_t value);
static gboolean control_accepts(GtkWidget *widget, int64_t value);
static int64_t control_value(GtkWidget *widget);
static gboolean parse_value(const char *text, int64_t *out);
static gboolean main_thread(void) { return syscall(SYS_gettid) == getpid(); }

/* Long valid text must wrap at any character instead of widening the window. */
static void wrap_label(GtkWidget *label) {
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
}
static GtkWidget *wrapping_button(const char *text) {
    GtkWidget *button = gtk_button_new_with_label(text);
    /* gtk_button_set_label reuses this label child, so the policy persists. */
    wrap_label(gtk_button_get_child(GTK_BUTTON(button)));
    return button;
}
/* A bounded default height on the first monitor; pages scroll vertically
 * instead of growing the window past it. */
static int window_height_limit(void) {
    GListModel *monitors = gdk_display_get_monitors(gdk_display_get_default());
    int height = 768;
    GdkMonitor *monitor = g_list_model_get_n_items(monitors) ? g_list_model_get_item(monitors, 0) : NULL;
    if (monitor) {
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        height = geometry.height;
        g_object_unref(monitor);
    }
    return CLAMP(height - 128, 320, 720);
}
#ifdef TARN_UI_DEVTOOLS
static int dev_start(Session *s);
static gboolean dev_pump(Session *s);
static void dev_complete(Session *s, int32_t status);
static void dev_dispose(Session *s, gboolean window_closed);
static void dev_hidden(Session *s);
static gboolean menu_activate_key(Session *s, const char *key);
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

uint32_t tarn_ui_gtk_abi(void) { return 14; }

static void enqueue(Session *s, int32_t code) {
    if (s->closed || s->queue_full) return;
    if (s->pending_increments == 64) s->queue_full = TRUE;
    else {
        unsigned slot = (s->increment_head + s->pending_increments) % 64;
        s->increment_tokens[slot] = s->enqueue_token;
        s->event_codes[slot] = code;
        ++s->pending_increments;
    }
}

static void clicked(GtkButton *button, gpointer data) {
    (void)button;
    enqueue(data, 3);
}

static void component_clicked(GtkButton *button, gpointer data) {
    unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "tarn-ui-action"));
    enqueue(data, 1000 + (int32_t)action);
}

static void entry_changed(GtkEditable *entry, gpointer data) {
    Session *s = data;
    if (s->closed || s->queue_full) return;
    unsigned slot = (s->increment_head + s->pending_increments) % 64;
    unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "tarn-ui-action"));
    enqueue(s, 1000 + (int32_t)action);
    if (!s->queue_full) s->event_texts[slot] = g_strdup(gtk_editable_get_text(entry));
}

static void mapped(GtkWidget *widget, gpointer data) {
    (void)widget;
    Session *s = data;
    if (!s->closed) s->opened = TRUE;
}

static gboolean close_requested(GtkWindow *window, gpointer data) {
    (void)window;
    Session *s = data;
    if (s->background) {
        /* Background mode: the window hides; the session and its events stay. */
        gtk_widget_set_visible(GTK_WIDGET(s->window), FALSE);
        s->hidden = TRUE;
#ifdef TARN_UI_DEVTOOLS
        dev_hidden(s);
#endif
        return TRUE;
    }
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
    for (unsigned i = 0; i < 64; ++i) g_clear_pointer(&s->event_texts[i], g_free);
    g_clear_pointer(&s->dispatched_text, g_free);
    g_clear_pointer(&s->list_seen, g_hash_table_unref);
    g_clear_pointer(&s->tree_last, g_hash_table_unref);
    if (s->components) {
        GHashTableIter iter;
        gpointer widget;
        g_hash_table_iter_init(&iter, s->components);
        while (g_hash_table_iter_next(&iter, NULL, &widget)) {
            unsigned disconnected = g_signal_handlers_disconnect_by_func(widget, component_clicked, s);
            disconnected += g_signal_handlers_disconnect_by_func(widget, entry_changed, s);
            if (GTK_IS_TEXT_VIEW(widget))
                disconnected += g_signal_handlers_disconnect_by_func(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)), text_changed, s);
            control_disconnect(widget, s, &disconnected);
#ifdef TARN_UI_TESTING
            stats.disconnected += disconnected;
#else
            (void)disconnected;
#endif
        }
    }
    if (s->click_handler) {
        g_signal_handler_disconnect(s->button, s->click_handler);
#ifdef TARN_UI_TESTING
        ++stats.disconnected;
#endif
    }
    if (s->window) {
        /* Menu actions hold the session pointer: drop them with it. */
        gtk_widget_insert_action_group(GTK_WIDGET(s->window), "tarn", NULL);
        g_object_set_data(G_OBJECT(s->window), "tarn-ui-menu-actions", NULL);
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
    g_clear_pointer(&s->components, g_hash_table_unref);
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
    GtkWidget *label = g_object_new(GTK_TYPE_LABEL, "accessible-role", GTK_ACCESSIBLE_ROLE_HEADING, "label", heading, NULL);
    gtk_accessible_update_property(GTK_ACCESSIBLE(label), GTK_ACCESSIBLE_PROPERTY_LEVEL, 1, -1);
    wrap_label(label);
    s->heading = label;
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
        if (s->hidden) {
            s->hidden = FALSE;
            return 4;
        }
        if (s->woken) {
            s->woken = FALSE;
            return 5;
        }
        if (s->opened) {
            s->opened = FALSE;
            return 1;
        }
        if (s->pending_increments) {
            int32_t code = s->event_codes[s->increment_head];
            g_free(s->dispatched_text);
            s->dispatched_text = s->event_texts[s->increment_head];
            s->event_texts[s->increment_head] = NULL;
            s->dispatched_token = s->increment_tokens[s->increment_head];
            s->increment_head = (s->increment_head + 1) % 64;
            --s->pending_increments;
            return code;
        }
        g_main_context_iteration(NULL, TRUE);
    }
}

int32_t tarn_ui_gtk_request_close(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    s->background = FALSE; /* an explicit close ends the session even in background mode */
    gtk_window_close(s->window);
    return 0;
}

int32_t tarn_ui_gtk_release(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    dispose(raw);
    return 0;
}

/* Safe to call from a GLib source callback on the main thread: it only sets a
 * flag, so any number of wakes before next() collapse into one Wake event. */
int32_t tarn_ui_gtk_wake(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    ((Session *)raw)->woken = TRUE;
    return 0;
}

int32_t tarn_ui_gtk_set_background(void *raw, uint32_t enabled) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (enabled > 1) return 1;
    s->background = enabled;
    return 0;
}

int32_t tarn_ui_gtk_present(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    s->hidden = FALSE;
    gtk_window_present(s->window);
    return 0;
}

int32_t tarn_ui_gtk_hide(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    gtk_widget_set_visible(GTK_WIDGET(s->window), FALSE);
    return 0;
}

int32_t tarn_ui_gtk_is_visible(void *raw) {
    int32_t status = validate(raw);
    if (status) return -status;
    return gtk_widget_get_visible(GTK_WIDGET(((Session *)raw)->window)) ? 1 : 0;
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
#ifdef TARN_UI_TESTING
    ++stats.counter_updates;
#endif
    return 0;
}

int32_t tarn_ui_gtk_set_heading(void *raw, const char *text) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!text || !g_utf8_validate(text, -1, NULL)) return 1;
    gtk_label_set_text(GTK_LABEL(s->heading), text);
#ifdef TARN_UI_TESTING
    ++stats.heading_updates;
#endif
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
    if (s->counter_label || s->pages) return 1;
    GtkWidget *heading = gtk_window_get_child(s->window);
    g_object_ref(heading); /* keep alive while moving between parents */
    gtk_window_set_child(s->window, NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    UI_ID(box, "content");
    gtk_window_set_child(s->window, box);
    gtk_box_append(GTK_BOX(box), heading);
    g_object_unref(heading);
    s->counter_label = gtk_label_new("");
    wrap_label(s->counter_label);
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

/* Scoped component registry: no widget or callback owner escapes the session. */
static gboolean valid_key(const char *key) {
    if (!key || !*key || strlen(key) > 128) return FALSE;
    for (const char *p = key; *p; ++p)
        if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_') return FALSE;
    return strcmp(key, "main") && strcmp(key, "heading") && strcmp(key, "content") && strcmp(key, "pages") && strcmp(key, "scroll");
}

static void component_track(GtkWidget *widget) {
#ifdef TARN_UI_TESTING
    ++stats.components;
    g_object_weak_ref(G_OBJECT(widget), finalized, &stats.components_finalized);
#else
    (void)widget;
#endif
}

static int32_t component_admit(Session *s, const char *key) {
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (s->forced_parent) {
        /* Region node: tree_prepare already checked the key and capacity. */
        return valid_key(key) && s->components && !g_hash_table_contains(s->components, key) ? 0 : 1;
    }
    if (!valid_key(key) || s->counter_label || s->pages_presented || s->list_parent) return 1;
    if (s->components && (g_hash_table_contains(s->components, key) || g_hash_table_size(s->components) >= 256)) return 1;
    return 0;
}

int32_t tarn_ui_gtk_add_page(void *raw, const char *key) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    if (s->page_count >= 32) return 1;
    if (!s->pages) {
        s->components = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        UI_ID(root, "content");
        component_track(root);
        g_object_ref(s->heading);
        gtk_window_set_child(s->window, NULL);
        gtk_window_set_child(s->window, root);
        gtk_box_append(GTK_BOX(root), s->heading);
        g_object_unref(s->heading);
        GtkWidget *scroll = gtk_scrolled_window_new();
        UI_ID(scroll, "scroll");
        component_track(scroll);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_window_set_default_size(s->window, 480, window_height_limit());
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_box_append(GTK_BOX(root), scroll);
        s->pages = gtk_stack_new();
        UI_ID(s->pages, "pages");
        component_track(s->pages);
        gtk_stack_set_transition_type(GTK_STACK(s->pages), GTK_STACK_TRANSITION_TYPE_NONE);
        /* Size to the visible page; hidden long pages must not add empty space. */
        gtk_stack_set_vhomogeneous(GTK_STACK(s->pages), FALSE);
        /* The automatic GtkViewport scrolls focused descendants into view. */
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), s->pages);
    }
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(page, 24);
    gtk_widget_set_margin_end(page, 24);
    gtk_widget_set_margin_bottom(page, 24);
    component_track(page);
    /* UI_ID must reference the owned key, not a transient FFI string. */
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, page);
    UI_ID(page, owned);
    gtk_stack_add_named(GTK_STACK(s->pages), page, key);
    ++s->page_count;
    return 0;
}

/* Parent in the UI tree: GTK 4 unparents a collapsed expander's content, so
 * content boxes remember their host. */
static GtkWidget *ui_parent(GtkWidget *widget) {
    GtkWidget *host = g_object_get_data(G_OBJECT(widget), "tarn-ui-host");
    return host ? host : gtk_widget_get_parent(widget);
}
static gboolean ui_within(GtkWidget *widget, GtkWidget *ancestor) {
    for (GtkWidget *p = ui_parent(widget); p; p = ui_parent(p)) if (p == ancestor) return TRUE;
    return FALSE;
}
/* The child a keyed widget occupies in its parent box (a caption box, frame,
 * expander or scrolled window), or the widget itself. */
static GtkWidget *ui_slot(GtkWidget *widget) {
    GtkWidget *outer = g_object_get_data(G_OBJECT(widget), "tarn-ui-outer");
    return outer ? outer : widget;
}

static GtkWidget *component_parent(Session *s, const char *key) {
    if (s->forced_parent) return s->forced_parent;
    if (!key || !s->components) return NULL;
    GtkWidget *parent = g_hash_table_lookup(s->components, key);
    if (!parent || !GTK_IS_BOX(parent) || g_object_get_data(G_OBJECT(parent), "tarn-ui-tabs") || (g_object_get_data(G_OBJECT(parent), "tarn-ui-list") || g_object_get_data(G_OBJECT(parent), "tarn-ui-tree") || g_object_get_data(G_OBJECT(parent), "tarn-ui-tree-kind"))) return NULL;
    /* GTK 4 unparents a collapsed expander's child: climb from the expander. */
    GtkWidget *anchor = parent;
    unsigned depth = 0;
    for (GtkWidget *host; (host = g_object_get_data(G_OBJECT(anchor), "tarn-ui-host")) != NULL; anchor = host) ++depth;
    if (!gtk_widget_is_ancestor(anchor, s->pages)) return NULL;
    for (GtkWidget *p = anchor; p && p != s->pages; p = gtk_widget_get_parent(p)) ++depth;
    return depth <= 16 ? parent : NULL;
}

int32_t tarn_ui_gtk_add_box(void *raw, const char *parent_key, const char *key, uint32_t horizontal, uint32_t spacing) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    GtkWidget *parent = component_parent(s, parent_key);
    if (!parent || horizontal > 1 || spacing > 64) return 1;
    GtkWidget *box = gtk_box_new(horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, (int)spacing);
    component_track(box);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, box);
    UI_ID(box, owned);
    gtk_box_append(GTK_BOX(parent), box);
    return 0;
}

static int32_t component_add(void *raw, const char *page_key, const char *key, const char *text, uint32_t action, uint32_t style) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    if (!page_key || !s->components || !text || !g_utf8_validate(text, -1, NULL)) return 1;
    GtkWidget *page = component_parent(s, page_key);
    if (!page || style > 2 || (action && style)) return 1;
    GtkWidget *widget;
    if (action) widget = wrapping_button(text);
    else if (style == 1) {
        widget = g_object_new(GTK_TYPE_LABEL, "accessible-role", GTK_ACCESSIBLE_ROLE_HEADING, "label", text, NULL);
        gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LEVEL, 2, -1);
        gtk_widget_add_css_class(widget, "title-2");
    } else {
        widget = gtk_label_new(text);
        if (style == 2) gtk_widget_add_css_class(widget, "title-1");
    }
    component_track(widget);
    if (!action) wrap_label(widget);
    if (gtk_orientable_get_orientation(GTK_ORIENTABLE(page)) == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_hexpand(widget, TRUE);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, widget);
    UI_ID(widget, owned);
    gtk_box_append(GTK_BOX(page), widget);
    if (action) {
        g_object_set_data(G_OBJECT(widget), "tarn-ui-action", GUINT_TO_POINTER(action));
        g_signal_connect(widget, "clicked", G_CALLBACK(component_clicked), s);
#ifdef TARN_UI_TESTING
        ++stats.connected;
#endif
    }
    return 0;
}

int32_t tarn_ui_gtk_add_label(void *raw, const char *page, const char *key, const char *text) {
    return component_add(raw, page, key, text, 0, 0);
}

int32_t tarn_ui_gtk_add_text(void *raw, const char *parent, const char *key, const char *text, uint32_t style) {
    return component_add(raw, parent, key, text, 0, style);
}

int32_t tarn_ui_gtk_add_button(void *raw, const char *page, const char *key, const char *text, uint32_t action) {
    if (!main_thread()) return 2;
    if (!action || action > 1024) return 1;
    return component_add(raw, page, key, text, action, 0);
}

static int32_t component_lookup(void *raw, const char *key, GtkWidget **out) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!key || !s->components) return 1;
    *out = g_hash_table_lookup(s->components, key);
    return *out ? 0 : 1;
}

int32_t tarn_ui_gtk_set_primary(void *raw, const char *key, uint32_t primary) {
    GtkWidget *widget = NULL;
    int32_t status = component_lookup(raw, key, &widget);
    if (status) return status;
    if (!GTK_IS_BUTTON(widget) || primary > 1) return 1;
    if (primary) gtk_widget_add_css_class(widget, "suggested-action");
    else gtk_widget_remove_css_class(widget, "suggested-action");
    return 0;
}

int32_t tarn_ui_gtk_set_accessible(void *raw, const char *key, const char *label, const char *description) {
    GtkWidget *widget = NULL;
    int32_t status = component_lookup(raw, key, &widget);
    if (status) return status;
    if (!label || !*label || !description || !g_utf8_validate(label, -1, NULL) || !g_utf8_validate(description, -1, NULL)) return 1;
    if (g_strcmp0(g_object_get_data(G_OBJECT(widget), "tarn-ui-a11y-label"), label) == 0 &&
        g_strcmp0(g_object_get_data(G_OBJECT(widget), "tarn-ui-a11y-description"), description) == 0) return 0;
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                   GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description, -1);
    /* Explicit adapter properties, not a claim of a computed accessible-name getter. */
    g_object_set_data_full(G_OBJECT(widget), "tarn-ui-a11y-label", g_strdup(label), g_free);
    g_object_set_data_full(G_OBJECT(widget), "tarn-ui-a11y-description", g_strdup(description), g_free);
    return 0;
}

static gboolean can_focus(GtkWidget *widget) {
    return gtk_widget_is_visible(widget) && gtk_widget_get_child_visible(widget) && gtk_widget_is_sensitive(widget);
}

static GtkWidget *first_focusable(GtkWidget *widget) {
    if (!can_focus(widget)) return NULL;
    if (gtk_widget_get_focusable(widget)) return widget;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *target = first_focusable(child);
        if (target) return target;
    }
    return NULL;
}

int32_t tarn_ui_gtk_focus(void *raw, const char *key) {
    GtkWidget *widget = NULL;
    int32_t status = component_lookup(raw, key, &widget);
    if (status) return status;
    Session *s = raw;
    GtkWidget *page = gtk_stack_get_visible_child(GTK_STACK(s->pages));
    if (!can_focus(widget) || !gtk_widget_is_ancestor(widget, page)) return 1;
    return gtk_widget_grab_focus(widget) ? 0 : 1;
}

int32_t tarn_ui_gtk_set_text(void *raw, const char *key, const char *text) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!key || !text || !s->components || !g_utf8_validate(text, -1, NULL)) return 1;
    GtkWidget *widget = g_hash_table_lookup(s->components, key);
#ifdef TARN_UI_TESTING
    ++stats.component_text_calls;
#endif
    if (GTK_IS_LABEL(widget)) {
        if (strcmp(gtk_label_get_text(GTK_LABEL(widget)), text)) gtk_label_set_text(GTK_LABEL(widget), text);
    } else if (GTK_IS_BUTTON(widget)) {
        if (strcmp(gtk_button_get_label(GTK_BUTTON(widget)), text)) gtk_button_set_label(GTK_BUTTON(widget), text);
    } else return 1;
    return 0;
}

int32_t tarn_ui_gtk_show_page(void *raw, const char *key) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!key || !s->pages || !gtk_stack_get_child_by_name(GTK_STACK(s->pages), key)) return 1;
    GtkWidget *page = gtk_stack_get_child_by_name(GTK_STACK(s->pages), key);
#ifdef TARN_UI_TESTING
    ++stats.page_calls;
#endif
    GtkWidget *previous = gtk_stack_get_visible_child(GTK_STACK(s->pages));
    if (previous != page || !s->pages_presented) {
        GtkWidget *focus = gtk_window_get_focus(s->window);
        if (previous && focus && gtk_widget_is_ancestor(focus, previous))
            g_object_set_data(G_OBJECT(previous), "tarn-ui-last-focus", focus);
        gtk_stack_set_visible_child(GTK_STACK(s->pages), page);
        if (previous != page) {
            GtkWidget *scroll = gtk_widget_get_ancestor(s->pages, GTK_TYPE_SCROLLED_WINDOW);
            gtk_adjustment_set_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll)), 0);
        }
        GtkWidget *target = g_object_get_data(G_OBJECT(page), "tarn-ui-last-focus");
        if (!target || !can_focus(target)) target = first_focusable(page);
        if (target) gtk_widget_grab_focus(target);
    }
    if (s->pages_presented) return 0;
    s->pages_presented = TRUE;
#ifdef TARN_UI_DEVTOOLS
    if (!s->dev) return dev_start(s);
#endif
    return 0;
}

#ifdef TARN_UI_TESTING
TestStats test_stats(void) { return stats; }
uint32_t test_counter_updates(void) { return stats.counter_updates; }
uint32_t test_heading_updates(void) { return stats.heading_updates; }
uint32_t test_component_text_calls(void) { return stats.component_text_calls; }
uint32_t test_page_calls(void) { return stats.page_calls; }
uint32_t test_pending_events(void) { return active ? active->pending_increments : 0; }
void test_fail_at(int stage) { fail_stage = stage; }
GtkWindow *test_window(void *raw) {
    return validate(raw) == 0 ? ((Session *)raw)->window : NULL;
}
int32_t test_clean(void) {
    return !active && dev_channels == 0 && stats.allocated == stats.freed &&
        stats.windows == stats.windows_finalized &&
        stats.headings == stats.headings_finalized &&
        stats.counter_widgets == stats.counter_widgets_finalized &&
        stats.components == stats.components_finalized &&
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

int32_t tarn_ui_gtk_add_entry(void *raw, const char *parent_key, const char *key, const char *text, uint32_t action) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    GtkWidget *parent = component_parent(s, parent_key);
    if (!parent || !text || !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 256 || action < 1 || action > 1024) return 1;
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 256);
    gtk_editable_set_text(GTK_EDITABLE(entry), text);
    component_track(entry);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, entry);
    UI_ID(entry, owned);
    g_object_set_data(G_OBJECT(entry), "tarn-ui-action", GUINT_TO_POINTER(action));
    gtk_box_append(GTK_BOX(parent), entry);
    g_signal_connect(entry, "changed", G_CALLBACK(entry_changed), s);
#ifdef TARN_UI_TESTING
    ++stats.connected;
#endif
    return 0;
}

const char *tarn_ui_gtk_event_text(void *raw) {
    if (validate(raw)) return NULL;
    Session *s = raw;
    if (s->closed) return NULL;
    return s->dispatched_text ? s->dispatched_text : "";
}

#ifdef TARN_UI_TESTING
int32_t test_entry_edits(void) {
    if (!main_thread() || !active || !active->components) return 0;
    GtkWidget *entry = g_hash_table_lookup(active->components, "input");
    if (!GTK_IS_ENTRY(entry)) return 0;
    int position = 0;
    gtk_editable_insert_text(GTK_EDITABLE(entry), "Ana", -1, &position);
    gtk_editable_insert_text(GTK_EDITABLE(entry), " María", -1, &position);
    return 1;
}
#endif

int32_t tarn_ui_gtk_set_enabled(void *raw, const char *key, uint32_t enabled) {
    GtkWidget *widget = NULL;
    int32_t status = component_lookup(raw, key, &widget);
    if (status) return status;
    if (enabled > 1) return 1;
    gtk_widget_set_sensitive(widget, enabled != 0);
    return 0;
}

int32_t tarn_ui_gtk_set_input(void *raw, const char *key, const char *text) {
    GtkWidget *widget = NULL;
    int32_t status = component_lookup(raw, key, &widget);
    if (status) return status;
    GtkWidget *state = g_object_get_data(G_OBJECT(widget), "tarn-ui-state");
    if (state) widget = state;
    if (is_control(widget)) {
        int64_t value;
        return text && parse_value(text, &value) ? control_write(widget, raw, value) : 1;
    }
    if (GTK_IS_TEXT_VIEW(widget)) {
        if (!text || !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 4096) return 1;
        text_view_write(widget, raw, text);
        return 0;
    }
    if (!GTK_IS_ENTRY(widget) || !text || !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 256) return 1;
    if (!strcmp(gtk_editable_get_text(GTK_EDITABLE(widget)), text)) return 0;
    /* Model writes must never enqueue a second user edit. */
    g_signal_handlers_block_by_func(widget, entry_changed, raw);
    gtk_editable_set_text(GTK_EDITABLE(widget), text);
    g_signal_handlers_unblock_by_func(widget, entry_changed, raw);
    return 0;
}

/* A dynamic region owns only direct keyed buttons. Other trees stay immutable. */
int32_t tarn_ui_gtk_list_begin(void *raw, const char *parent_key, uint32_t count) {
    GtkWidget *parent = NULL;
    int32_t status = component_lookup(raw, parent_key, &parent);
    if (status) return status;
    Session *s = raw;
    if (s->list_parent || !GTK_IS_BOX(parent) || (g_object_get_data(G_OBJECT(parent), "tarn-ui-tree") || g_object_get_data(G_OBJECT(parent), "tarn-ui-tree-kind")) || count > 256) return 1;
    if (!g_object_get_data(G_OBJECT(parent), "tarn-ui-list") && gtk_widget_get_first_child(parent)) return 1;
    unsigned old_count = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(parent); child; child = gtk_widget_get_next_sibling(child)) ++old_count;
    if (g_hash_table_size(s->components) - old_count + count > 256) return 1;
    g_object_set_data(G_OBJECT(parent), "tarn-ui-list", GINT_TO_POINTER(1));
    s->list_expected = count;
    s->list_parent = parent;
    s->list_previous = NULL;
    s->list_focus_removed = FALSE;
    s->list_seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    return 0;
}

int32_t tarn_ui_gtk_list_item(void *raw, const char *key, const char *text, uint32_t action) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->list_parent || s->tree_mode || !valid_key(key) || !text || !g_utf8_validate(text, -1, NULL) || action < 1 || action > 1024 || g_hash_table_contains(s->list_seen, key)) return 1;
    if (g_hash_table_size(s->list_seen) >= s->list_expected) return 1;
    GtkWidget *widget = g_hash_table_lookup(s->components, key);
    if (widget) {
        if (gtk_widget_get_parent(widget) != s->list_parent || !GTK_IS_BUTTON(widget) ||
            GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-action")) != action) return 1;
        if (strcmp(gtk_button_get_label(GTK_BUTTON(widget)), text)) gtk_button_set_label(GTK_BUTTON(widget), text);
    } else {
        /* Replacement may temporarily hold old and new rows, bounded at 512 keys. */
        if (g_hash_table_size(s->components) >= 512) return 1;
        widget = wrapping_button(text);
        component_track(widget);
        g_hash_table_insert(s->components, g_strdup(key), widget);
#ifdef TARN_UI_DEVTOOLS
        /* Removed widgets may outlive registry membership through GTK references. */
        g_object_set_data_full(G_OBJECT(widget), "tarn-ui-id", g_strdup(key), g_free);
#endif
        g_object_set_data(G_OBJECT(widget), "tarn-ui-action", GUINT_TO_POINTER(action));
        gtk_box_append(GTK_BOX(s->list_parent), widget);
        g_signal_connect(widget, "clicked", G_CALLBACK(component_clicked), s);
#ifdef TARN_UI_TESTING
        ++stats.connected;
#endif
    }
    gtk_box_reorder_child_after(GTK_BOX(s->list_parent), widget, s->list_previous);
    s->list_previous = widget;
    g_hash_table_add(s->list_seen, g_strdup(key));
    return 0;
}

int32_t tarn_ui_gtk_list_end(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->list_parent || s->tree_mode || g_hash_table_size(s->list_seen) != s->list_expected) return 1;
    GtkWidget *child = gtk_widget_get_first_child(s->list_parent);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        /* Registry lookup avoids depending on development-only IDs. */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, s->components);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (value != child || g_hash_table_contains(s->list_seen, key)) continue;
            GtkWidget *focus = gtk_window_get_focus(s->window);
            if (focus == child || (focus && gtk_widget_is_ancestor(focus, child))) s->list_focus_removed = TRUE;
            for (GtkWidget *page = gtk_widget_get_first_child(s->pages); page; page = gtk_widget_get_next_sibling(page)) {
                GtkWidget *saved = g_object_get_data(G_OBJECT(page), "tarn-ui-last-focus");
                if (saved == child || (saved && gtk_widget_is_ancestor(saved, child)))
                    g_object_set_data(G_OBJECT(page), "tarn-ui-last-focus", NULL);
            }
            unsigned disconnected = g_signal_handlers_disconnect_by_func(child, component_clicked, s);
#ifdef TARN_UI_TESTING
            stats.disconnected += disconnected;
#else
            (void)disconnected;
#endif
            gtk_box_remove(GTK_BOX(s->list_parent), child);
            g_hash_table_iter_remove(&iter);
            break;
        }
        child = next;
    }
    if (s->list_focus_removed) {
        GtkWidget *page = gtk_stack_get_visible_child(GTK_STACK(s->pages));
        GtkWidget *target = first_focusable(page);
        if (target) gtk_widget_grab_focus(target);
    }
    s->list_parent = s->list_previous = NULL;
    g_clear_pointer(&s->list_seen, g_hash_table_unref);
    return 0;
}

/* Whole-region traversal: parents precede children; keys cannot move parents or
 * change native type. Native GTK internals never enter the keyed registry. */
/* Orders a reconciled node after its previous sibling and marks it seen. */
static void tree_place(Session *s, GtkWidget *parent, const char *key, GtkWidget *widget) {
    GtkWidget *slot = ui_slot(widget);
    GtkWidget *previous = g_hash_table_lookup(s->tree_last, parent);
    if (gtk_widget_get_prev_sibling(slot) != previous) gtk_box_reorder_child_after(GTK_BOX(parent), slot, previous);
    g_hash_table_insert(s->tree_last, parent, slot);
    g_hash_table_add(s->list_seen, g_strdup(key));
}

int32_t tarn_ui_gtk_tree_begin(void *raw, const char *key, uint32_t count) {
    GtkWidget *root = NULL;
    int32_t status = component_lookup(raw, key, &root);
    if (status) return status;
    Session *s = raw;
    if (s->list_parent || !GTK_IS_BOX(root) || count > 256 || g_object_get_data(G_OBJECT(root), "tarn-ui-list")) return 1;
    if (!g_object_get_data(G_OBJECT(root), "tarn-ui-tree") && gtk_widget_get_first_child(root)) return 1;
    /* A nested owned group cannot become a separately controlled region. */
    if (g_object_get_data(G_OBJECT(root), "tarn-ui-tree-kind")) return 1;
    unsigned old_count = 0;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, s->components);
    while (g_hash_table_iter_next(&iter, NULL, &value))
        if (ui_within(value, root)) ++old_count;
    if (g_hash_table_size(s->components) - old_count + count > 256) return 1;
    g_object_set_data(G_OBJECT(root), "tarn-ui-tree", GINT_TO_POINTER(1));
    s->list_parent = root;
    s->list_expected = count;
    s->list_focus_removed = FALSE;
    s->tree_mode = TRUE;
    s->list_seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    s->tree_last = g_hash_table_new(g_direct_hash, g_direct_equal);
    return 0;
}

int32_t tarn_ui_gtk_tree_node(void *raw, const char *parent_key, const char *key, uint32_t kind,
                             const char *text, uint32_t action, uint32_t spacing) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->tree_mode || !s->list_parent || !valid_key(key) || !text || !g_utf8_validate(text, -1, NULL) || kind > 7 || spacing > 64 ||
        g_hash_table_contains(s->list_seen, key) || g_hash_table_size(s->list_seen) >= s->list_expected) return 1;
    if (kind >= 3 && kind <= 5) { if (action < 1 || action > 1024) return 1; }
    else if (action) return 1;
    if (kind == 5 && g_utf8_strlen(text, -1) > 256) return 1;
    GtkWidget *parent = parent_key ? g_hash_table_lookup(s->components, parent_key) : NULL;
    if (!parent || !GTK_IS_BOX(parent) || (parent != s->list_parent &&
        (!g_hash_table_contains(s->list_seen, parent_key) || !ui_within(parent, s->list_parent)))) return 1;
    unsigned depth = 0;
    for (GtkWidget *p = parent; p != s->list_parent; p = ui_parent(p)) if (++depth > 12) return 1;
    if (kind >= 6 && depth >= 12) return 1;
    GtkWidget *widget = g_hash_table_lookup(s->components, key);
    if (widget) {
        if (gtk_widget_get_parent(widget) != parent || GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-tree-kind")) != kind + 1 ||
            GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-action")) != action) return 1;
    } else {
        if (g_hash_table_size(s->components) >= 512) return 1;
        if (kind >= 6) widget = gtk_box_new(kind == 7 ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, (int)spacing);
        else if (kind == 5) { widget = gtk_entry_new(); gtk_entry_set_max_length(GTK_ENTRY(widget), 256); }
        else if (kind == 3 || kind == 4) widget = wrapping_button(text);
        else if (kind == 1) {
            widget = g_object_new(GTK_TYPE_LABEL, "accessible-role", GTK_ACCESSIBLE_ROLE_HEADING, "label", text, NULL);
            gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LEVEL, 2, -1);
            gtk_widget_add_css_class(widget, "title-2");
        } else widget = gtk_label_new(text);
        if (kind == 2) gtk_widget_add_css_class(widget, "title-1");
        if (kind == 4) gtk_widget_add_css_class(widget, "suggested-action");
        if (kind <= 2) wrap_label(widget);
        component_track(widget);
        g_hash_table_insert(s->components, g_strdup(key), widget);
#ifdef TARN_UI_DEVTOOLS
        g_object_set_data_full(G_OBJECT(widget), "tarn-ui-id", g_strdup(key), g_free);
#endif
        g_object_set_data(G_OBJECT(widget), "tarn-ui-tree-kind", GUINT_TO_POINTER(kind + 1));
        g_object_set_data(G_OBJECT(widget), "tarn-ui-action", GUINT_TO_POINTER(action));
        gtk_box_append(GTK_BOX(parent), widget);
        if (kind >= 3 && kind <= 5) {
            if (kind == 5) g_signal_connect(widget, "changed", G_CALLBACK(entry_changed), s);
            else g_signal_connect(widget, "clicked", G_CALLBACK(component_clicked), s);
#ifdef TARN_UI_TESTING
            ++stats.connected;
#endif
        }
    }
    if (kind >= 6) {
        if (gtk_box_get_spacing(GTK_BOX(widget)) != (int)spacing) gtk_box_set_spacing(GTK_BOX(widget), (int)spacing);
    } else if (kind == 5) {
        status = tarn_ui_gtk_set_input(raw, key, text);
        if (status) return status;
    } else if (kind >= 3) {
        if (strcmp(gtk_button_get_label(GTK_BUTTON(widget)), text)) gtk_button_set_label(GTK_BUTTON(widget), text);
    } else if (strcmp(gtk_label_get_text(GTK_LABEL(widget)), text)) gtk_label_set_text(GTK_LABEL(widget), text);
    tree_place(s, parent, key, widget);
    return 0;
}

static void tree_remove(Session *s, GtkWidget *parent, GtkWidget *child) {
    GtkWidget *focus = gtk_window_get_focus(s->window);
    if (focus == child || (focus && gtk_widget_is_ancestor(focus, child))) s->list_focus_removed = TRUE;
    for (GtkWidget *page = gtk_widget_get_first_child(s->pages); page; page = gtk_widget_get_next_sibling(page)) {
        GtkWidget *saved = g_object_get_data(G_OBJECT(page), "tarn-ui-last-focus");
        if (saved == child || (saved && gtk_widget_is_ancestor(saved, child)))
            g_object_set_data(G_OBJECT(page), "tarn-ui-last-focus", NULL);
    }
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, s->components);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        GtkWidget *widget = value;
        if (widget != child && !ui_within(widget, child)) continue;
        unsigned disconnected = g_signal_handlers_disconnect_by_func(widget, component_clicked, s);
        disconnected += g_signal_handlers_disconnect_by_func(widget, entry_changed, s);
        if (GTK_IS_TEXT_VIEW(widget))
            disconnected += g_signal_handlers_disconnect_by_func(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)), text_changed, s);
        control_disconnect(widget, s, &disconnected);
#ifdef TARN_UI_TESTING
        stats.disconnected += disconnected;
#else
        (void)disconnected;
#endif
        g_hash_table_iter_remove(&iter);
    }
    gtk_box_remove(GTK_BOX(parent), child);
}
static void tree_prune(Session *s, GtkWidget *parent) {
    GtkWidget *child = gtk_widget_get_first_child(parent);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gboolean keep = FALSE;
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, s->components);
        GtkWidget *keyed = NULL;
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (ui_slot(value) == child) { keep = g_hash_table_contains(s->list_seen, key); keyed = value; break; }
        }
        if (!keep) tree_remove(s, parent, child);
        else if (GTK_IS_BOX(keyed) && !g_object_get_data(G_OBJECT(keyed), "tarn-ui-tabs")) tree_prune(s, keyed);
        child = next;
    }
}
int32_t tarn_ui_gtk_tree_end(void *raw) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->tree_mode || !s->list_parent || g_hash_table_size(s->list_seen) != s->list_expected) return 1;
    tree_prune(s, s->list_parent);
    if (s->list_focus_removed) {
        GtkWidget *target = first_focusable(gtk_stack_get_visible_child(GTK_STACK(s->pages)));
        if (target) gtk_widget_grab_focus(target);
    }
    s->list_parent = NULL;
    s->tree_mode = FALSE;
    g_clear_pointer(&s->list_seen, g_hash_table_unref);
    g_clear_pointer(&s->tree_last, g_hash_table_unref);
    return 0;
}

/* Form controls. Each keyed control queues its action with the new value as a
 * decimal event-text snapshot; model writes through set_input never enqueue. */
enum { CONTROL_SWITCH, CONTROL_CHECK, CONTROL_SCALE, CONTROL_SPIN, CONTROL_DROPDOWN,
       CONTROL_PROGRESS, CONTROL_LEVEL, CONTROL_SPINNER, CONTROL_KINDS };

/* Indicators are display-only controls: action 0, no user signal, and the
 * model writes them through set_input like any other control. */
static gboolean kind_is_indicator(uint32_t kind) { return kind >= CONTROL_PROGRESS && kind < CONTROL_KINDS; }
static gboolean is_indicator(GtkWidget *widget) {
    uintptr_t tag = GPOINTER_TO_UINT(widget ? g_object_get_data(G_OBJECT(widget), "tarn-ui-control") : NULL);
    return tag && kind_is_indicator((uint32_t)tag - 1);
}

static gboolean is_control(GtkWidget *widget) {
    return widget && g_object_get_data(G_OBJECT(widget), "tarn-ui-control") != NULL;
}

static int64_t control_value(GtkWidget *widget) {
    if (GTK_IS_EXPANDER(widget)) return gtk_expander_get_expanded(GTK_EXPANDER(widget));
    if (GTK_IS_NOTEBOOK(widget)) return MAX(gtk_notebook_get_current_page(GTK_NOTEBOOK(widget)), 0);
    if (is_indicator(widget)) return (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-value");
    if (GTK_IS_SWITCH(widget)) return gtk_switch_get_active(GTK_SWITCH(widget));
    if (GTK_IS_CHECK_BUTTON(widget)) return gtk_check_button_get_active(GTK_CHECK_BUTTON(widget));
    if (GTK_IS_DROP_DOWN(widget)) return gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
    if (GTK_IS_SPIN_BUTTON(widget)) return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
    return (int64_t)gtk_range_get_value(GTK_RANGE(widget));
}

static void control_enqueue(GtkWidget *widget, Session *s) {
    if (s->closed || s->queue_full) return;
    unsigned slot = (s->increment_head + s->pending_increments) % 64;
    unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-action"));
    int64_t value = control_value(widget);
    enqueue(s, 1000 + (int32_t)action);
    if (!s->queue_full) s->event_texts[slot] = g_strdup_printf("%" PRId64, value);
}
static void control_notified(GObject *object, GParamSpec *pspec, gpointer data) {
    (void)pspec;
    control_enqueue(GTK_WIDGET(object), data);
}
static void control_changed(GtkWidget *widget, gpointer data) { control_enqueue(widget, data); }

static void control_disconnect(GtkWidget *widget, Session *s, unsigned *count) {
    /* A stateful container is keyed by its content box; its state widget
     * (expander or stack) carries the handler. */
    GtkWidget *state = g_object_get_data(G_OBJECT(widget), "tarn-ui-state");
    if (state) control_disconnect(state, s, count);
    *count += g_signal_handlers_disconnect_by_func(widget, control_notified, s);
    *count += g_signal_handlers_disconnect_by_func(widget, control_changed, s);
}

/* Model write without a user event; out-of-range values are rejected. */
static gboolean control_accepts(GtkWidget *widget, int64_t value) {
    int64_t min = (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-min");
    int64_t max = (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-max");
    return value >= min && value <= max;
}
static int32_t control_write(GtkWidget *widget, Session *s, int64_t value) {
    if (!control_accepts(widget, value)) return 1;
    g_signal_handlers_block_by_func(widget, control_notified, s);
    g_signal_handlers_block_by_func(widget, control_changed, s);
    if (is_indicator(widget)) {
        int64_t min = (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-min");
        int64_t max = (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-max");
        g_object_set_data(G_OBJECT(widget), "tarn-ui-value", (gpointer)(intptr_t)value);
        if (GTK_IS_PROGRESS_BAR(widget)) {
            double fraction = max > min ? (double)(value - min) / (double)(max - min) : 0;
            char *percent = g_strdup_printf("%d %%", (int)(fraction * 100 + 0.5));
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widget), fraction);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(widget), percent);
            g_free(percent);
        } else if (GTK_IS_LEVEL_BAR(widget)) {
            gtk_level_bar_set_value(GTK_LEVEL_BAR(widget), (double)value);
        } else {
            gtk_spinner_set_spinning(GTK_SPINNER(widget), value != 0);
        }
        return 0;
    }
    if (GTK_IS_EXPANDER(widget)) gtk_expander_set_expanded(GTK_EXPANDER(widget), value != 0);
    else if (GTK_IS_NOTEBOOK(widget)) gtk_notebook_set_current_page(GTK_NOTEBOOK(widget), (int)value);
    else if (GTK_IS_SWITCH(widget)) gtk_switch_set_active(GTK_SWITCH(widget), value != 0);
    else if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_active(GTK_CHECK_BUTTON(widget), value != 0);
    else if (GTK_IS_DROP_DOWN(widget)) gtk_drop_down_set_selected(GTK_DROP_DOWN(widget), (guint)value);
    else if (GTK_IS_SPIN_BUTTON(widget)) gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), (double)value);
    else gtk_range_set_value(GTK_RANGE(widget), (double)value);
    g_signal_handlers_unblock_by_func(widget, control_notified, s);
    g_signal_handlers_unblock_by_func(widget, control_changed, s);
    return 0;
}

static gboolean parse_value(const char *text, int64_t *out) {
    if (!text || !*text) return FALSE;
    char *end = NULL;
    gint64 value = g_ascii_strtoll(text, &end, 10);
    if (*end || (value == 0 && strcmp(text, "0") && strcmp(text, "-0"))) return FALSE;
    *out = value;
    return TRUE;
}

int32_t tarn_ui_gtk_add_control(void *raw, const char *parent_key, const char *key, uint32_t kind,
                                const char *text, uint32_t action, int64_t value, int64_t min, int64_t max) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    GtkWidget *parent = component_parent(s, parent_key);
    gboolean indicator = kind_is_indicator(kind);
    if (!parent || kind >= CONTROL_KINDS || (indicator ? action != 0 : (action < 1 || action > 1024)) || !text ||
        !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 256) return 1;
    if (kind <= CONTROL_CHECK || kind == CONTROL_SPINNER) { min = 0; max = 1; }
    char **lines = NULL;
    const char *caption_text = text;
    if (kind == CONTROL_DROPDOWN) {
        /* First line is the caption; then 1..64 non-empty options. */
        lines = g_strsplit(text, "\n", 66);
        guint count = g_strv_length(lines);
        gboolean valid = count >= 2 && count <= 65;
        for (guint i = 1; valid && i < count; ++i) valid = lines[i][0] != 0;
        if (!valid) { g_strfreev(lines); return 1; }
        caption_text = lines[0];
        min = 0;
        max = count - 2;
    } else if (strchr(text, '\n')) return 1;
    if (min > max || min < -1000000000 || max > 1000000000 || value < min || value > max) {
        g_strfreev(lines);
        return 1;
    }
    GtkWidget *widget, *outer;
    if (kind == CONTROL_CHECK) {
        widget = gtk_check_button_new_with_label(text);
        outer = widget;
    } else {
        if (kind == CONTROL_SWITCH) {
            widget = gtk_switch_new();
            gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
        } else if (kind == CONTROL_SCALE) {
            widget = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, (double)min, (double)max, 1);
            gtk_scale_set_digits(GTK_SCALE(widget), 0);
            gtk_scale_set_draw_value(GTK_SCALE(widget), TRUE);
            gtk_widget_set_hexpand(widget, TRUE);
        } else if (kind == CONTROL_SPIN) {
            widget = gtk_spin_button_new_with_range((double)min, (double)max, 1);
        } else if (kind == CONTROL_PROGRESS) {
            widget = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(widget), TRUE);
            gtk_widget_set_hexpand(widget, TRUE);
        } else if (kind == CONTROL_LEVEL) {
            widget = gtk_level_bar_new_for_interval((double)min, (double)max);
            gtk_level_bar_set_mode(GTK_LEVEL_BAR(widget), GTK_LEVEL_BAR_MODE_CONTINUOUS);
            gtk_widget_set_hexpand(widget, TRUE);
            gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
        } else if (kind == CONTROL_SPINNER) {
            widget = gtk_spinner_new();
        } else {
            widget = gtk_drop_down_new_from_strings((const char *const *)lines + 1);
        }
        /* A visible caption, also the control's accessible label. A slider needs
         * the full width, so its caption sits above it. */
        gboolean stacked = kind == CONTROL_SCALE || kind == CONTROL_PROGRESS;
        outer = gtk_box_new(stacked ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, stacked ? 6 : 12);
        GtkWidget *caption = gtk_label_new(caption_text);
        wrap_label(caption);
        gtk_label_set_xalign(GTK_LABEL(caption), 0);
        gtk_widget_set_hexpand(caption, TRUE);
        gtk_box_append(GTK_BOX(outer), caption);
        gtk_box_append(GTK_BOX(outer), widget);
        gtk_accessible_update_relation(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_RELATION_LABELLED_BY, caption, NULL, -1);
        component_track(outer);
    }
    g_strfreev(lines);
    component_track(widget);
    if (outer != widget) g_object_set_data(G_OBJECT(widget), "tarn-ui-outer", outer);
    g_object_set_data(G_OBJECT(widget), "tarn-ui-control", GUINT_TO_POINTER(kind + 1));
    g_object_set_data(G_OBJECT(widget), "tarn-ui-action", GUINT_TO_POINTER(action));
    g_object_set_data(G_OBJECT(widget), "tarn-ui-min", (gpointer)(intptr_t)min);
    g_object_set_data(G_OBJECT(widget), "tarn-ui-max", (gpointer)(intptr_t)max);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, widget);
    UI_ID(widget, owned);
    gtk_box_append(GTK_BOX(parent), outer);
    control_write(widget, s, value);
    if (indicator) return 0;
    if (kind == CONTROL_SWITCH) g_signal_connect(widget, "notify::active", G_CALLBACK(control_notified), s);
    else if (kind == CONTROL_CHECK) g_signal_connect(widget, "toggled", G_CALLBACK(control_changed), s);
    else if (kind == CONTROL_DROPDOWN) g_signal_connect(widget, "notify::selected", G_CALLBACK(control_notified), s);
    else g_signal_connect(widget, "value-changed", G_CALLBACK(control_changed), s);
#ifdef TARN_UI_TESTING
    ++stats.connected;
#endif
    return 0;
}

/* Containers. A frame groups its children under a title. An expander shows
 * them on demand; open/closed (0/1) is model state like a control. Tabs host
 * only tab containers; the selected index is model state. Each container is
 * keyed by its content box, so children use the container key as parent. A
 * stateful container with action 0 never queues events. */
enum { CONTAINER_FRAME, CONTAINER_EXPANDER, CONTAINER_TABS, CONTAINER_TAB, CONTAINER_KINDS };

static GtkWidget *content_box(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    return box;
}

static void state_tag(GtkWidget *state, GtkWidget *box, uint32_t action, int64_t max, Session *s, const char *signal) {
    g_object_set_data(G_OBJECT(state), "tarn-ui-control", GUINT_TO_POINTER(CONTROL_KINDS + 1));
    g_object_set_data(G_OBJECT(state), "tarn-ui-action", GUINT_TO_POINTER(action));
    g_object_set_data(G_OBJECT(state), "tarn-ui-min", (gpointer)(intptr_t)0);
    g_object_set_data(G_OBJECT(state), "tarn-ui-max", (gpointer)(intptr_t)max);
    g_object_set_data(G_OBJECT(box), "tarn-ui-state", state);
    if (action) {
        g_signal_connect(state, signal, G_CALLBACK(control_notified), s);
#ifdef TARN_UI_TESTING
        ++stats.connected;
#endif
    }
}

int32_t tarn_ui_gtk_add_container(void *raw, const char *parent_key, const char *key, uint32_t kind,
                                  const char *title, uint32_t action, int64_t value) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    if (kind >= CONTAINER_KINDS || action > 1024 || !title || !g_utf8_validate(title, -1, NULL) ||
        g_utf8_strlen(title, -1) > 128 || strchr(title, '\n')) return 1;
    GtkWidget *parent = NULL, *stack = NULL;
    if (kind == CONTAINER_TAB) {
        /* A tab's parent is a tabs container, which accepts nothing else. */
        parent = parent_key && s->components ? g_hash_table_lookup(s->components, parent_key) : NULL;
        stack = parent ? g_object_get_data(G_OBJECT(parent), "tarn-ui-tabs") : NULL; /* the notebook */
        if (!stack || !title[0] || action || value) return 1;
    } else {
        parent = component_parent(s, parent_key);
        if (!parent) return 1;
    }
    if ((kind == CONTAINER_FRAME && (action || value)) || (kind == CONTAINER_EXPANDER && (value < 0 || value > 1)) ||
        (kind == CONTAINER_TABS && (value < 0 || value > 31 || title[0])) || (kind == CONTAINER_EXPANDER && !title[0])) return 1;
    GtkWidget *box, *outer, *ided;
    if (kind == CONTAINER_FRAME) {
        box = content_box();
        outer = gtk_frame_new(title[0] ? title : NULL);
        gtk_frame_set_child(GTK_FRAME(outer), box);
        ided = box;
    } else if (kind == CONTAINER_EXPANDER) {
        box = content_box();
        outer = gtk_expander_new(title);
        gtk_expander_set_child(GTK_EXPANDER(outer), box);
        g_object_set_data(G_OBJECT(box), "tarn-ui-host", outer);
        gtk_expander_set_expanded(GTK_EXPANDER(outer), value != 0);
        state_tag(outer, box, action, 1, s, "notify::expanded");
        ided = outer;
    } else if (kind == CONTAINER_TABS) {
        /* The notebook sits in a box so the container key has a box like the
         * others; component_parent refuses that box for anything but tabs. */
        box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget *tabs = gtk_notebook_new();
        gtk_box_append(GTK_BOX(box), tabs);
        g_object_set_data(G_OBJECT(box), "tarn-ui-tabs", tabs);
        g_object_set_data(G_OBJECT(tabs), "tarn-ui-wanted", (gpointer)(intptr_t)value);
        state_tag(tabs, box, action, -1, s, "notify::page");
        outer = box;
        ided = tabs;
    } else {
        box = content_box();
        int64_t index = (int64_t)(intptr_t)g_object_get_data(G_OBJECT(stack), "tarn-ui-max") + 1;
        if (index >= 32) return 1;
        /* Adding a page may change the visible child: never report it. */
        g_signal_handlers_block_by_func(stack, control_notified, s);
        gtk_notebook_append_page(GTK_NOTEBOOK(stack), box, gtk_label_new(title));
        g_object_set_data(G_OBJECT(stack), "tarn-ui-max", (gpointer)(intptr_t)index);
        if ((int64_t)(intptr_t)g_object_get_data(G_OBJECT(stack), "tarn-ui-wanted") == index)
            gtk_notebook_set_current_page(GTK_NOTEBOOK(stack), (int)index);
        g_signal_handlers_unblock_by_func(stack, control_notified, s);
        outer = NULL;
        ided = box;
    }
    component_track(box);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, box);
    UI_ID(ided, owned);
    (void)ided; /* UI_ID is empty outside development builds */
    if (outer) {
        if (outer != box) {
            component_track(outer);
            g_object_set_data(G_OBJECT(box), "tarn-ui-outer", outer);
        }
        gtk_box_append(GTK_BOX(parent), outer);
    }
    return 0;
}

/* Multiline text: a GtkTextView in a scrolled frame. Like an entry, each user
 * edit queues the action with a full snapshot (at most 4096 characters);
 * model writes through set_input never queue. */
static char *text_view_text(GtkWidget *view) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void text_changed(GtkTextBuffer *buffer, gpointer data) {
    Session *s = data;
    if (s->closed || s->queue_full) return;
    GtkWidget *view = g_object_get_data(G_OBJECT(buffer), "tarn-ui-view");
    char *text = text_view_text(view);
    if (g_utf8_strlen(text, -1) > 4096) {
        /* Over the limit: keep the last accepted snapshot in the widget. */
        const char *previous = g_object_get_data(G_OBJECT(view), "tarn-ui-last");
        g_free(text);
        text_view_write(view, s, previous ? previous : "");
        return;
    }
    g_object_set_data_full(G_OBJECT(view), "tarn-ui-last", g_strdup(text), g_free);
    unsigned slot = (s->increment_head + s->pending_increments) % 64;
    unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view), "tarn-ui-action"));
    enqueue(s, 1000 + (int32_t)action);
    if (!s->queue_full) s->event_texts[slot] = text;
    else g_free(text);
}

static void text_view_write(GtkWidget *view, Session *s, const char *text) {
    char *current = text_view_text(view);
    gboolean same = !strcmp(current, text);
    g_free(current);
    if (same) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    g_signal_handlers_block_by_func(buffer, text_changed, s);
    gtk_text_buffer_set_text(buffer, text, -1);
    g_signal_handlers_unblock_by_func(buffer, text_changed, s);
    g_object_set_data_full(G_OBJECT(view), "tarn-ui-last", g_strdup(text), g_free);
}

int32_t tarn_ui_gtk_add_text_area(void *raw, const char *parent_key, const char *key, const char *text,
                                  uint32_t action, uint32_t lines) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    GtkWidget *parent = component_parent(s, parent_key);
    if (!parent || !text || !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 4096 ||
        action < 1 || action > 1024 || lines < 2 || lines > 20) return 1;
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE); /* Tab keeps moving focus */
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 6);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buffer, text, -1);
    g_object_set_data_full(G_OBJECT(view), "tarn-ui-last", g_strdup(text), g_free);
    g_object_set_data(G_OBJECT(buffer), "tarn-ui-view", view);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scroll), TRUE);
    /* Line height from the theme font: about 1.5 em per line. */
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), (int)lines * 22);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    component_track(view);
    component_track(scroll);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, view);
    UI_ID(view, owned);
    g_object_set_data(G_OBJECT(view), "tarn-ui-action", GUINT_TO_POINTER(action));
    g_object_set_data(G_OBJECT(view), "tarn-ui-outer", scroll);
    gtk_box_append(GTK_BOX(parent), scroll);
    g_signal_connect(buffer, "changed", G_CALLBACK(text_changed), s);
#ifdef TARN_UI_TESTING
    ++stats.connected;
#endif
    return 0;
}

/* Header bar and its menu. The header replaces the window title bar; its
 * buttons are keyed components (set_text/set_enabled work on them). Menu
 * entries are window actions "tarn.<key>" shown in a primary menu button;
 * activating one queues 1000 + action like a button. */
static void menu_activated(GSimpleAction *item, GVariant *parameter, gpointer data) {
    (void)parameter;
    unsigned action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "tarn-ui-action"));
    enqueue(data, 1000 + (int32_t)action);
}

static GtkWidget *header_of(Session *s) {
    GtkWidget *header = gtk_window_get_titlebar(s->window);
    return GTK_IS_HEADER_BAR(header) ? header : NULL;
}

int32_t tarn_ui_gtk_set_header(void *raw, const char *subtitle) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (!subtitle || !g_utf8_validate(subtitle, -1, NULL) || g_utf8_strlen(subtitle, -1) > 128 || strchr(subtitle, '\n')) return 1;
    GtkWidget *header = header_of(s);
    if (!header) {
        header = gtk_header_bar_new();
        /* GTK only accepts a title bar on an unrealized window: create presents
         * it, so take it down briefly. Apps call this while building their UI,
         * before the first event, so the user sees no flicker. */
        gboolean visible = gtk_widget_get_visible(GTK_WIDGET(s->window));
        gtk_widget_set_visible(GTK_WIDGET(s->window), FALSE);
        gtk_widget_unrealize(GTK_WIDGET(s->window));
        gtk_window_set_titlebar(s->window, header);
        if (visible) gtk_window_present(s->window);
    }
    GtkWidget *title = NULL;
    if (subtitle[0]) {
        /* Title and subtitle stacked, as in GNOME's window title widget. */
        title = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
        GtkWidget *main_title = gtk_label_new(gtk_window_get_title(s->window));
        gtk_widget_add_css_class(main_title, "title");
        GtkWidget *sub = gtk_label_new(subtitle);
        gtk_widget_add_css_class(sub, "subtitle");
        gtk_box_append(GTK_BOX(title), main_title);
        gtk_box_append(GTK_BOX(title), sub);
    }
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
    return 0;
}

int32_t tarn_ui_gtk_add_header_button(void *raw, const char *key, const char *text, const char *icon,
                                      uint32_t action, uint32_t end) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    status = component_admit(s, key);
    if (status) return status;
    GtkWidget *header = header_of(s);
    if (!header || !s->components || !text || !icon || !g_utf8_validate(text, -1, NULL) || !text[0] || g_utf8_strlen(text, -1) > 64 ||
        strchr(text, '\n') || !g_utf8_validate(icon, -1, NULL) || strlen(icon) > 128 || action < 1 || action > 1024 || end > 1) return 1;
    GtkWidget *button;
    if (icon[0]) {
        /* Icon-only buttons keep the text as tooltip and accessible name. */
        button = gtk_button_new_from_icon_name(icon);
        gtk_widget_set_tooltip_text(button, text);
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, text, -1);
    } else button = gtk_button_new_with_label(text);
    /* Some themes (Breeze) align header children by baseline, which icon
     * buttons cannot report; centering avoids GTK's baseline warning. */
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    component_track(button);
    char *owned = g_strdup(key);
    g_hash_table_insert(s->components, owned, button);
    UI_ID(button, owned);
    g_object_set_data(G_OBJECT(button), "tarn-ui-action", GUINT_TO_POINTER(action));
    if (end) gtk_header_bar_pack_end(GTK_HEADER_BAR(header), button);
    else gtk_header_bar_pack_start(GTK_HEADER_BAR(header), button);
    g_signal_connect(button, "clicked", G_CALLBACK(component_clicked), s);
#ifdef TARN_UI_TESTING
    ++stats.connected;
#endif
    return 0;
}

int32_t tarn_ui_gtk_add_menu_item(void *raw, const char *key, const char *text, uint32_t action) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    GtkWidget *header = header_of(s);
    if (!header || !key || !text || !g_utf8_validate(text, -1, NULL) || !text[0] || g_utf8_strlen(text, -1) > 64 ||
        strchr(text, '\n') || action < 1 || action > 1024) return 1;
    /* Keys become action names: ASCII letters, digits and '-' only. */
    size_t length = strlen(key);
    if (!length || length > 64) return 1;
    for (size_t i = 0; i < length; ++i) if (!g_ascii_isalnum(key[i]) && key[i] != '-') return 1;
    GSimpleActionGroup *group = g_object_get_data(G_OBJECT(s->window), "tarn-ui-menu-actions");
    GMenu *menu = g_object_get_data(G_OBJECT(s->window), "tarn-ui-menu");
    if (!group) {
        group = g_simple_action_group_new();
        menu = g_menu_new();
        gtk_widget_insert_action_group(GTK_WIDGET(s->window), "tarn", G_ACTION_GROUP(group));
        g_object_set_data_full(G_OBJECT(s->window), "tarn-ui-menu-actions", group, g_object_unref);
        g_object_set_data_full(G_OBJECT(s->window), "tarn-ui-menu", menu, g_object_unref);
        GtkWidget *button = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
        gtk_menu_button_set_primary(GTK_MENU_BUTTON(button), TRUE);
        gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
        gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
        gtk_widget_set_tooltip_text(button, "Menú principal");
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, "Menú principal", -1);
        UI_ID(button, "menu");
        gtk_header_bar_pack_end(GTK_HEADER_BAR(header), button);
    }
    if (g_action_map_lookup_action(G_ACTION_MAP(group), key)) return 1;
    if (g_menu_model_get_n_items(G_MENU_MODEL(menu)) >= 32) return 1;
    GSimpleAction *item = g_simple_action_new(key, NULL);
    g_object_set_data(G_OBJECT(item), "tarn-ui-action", GUINT_TO_POINTER(action));
    g_signal_connect(item, "activate", G_CALLBACK(menu_activated), s);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(item));
    g_object_unref(item);
    char *detailed = g_strconcat("tarn.", key, NULL);
    g_menu_append(menu, text, detailed);
    g_free(detailed);
    return 0;
}

#ifdef TARN_UI_DEVTOOLS
/* Menu entries are not widgets until the popover opens; tests activate them
 * by key through the same action the menu uses. */
static gboolean menu_activate_key(Session *s, const char *key) {
    GSimpleActionGroup *group = g_object_get_data(G_OBJECT(s->window), "tarn-ui-menu-actions");
    GAction *item = group && key ? g_action_map_lookup_action(G_ACTION_MAP(group), key) : NULL;
    if (!item || !g_action_get_enabled(item)) return FALSE;
    g_action_activate(item, NULL);
    return TRUE;
}
#endif

/* Region reconciliation for controls, containers and multiline text. A new
 * key is built by the static creator under the region parent; an existing key
 * must keep its family, kind, parent and action, and gets its text and value
 * written without queuing events. Tabs stay static-only. */
enum { TREE_CONTROL = 100, TREE_CONTAINER = 200, TREE_AREA = 300 };

static int32_t tree_prepare(void *raw, const char *parent_key, const char *key, uint32_t family,
                            GtkWidget **parent_out, GtkWidget **existing) {
    int32_t status = validate(raw);
    if (status) return status;
    Session *s = raw;
    if (s->closed) return 6;
    if (s->queue_full) return 8;
    if (!s->tree_mode || !s->list_parent || !valid_key(key) || g_hash_table_contains(s->list_seen, key) ||
        g_hash_table_size(s->list_seen) >= s->list_expected) return 1;
    GtkWidget *parent = parent_key ? g_hash_table_lookup(s->components, parent_key) : NULL;
    if (!parent || !GTK_IS_BOX(parent) || g_object_get_data(G_OBJECT(parent), "tarn-ui-tabs") || (parent != s->list_parent &&
        (!g_hash_table_contains(s->list_seen, parent_key) || !ui_within(parent, s->list_parent)))) return 1;
    unsigned depth = 0;
    for (GtkWidget *p = parent; p != s->list_parent; p = ui_parent(p)) if (++depth > 12) return 1;
    GtkWidget *widget = g_hash_table_lookup(s->components, key);
    if (widget) {
        guint tag = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-tree-kind"));
        if (gtk_widget_get_parent(ui_slot(widget)) != parent || tag / 100 * 100 != family) return 1;
    } else if (g_hash_table_size(s->components) >= 512) return 1;
    *parent_out = parent;
    *existing = widget;
    return 0;
}

static int32_t tree_create(Session *s, GtkWidget *parent, const char *key, guint tag, int32_t status) {
    s->forced_parent = NULL;
    if (status) return status;
    GtkWidget *widget = g_hash_table_lookup(s->components, key);
    g_object_set_data(G_OBJECT(widget), "tarn-ui-tree-kind", GUINT_TO_POINTER(tag));
#ifdef TARN_UI_DEVTOOLS
    /* Region keys are freed with the tree; give the id its own copy. */
    GtkWidget *ided = g_object_get_data(G_OBJECT(widget), "tarn-ui-state");
    if (!ided || !g_object_get_data(G_OBJECT(ided), "tarn-ui-id")) ided = widget;
    g_object_set_data_full(G_OBJECT(ided), "tarn-ui-id", g_strdup(key), g_free);
#endif
    tree_place(s, parent, key, widget);
    return 0;
}

int32_t tarn_ui_gtk_tree_control(void *raw, const char *parent_key, const char *key, uint32_t kind, const char *text,
                                 uint32_t action, int64_t value, int64_t min, int64_t max) {
    GtkWidget *parent = NULL, *widget = NULL;
    int32_t status = tree_prepare(raw, parent_key, key, TREE_CONTROL, &parent, &widget);
    if (status) return status;
    Session *s = raw;
    if (!widget) {
        s->forced_parent = parent;
        return tree_create(s, parent, key, TREE_CONTROL + kind,
                           tarn_ui_gtk_add_control(raw, parent_key, key, kind, text, action, value, min, max));
    }
    guint tag = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-tree-kind"));
    if (tag != TREE_CONTROL + kind || GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "tarn-ui-action")) != action) return 1;
    /* Range and options are fixed per key; the caption is not re-read. */
    if (kind != CONTROL_DROPDOWN && kind > CONTROL_CHECK && kind != CONTROL_SPINNER &&
        ((int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-min") != min ||
         (int64_t)(intptr_t)g_object_get_data(G_OBJECT(widget), "tarn-ui-max") != max)) return 1;
    status = control_write(widget, s, value);
    if (status) return status;
    tree_place(s, parent, key, widget);
    return 0;
}

int32_t tarn_ui_gtk_tree_container(void *raw, const char *parent_key, const char *key, uint32_t kind,
                                   const char *title, uint32_t action, int64_t value) {
    if (kind != CONTAINER_FRAME && kind != CONTAINER_EXPANDER) return 1;
    GtkWidget *parent = NULL, *box = NULL;
    int32_t status = tree_prepare(raw, parent_key, key, TREE_CONTAINER, &parent, &box);
    if (status) return status;
    Session *s = raw;
    if (!box) {
        s->forced_parent = parent;
        return tree_create(s, parent, key, TREE_CONTAINER + kind,
                           tarn_ui_gtk_add_container(raw, parent_key, key, kind, title, action, value));
    }
    GtkWidget *outer = ui_slot(box);
    GtkWidget *state = g_object_get_data(G_OBJECT(box), "tarn-ui-state");
    guint tag = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(box), "tarn-ui-tree-kind"));
    if (tag != TREE_CONTAINER + kind || !title || !g_utf8_validate(title, -1, NULL) || g_utf8_strlen(title, -1) > 128 ||
        strchr(title, '\n') || (state && GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(state), "tarn-ui-action")) != action)) return 1;
    if (kind == CONTAINER_FRAME) {
        if (action || value) return 1;
        if (g_strcmp0(gtk_frame_get_label(GTK_FRAME(outer)), title[0] ? title : NULL))
            gtk_frame_set_label(GTK_FRAME(outer), title[0] ? title : NULL);
    } else {
        if (!title[0] || value < 0 || value > 1) return 1;
        if (strcmp(gtk_expander_get_label(GTK_EXPANDER(outer)), title)) gtk_expander_set_label(GTK_EXPANDER(outer), title);
        status = control_write(state, s, value);
        if (status) return status;
    }
    tree_place(s, parent, key, box);
    return 0;
}

int32_t tarn_ui_gtk_tree_text_area(void *raw, const char *parent_key, const char *key, const char *text,
                                   uint32_t action, uint32_t lines) {
    GtkWidget *parent = NULL, *view = NULL;
    int32_t status = tree_prepare(raw, parent_key, key, TREE_AREA, &parent, &view);
    if (status) return status;
    Session *s = raw;
    if (!view) {
        s->forced_parent = parent;
        return tree_create(s, parent, key, TREE_AREA, tarn_ui_gtk_add_text_area(raw, parent_key, key, text, action, lines));
    }
    if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(view), "tarn-ui-action")) != action || !text ||
        !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 4096) return 1;
    text_view_write(view, s, text);
    tree_place(s, parent, key, view);
    return 0;
}
