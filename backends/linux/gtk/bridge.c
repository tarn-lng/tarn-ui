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
    GtkWidget *heading, *counter_label, *button; /* borrowed from the window's child tree */
    GtkWidget *pages;
    GHashTable *components; /* per-session borrowed widgets; the native tree owns them */
    unsigned page_count;
    GtkWidget *list_parent, *list_previous;
    GHashTable *list_seen;
    gboolean list_focus_removed;
    unsigned list_expected;
    gboolean tree_mode;
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

uint32_t tarn_ui_gtk_abi(void) { return 12; }

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

static GtkWidget *component_parent(Session *s, const char *key) {
    if (!key || !s->components) return NULL;
    GtkWidget *parent = g_hash_table_lookup(s->components, key);
    if (!parent || !GTK_IS_BOX(parent) || (g_object_get_data(G_OBJECT(parent), "tarn-ui-list") || g_object_get_data(G_OBJECT(parent), "tarn-ui-tree") || g_object_get_data(G_OBJECT(parent), "tarn-ui-tree-kind")) || !gtk_widget_is_ancestor(parent, s->pages)) return NULL;
    unsigned depth = 0;
    for (GtkWidget *p = parent; p && p != s->pages; p = gtk_widget_get_parent(p)) ++depth;
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
        if (gtk_widget_is_ancestor(value, root)) ++old_count;
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
        (!g_hash_table_contains(s->list_seen, parent_key) || !gtk_widget_is_ancestor(parent, s->list_parent)))) return 1;
    unsigned depth = 0;
    for (GtkWidget *p = parent; p != s->list_parent; p = gtk_widget_get_parent(p)) if (++depth > 12) return 1;
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
    GtkWidget *previous = g_hash_table_lookup(s->tree_last, parent);
    if (gtk_widget_get_prev_sibling(widget) != previous) gtk_box_reorder_child_after(GTK_BOX(parent), widget, previous);
    g_hash_table_insert(s->tree_last, parent, widget);
    g_hash_table_add(s->list_seen, g_strdup(key));
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
        if (widget != child && !gtk_widget_is_ancestor(widget, child)) continue;
        unsigned disconnected = g_signal_handlers_disconnect_by_func(widget, component_clicked, s);
        disconnected += g_signal_handlers_disconnect_by_func(widget, entry_changed, s);
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
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            if (value == child) { keep = g_hash_table_contains(s->list_seen, key); break; }
        }
        if (!keep) tree_remove(s, parent, child);
        else if (GTK_IS_BOX(child)) tree_prune(s, child);
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
