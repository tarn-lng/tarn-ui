/* Private development-only implementation; included by bridge.c.
 * No listener, Tarn closure callback, counter state or production transport. */
#include <json-glib/json-glib.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define DEV_RECORD_LIMIT (1024 * 1024)
#define DEV_QUEUE_LIMIT 32
struct DevChannel {
    int fd;
    guint reader, writer, timer;
    GString *input, *output;
    unsigned lines;
    gboolean failed, ready, authorized, allow_text, timed_out;
    gboolean inspect, click, close, closed;
    const char *terminal;
    char *session_id, *pending_id, *close_id;
    uint64_t revision, sequence, pending_token;
    gint64 deadline;
    GHashTable *request_ids;
};

static JsonObject *dev_object(void) { return json_object_new(); }
static void dev_string(JsonObject *o, const char *k, const char *v) {
    if (v) json_object_set_string_member(o, k, v);
    else json_object_set_null_member(o, k);
}
static void dev_int(JsonObject *o, const char *k, gint64 v) { json_object_set_int_member(o, k, v); }
static void dev_bool(JsonObject *o, const char *k, gboolean v) { json_object_set_boolean_member(o, k, v); }
static gboolean dev_flush(DevChannel *d);
static gboolean dev_writable(gint fd, GIOCondition condition, gpointer data) {
    (void)fd; (void)condition;
    DevChannel *d = data;
    if (dev_flush(d)) { d->writer = 0; return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}
static gboolean dev_flush(DevChannel *d) {
    while (d->output->len) {
        ssize_t n = send(d->fd, d->output->str, d->output->len, MSG_NOSIGNAL);
        if (n > 0) g_string_erase(d->output, 0, n);
        else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return FALSE;
        else { d->failed = TRUE; return TRUE; }
    }
    return TRUE;
}
static void dev_wire(DevChannel *d, JsonObject *object) {
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, object);
    char *text = json_to_string(node, FALSE);
    if (strlen(text) + d->output->len > 2 * DEV_RECORD_LIMIT) d->failed = TRUE;
    else { g_string_append(d->output, text); g_string_append_c(d->output, '\n'); }
    g_free(text);
    json_node_free(node);
    if (!d->failed && !dev_flush(d) && !d->writer)
        d->writer = g_unix_fd_add(d->fd, G_IO_OUT | G_IO_ERR | G_IO_HUP, dev_writable, d);
}
static JsonObject *dev_result(DevChannel *d) {
    JsonObject *o = dev_object();
    dev_string(o, "session_id", d->session_id);
    dev_int(o, "revision", d->revision);
    return o;
}
static void dev_reply(DevChannel *d, const char *id, JsonObject *result, const char *error) {
    JsonObject *o = dev_object();
    dev_int(o, "version", 1);
    dev_string(o, "request_id", id);
    if (error) {
        JsonObject *e = dev_object();
        dev_string(e, "code", error);
        dev_string(e, "message", error);
        json_object_set_object_member(o, "error", e);
    } else json_object_set_object_member(o, "result", result);
    dev_wire(d, o);
}
static gboolean dev_readable(gint fd, GIOCondition condition, gpointer data) {
    (void)condition;
    DevChannel *d = data;
    char buffer[8192];
    for (int batch = 0; batch < 8; ++batch) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            g_string_append_len(d->input, buffer, n);
            for (ssize_t i = 0; i < n; ++i) if (buffer[i] == '\n') ++d->lines;
            if (d->lines > DEV_QUEUE_LIMIT) d->terminal = "QUEUE_FULL";
            if (d->input->len > DEV_RECORD_LIMIT) d->terminal = "INVALID_REQUEST";
            if (d->terminal) { d->reader = 0; return G_SOURCE_REMOVE; }
        } else if (n < 0 && errno == EINTR) continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return G_SOURCE_CONTINUE;
        else { d->failed = TRUE; d->reader = 0; return G_SOURCE_REMOVE; }
    }
    return G_SOURCE_CONTINUE;
}
static gboolean dev_timeout(gpointer data) {
    DevChannel *d = data;
    d->timed_out = TRUE;
    d->timer = 0;
    return G_SOURCE_REMOVE;
}
static void dev_clear_pending(DevChannel *d) {
    if (d->timer) g_source_remove(d->timer);
    d->timer = 0;
    g_clear_pointer(&d->pending_id, g_free);
    d->pending_token = 0;
}
static void dev_free(Session *s) {
    DevChannel *d = s->dev;
    if (!d) return;
    if (d->reader) g_source_remove(d->reader);
    if (d->writer) g_source_remove(d->writer);
    dev_clear_pending(d);
    close(d->fd);
    g_string_free(d->input, TRUE);
    g_string_free(d->output, TRUE);
    g_free(d->session_id);
    g_free(d->close_id);
    g_hash_table_unref(d->request_ids);
    g_free(d);
    s->dev = NULL;
#ifdef TARN_UI_TESTING
    --dev_channels;
#endif
}
static const char *dev_id(GtkWidget *widget) { return g_object_get_data(G_OBJECT(widget), "tarn-ui-id"); }
static GtkWidget *dev_find_at(GtkWidget *widget, const char *id, const char *fallback) {
    const char *logical = dev_id(widget);
    if (g_strcmp0(logical ? logical : fallback, id) == 0) return widget;
    unsigned index = 0;
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        char *path = g_strdup_printf("%s/%u", logical ? logical : fallback, index++);
        GtkWidget *found = dev_find_at(child, id, path);
        g_free(path);
        if (found) return found;
    }
    return NULL;
}
static GtkWidget *dev_find(GtkWidget *widget, const char *id) { return dev_find_at(widget, id, "main"); }
static gboolean dev_allocated(GtkWidget *widget) {
    return !gtk_widget_is_visible(widget) || (gtk_widget_get_mapped(widget) &&
        gtk_widget_get_width(widget) > 0 && gtk_widget_get_height(widget) > 0);
}
static gboolean dev_ids_unique(GtkWidget *widget, GHashTable *ids) {
    const char *id = dev_id(widget);
    if (id && g_hash_table_contains(ids, id)) return FALSE;
    if (id) g_hash_table_add(ids, (gpointer)id);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
        if (!dev_ids_unique(child, ids)) return FALSE;
    return TRUE;
}
static JsonObject *dev_widget(DevChannel *d, GtkWidget *widget, GtkWidget *root,
                              const char *fallback, const char *parent, unsigned depth) {
    JsonObject *o = dev_object();
    const char *id = dev_id(widget);
    dev_string(o, "id", id ? id : fallback);
    dev_bool(o, "stable_id", id != NULL);
    dev_string(o, "parent_id", parent);
    dev_string(o, "native_type", G_OBJECT_TYPE_NAME(widget));
    GEnumClass *roles = g_type_class_ref(GTK_TYPE_ACCESSIBLE_ROLE);
    GEnumValue *role = g_enum_get_value(roles, gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(widget)));
    dev_string(o, "type", role ? role->value_nick : "unknown");
    g_type_class_unref(roles);
    const char *text = NULL;
    if (d->allow_text) {
        if (GTK_IS_LABEL(widget)) text = gtk_label_get_text(GTK_LABEL(widget));
        else if (GTK_IS_BUTTON(widget)) text = gtk_button_get_label(GTK_BUTTON(widget));
        else if (GTK_IS_WINDOW(widget)) text = gtk_window_get_title(GTK_WINDOW(widget));
    }
    dev_string(o, "text", text);
    dev_bool(o, "redacted", !d->allow_text);
    dev_string(o, "accessibility_label", NULL);
    dev_string(o, "accessibility_label_reason", "native property getter unavailable");
    dev_bool(o, "enabled", gtk_widget_is_sensitive(widget));
    gboolean visible = gtk_widget_is_visible(widget) && gtk_widget_get_mapped(widget);
    dev_bool(o, "visible", visible);
    graphene_rect_t rect;
    if (visible && gtk_widget_get_width(widget) > 0 && gtk_widget_compute_bounds(widget, root, &rect)) {
        JsonObject *b = dev_object();
        json_object_set_double_member(b, "x", rect.origin.x);
        json_object_set_double_member(b, "y", rect.origin.y);
        json_object_set_double_member(b, "width", rect.size.width);
        json_object_set_double_member(b, "height", rect.size.height);
        dev_string(b, "space", "window");
        dev_string(b, "units", "logical-pixels");
        dev_int(b, "scale", gtk_widget_get_scale_factor(widget));
        json_object_set_object_member(o, "bounds", b);
    } else {
        json_object_set_null_member(o, "bounds");
        dev_string(o, "bounds_reason", "not allocated or not visible");
    }
    json_object_set_null_member(o, "global_bounds");
    dev_string(o, "global_bounds_reason", "desktop coordinates not requested");
    JsonArray *children = json_array_new();
    dev_bool(o, "children_truncated", depth >= 32 && gtk_widget_get_first_child(widget) != NULL);
    unsigned index = 0;
    /* Bound traversal independently of request size. The Phase 3 tree is tiny. */
    if (depth < 32) for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) {
        char *path = g_strdup_printf("%s/%u", id ? id : fallback, index++);
        json_array_add_object_element(children, dev_widget(d, child, root, path, id ? id : fallback, depth + 1));
        g_free(path);
    }
    json_object_set_array_member(o, "children", children);
    return o;
}
static const char *dev_get_string(JsonObject *o, const char *key) {
    JsonNode *n = json_object_get_member(o, key);
    return n && JSON_NODE_HOLDS_VALUE(n) && json_node_get_value_type(n) == G_TYPE_STRING ? json_node_get_string(n) : NULL;
}
static gboolean dev_fields(JsonObject *o, const char *const *allowed) {
    GList *members = json_object_get_members(o);
    gboolean valid = TRUE;
    for (GList *it = members; it; it = it->next) {
        gboolean found = FALSE;
        for (unsigned i = 0; allowed[i]; ++i) if (g_str_equal(it->data, allowed[i])) found = TRUE;
        if (!found) valid = FALSE;
    }
    g_list_free(members);
    return valid;
}
typedef struct { GHashTable *members; gboolean duplicate; } DevParse;
static void dev_member(JsonParser *parser, JsonObject *object, const char *member, gpointer data) {
    (void)parser;
    DevParse *state = data;
    char *key = g_strdup_printf("%p:%s", (void *)object, member);
    if (g_hash_table_contains(state->members, key)) state->duplicate = TRUE;
    g_hash_table_add(state->members, key);
}
static void dev_request(Session *s, const char *record, gsize length) {
    DevChannel *d = s->dev;
    JsonParser *parser = json_parser_new();
    json_parser_set_strict(parser, TRUE);
    DevParse state = { g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL), FALSE };
    g_signal_connect(parser, "object-member", G_CALLBACK(dev_member), &state);
    JsonObject *o = NULL;
    const char *id = NULL, *error = "INVALID_REQUEST";
    if (memchr(record, 0, length) || !g_utf8_validate(record, length, NULL)) goto done;
    unsigned depth = 0; gboolean quoted = FALSE, escaped = FALSE;
    for (gsize i = 0; i < length; ++i) {
        char c = record[i];
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < length && memcmp(record + i + 1, "0000", 4) == 0) goto done;
                escaped = FALSE;
            }
            else if (c == '\\') escaped = TRUE;
            else if (c == '"') quoted = FALSE;
        } else if (c == '"') quoted = TRUE;
        else if (c == '{' || c == '[') { if (++depth > 64) goto done; }
        else if (c == '}' || c == ']') { if (depth) --depth; }
    }
    if (!json_parser_load_from_data(parser, record, length, NULL) || state.duplicate) goto done;
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) goto done;
    o = json_node_get_object(root);
    id = dev_get_string(o, "request_id");
    if (!id || !*id || strlen(id) > 128) { id = NULL; goto done; }
    for (const char *c = id; *c; ++c) if (!g_ascii_isalnum(*c) && *c != '_' && *c != '-') { id = NULL; goto done; }
    const char *const fields[] = {"version", "request_id", "method", "params", NULL};
    if (!dev_fields(o, fields) || json_object_get_size(o) != 4) goto done;
    JsonNode *version = json_object_get_member(o, "version");
    if (!JSON_NODE_HOLDS_VALUE(version) || json_node_get_value_type(version) != G_TYPE_INT64) goto done;
    if (json_node_get_int(version) != 1) { error = "UNSUPPORTED_VERSION"; goto done; }
    const char *method = dev_get_string(o, "method");
    JsonNode *params_node = json_object_get_member(o, "params");
    if (!method || !JSON_NODE_HOLDS_OBJECT(params_node)) goto done;
    JsonObject *params = json_node_get_object(params_node);
    if (g_hash_table_contains(d->request_ids, id)) goto done;
    if (g_hash_table_size(d->request_ids) >= 1024) { error = "QUEUE_FULL"; goto done; }
    g_hash_table_add(d->request_ids, g_strdup(id));
    if (g_str_equal(method, "session.authorize")) {
        const char *const auth_fields[] = {"session_id", NULL};
        if (!dev_fields(params, auth_fields) || json_object_get_size(params) != 1) goto done;
        if (g_strcmp0(dev_get_string(params, "session_id"), d->session_id) != 0) { error = "UNAUTHORIZED"; goto done; }
        d->authorized = TRUE;
        dev_reply(d, id, dev_result(d), NULL);
        error = NULL; goto done;
    }
    if (!d->authorized) { error = "UNAUTHORIZED"; goto done; }
    gboolean inspect = g_str_equal(method, "ui.inspect"), click = g_str_equal(method, "ui.click"), close = g_str_equal(method, "ui.close");
    if (!inspect && !click && !close) { error = "UNSUPPORTED_OPERATION"; goto done; }
    if ((inspect && !d->inspect) || (click && !d->click) || (close && !d->close)) { error = "UNAUTHORIZED"; goto done; }
    const char *const action_fields[] = {"window_id", "element_id", NULL};
    const char *const window_fields[] = {"window_id", NULL};
    if (!dev_fields(params, click ? action_fields : window_fields) || json_object_get_size(params) != (click ? 2 : 1)) goto done;
    if (!dev_get_string(params, "window_id") || (click && !dev_get_string(params, "element_id"))) goto done;
    if (g_strcmp0(dev_get_string(params, "window_id"), "main") != 0) { error = "NOT_FOUND"; goto done; }
    if (s->closed || d->closed) { error = "WINDOW_CLOSED"; goto done; }
    if (!gtk_widget_get_mapped(GTK_WIDGET(s->window)) || gtk_widget_get_width(GTK_WIDGET(s->window)) <= 0) { error = "NOT_READY"; goto done; }
    if (inspect) {
        JsonObject *result = dev_result(d);
        dev_string(result, "platform", "linux");
        dev_string(result, "backend", "gtk4");
        char *version_text = g_strdup_printf("%u.%u.%u", gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version());
        dev_string(result, "backend_version", version_text); g_free(version_text);
        dev_string(result, "window_id", "main");
        json_object_set_object_member(result, "root", dev_widget(d, GTK_WIDGET(s->window), GTK_WIDGET(s->window), "main", NULL, 0));
        dev_reply(d, id, result, NULL);
    } else if (close) {
        d->close_id = g_strdup(id);
        gtk_window_close(s->window);
    } else {
        GtkWidget *target = dev_find(GTK_WIDGET(s->window), dev_get_string(params, "element_id"));
        if (!target) { error = "NOT_FOUND"; goto done; }
        if (!GTK_IS_BUTTON(target)) { error = "WRONG_ELEMENT_TYPE"; goto done; }
        if (!gtk_widget_is_sensitive(target)) { error = "DISABLED"; goto done; }
        if (!gtk_widget_is_visible(target) || !gtk_widget_get_mapped(target)) { error = "HIDDEN"; goto done; }
        if (s->queue_full || s->pending_increments >= 64) { error = "QUEUE_FULL"; goto done; }
        d->pending_id = g_strdup(id);
        d->pending_token = ++d->sequence;
        d->deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
        s->enqueue_token = d->pending_token;
        /* Native semantic activation: emits GTK's clicked signal synchronously.
         * The C callback only queues an event, carrying this action token. */
        g_signal_emit_by_name(target, "clicked");
        s->enqueue_token = 0;
        d->timer = g_timeout_add(2000, dev_timeout, d);
    }
    error = NULL;
done:
    if (error) dev_reply(d, id, NULL, error);
    g_hash_table_unref(state.members);
    g_object_unref(parser);
}
static char *dev_pop(DevChannel *d, gsize *length) {
    char *end = memchr(d->input->str, '\n', d->input->len);
    if (!end) return NULL;
    *length = end - d->input->str;
    char *record = g_strndup(d->input->str, *length);
    g_string_erase(d->input, 0, *length + 1);
    --d->lines;
    return record;
}
static gboolean dev_pump(Session *s) {
    DevChannel *d = s->dev;
    if (!d) return FALSE;
    if (d->failed) { dev_free(s); return FALSE; }
    if (d->terminal) {
        dev_reply(d, NULL, NULL, d->terminal);
        dev_free(s); return FALSE;
    }
    if (!d->ready) {
        if (!gtk_widget_get_mapped(GTK_WIDGET(s->window)) || !dev_allocated(GTK_WIDGET(s->window)) ||
            !dev_allocated(s->counter_label) || !dev_allocated(s->button)) return FALSE;
        JsonObject *o = dev_result(d);
        dev_int(o, "version", 1); dev_string(o, "event", "ready");
        dev_wire(d, o); d->ready = TRUE; return TRUE;
    }
    if (d->pending_id) {
        if (g_get_monotonic_time() >= d->deadline) d->timed_out = TRUE;
        if (s->closed || d->timed_out) {
            dev_reply(d, d->pending_id, NULL, s->closed ? "WINDOW_CLOSED" : "TIMEOUT");
            dev_clear_pending(d);
            if (d->timed_out) { dev_free(s); return FALSE; }
        } else return FALSE;
    }
    if (d->output->len || d->close_id) return FALSE;
    gsize length;
    char *record = dev_pop(d, &length);
    if (!record) return FALSE;
    dev_request(s, record, length); g_free(record);
    return TRUE;
}
static void dev_complete(Session *s, int32_t status) {
    DevChannel *d = s->dev;
    if (!d) return;
    if (d->pending_id && g_get_monotonic_time() >= d->deadline) {
        dev_reply(d, d->pending_id, NULL, "TIMEOUT");
        dev_free(s);
        return;
    }
    if (!status) ++d->revision;
    if (d->pending_id && s->dispatched_token == d->pending_token) {
        dev_reply(d, d->pending_id, status ? NULL : dev_result(d), status ? "NATIVE_ERROR" : NULL);
        dev_clear_pending(d);
    }
}
static void dev_dispose(Session *s, gboolean window_closed) {
    DevChannel *d = s->dev;
    if (!d) return;
    if (d->pending_id) {
        dev_reply(d, d->pending_id, NULL, window_closed ? "WINDOW_CLOSED" : "NATIVE_ERROR");
        dev_clear_pending(d);
    }
    d->closed = TRUE;
    if (d->close_id) {
        ++d->revision;
        JsonObject *result = dev_result(d); dev_bool(result, "closed", TRUE);
        dev_reply(d, d->close_id, result, NULL);
    }
    gsize length; char *record;
    while ((record = dev_pop(d, &length))) { dev_request(s, record, length); g_free(record); }
    /* A non-reading peer may lose replies at teardown; never invent success.
     * Normal replies fit the private socket. All sources/FDs still get released. */
    dev_free(s);
}
static int dev_start(Session *s) {
    GHashTable *ids = g_hash_table_new(g_str_hash, g_str_equal);
    gboolean unique = dev_ids_unique(GTK_WIDGET(s->window), ids);
    g_hash_table_unref(ids);
    const char *fd_text = g_getenv("TARN_UI_DEV_FD");
    if (!fd_text) return unique ? 0 : 1; /* explicit opt-in only */
    char *end; errno = 0;
    long fd = strtol(fd_text, &end, 10);
    if (errno || !*fd_text || *end || fd < 3 || fd > G_MAXINT) return 1;
    struct sockaddr_storage address; socklen_t length = sizeof(address);
    struct ucred peer; socklen_t peer_length = sizeof(peer);
    int type; socklen_t type_length = sizeof(type);
    if (getsockname(fd, (struct sockaddr *)&address, &length) || address.ss_family != AF_UNIX ||
        getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_length) || type != SOCK_STREAM ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) || peer.uid != getuid()) return 1;
    DevChannel *d = g_new0(DevChannel, 1);
#ifdef TARN_UI_TESTING
    ++dev_channels;
#endif
    d->fd = fd;
    d->input = g_string_new(NULL); d->output = g_string_new(NULL);
    d->session_id = g_uuid_string_random();
    d->request_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    d->allow_text = g_strcmp0(g_getenv("TARN_UI_DEV_TEXT"), "1") == 0;
    s->dev = d;
    const char *grants = g_getenv("TARN_UI_DEV_ALLOW");
    if (!grants || !*grants) { dev_free(s); return 1; }
    char **parts = g_strsplit(grants, ",", -1);
    gboolean valid = unique;
    for (unsigned i = 0; parts[i]; ++i) {
        if (g_str_equal(parts[i], "inspect")) d->inspect = TRUE;
        else if (g_str_equal(parts[i], "click")) d->click = TRUE;
        else if (g_str_equal(parts[i], "close")) d->close = TRUE;
        else valid = FALSE;
    }
    g_strfreev(parts);
    if (!valid || fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        dev_free(s); return 1;
    }
    d->reader = g_unix_fd_add(fd, G_IO_IN | G_IO_HUP | G_IO_ERR, dev_readable, d);
    return 0;
}
