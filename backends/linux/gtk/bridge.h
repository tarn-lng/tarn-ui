#ifndef TARN_UI_GTK_BRIDGE_H
#define TARN_UI_GTK_BRIDGE_H
#include <stdint.h>

/* ABI 14: pointers are opaque; input strings are copied during each call.
 * All operations require the Linux process main thread. release consumes a
 * valid session on that thread, including one whose window is already closed.
 * Status: 0 success; 1 invalid input, 2 wrong thread, 3 session active,
 * 4 no display, 5 allocation/creation failure, 6 closed, 8 event queue full.
 * next: 1 Opened, 2 Closed, 3 Increment, 4 Hidden, 5 Wake, or negative status.
 * Closed supersedes pending events. Increment queue capacity is 64.
 */
uint32_t tarn_ui_gtk_abi(void);
int32_t tarn_ui_gtk_create(const char *title, const char *heading, void **out);
int32_t tarn_ui_gtk_next(void *session);
int32_t tarn_ui_gtk_request_close(void *session);
int32_t tarn_ui_gtk_release(void *session);
int32_t tarn_ui_gtk_finish(void *session, int32_t application_error);
int32_t tarn_ui_gtk_enable_counter(void *session, int64_t value);
int32_t tarn_ui_gtk_set_counter(void *session, int64_t value);
int32_t tarn_ui_gtk_set_heading(void *session, const char *text);
/* Keyed pages and leaf components: 32 pages, 256 keyed widgets, 128-byte keys.
 * Page/component keys share a namespace; action IDs are in 1..1024.
 * show_page retains both pages; normal session teardown frees their trees.
 * next additionally returns 1000 + action ID for a component activation. */
int32_t tarn_ui_gtk_add_page(void *session, const char *key);
int32_t tarn_ui_gtk_add_label(void *session, const char *page, const char *key, const char *text);
int32_t tarn_ui_gtk_add_button(void *session, const char *page, const char *key, const char *text, uint32_t action);
int32_t tarn_ui_gtk_set_text(void *session, const char *key, const char *text);
int32_t tarn_ui_gtk_show_page(void *session, const char *key);
int32_t tarn_ui_gtk_add_box(void *session, const char *parent, const char *key, uint32_t horizontal, uint32_t spacing);
/* Text styles: 0 body, 1 heading (level 2), 2 prominent value. */
int32_t tarn_ui_gtk_add_text(void *session, const char *parent, const char *key, const char *text, uint32_t style);
int32_t tarn_ui_gtk_set_primary(void *session, const char *key, uint32_t primary);
int32_t tarn_ui_gtk_set_accessible(void *session, const char *key, const char *label, const char *description);
int32_t tarn_ui_gtk_focus(void *session, const char *key);
/* After applying an Increment or keyed component action in Tarn. */
int32_t tarn_ui_gtk_complete_event(void *session, int32_t status);
/* Entry changes enqueue owned text snapshots, limited to 256 Unicode characters.
 * event_text is borrowed until next()/release(); copy before either call. */
int32_t tarn_ui_gtk_add_entry(void *session, const char *parent, const char *key, const char *text, uint32_t action);
const char *tarn_ui_gtk_event_text(void *session);
int32_t tarn_ui_gtk_set_enabled(void *session, const char *key, uint32_t enabled);
/* Model write: no changed action; rejects invalid UTF-8 or >256 characters. */
int32_t tarn_ui_gtk_set_input(void *session, const char *key, const char *text);
/* Exclusive dynamic GtkBox region, keyed button rows. Errors require scope exit.
 * begin(count), item in desired order, end removes absent rows. Stable keys keep
 * widgets and action IDs. The completed registry remains bounded to 256 keys. */
int32_t tarn_ui_gtk_list_begin(void *session, const char *parent, uint32_t count);
int32_t tarn_ui_gtk_list_item(void *session, const char *key, const char *text, uint32_t action);
int32_t tarn_ui_gtk_list_end(void *session);
/* Mixed region nodes: body=0, heading=1, value=2, button=3, primary=4,
 * input=5, column=6, row=7. Preorder, fixed kind/parent/action per stable key. */
int32_t tarn_ui_gtk_tree_begin(void *session, const char *key, uint32_t count);
int32_t tarn_ui_gtk_tree_node(void *session, const char *parent, const char *key, uint32_t kind, const char *text, uint32_t action, uint32_t spacing);
int32_t tarn_ui_gtk_tree_end(void *session);
/* ABI 13+ form controls in static pages: switch=0, check=1, scale=2, spin=3,
 * dropdown=4. text is a single-line caption; for dropdown, the caption line
 * is followed by 1..64 newline-separated non-empty options. Switch/check use 0..1; dropdown uses the option index;
 * scale/spin use min..max within +-1e9. A user change queues 1000 + action with
 * the new decimal value as event_text. set_input writes a decimal value without
 * an event and rejects values outside the range. */
int32_t tarn_ui_gtk_add_control(void *session, const char *parent, const char *key, uint32_t kind,
                                const char *text, uint32_t action, int64_t value, int64_t min, int64_t max);
/* ABI 14 background mode. With set_background(1), closing the window hides
 * it and next returns 4 (Hidden) instead of Closed; request_close still ends
 * the session. wake sets a coalesced flag (next returns 5) and is meant for
 * GLib source callbacks, such as a D-Bus socket watch. is_visible: 1, 0 or a
 * negative status. */
int32_t tarn_ui_gtk_wake(void *session);
int32_t tarn_ui_gtk_set_background(void *session, uint32_t enabled);
int32_t tarn_ui_gtk_present(void *session);
int32_t tarn_ui_gtk_hide(void *session);
int32_t tarn_ui_gtk_is_visible(void *session);
/* Containers, keyed by their content box: frame=0, expander=1 (value 0/1),
 * tabs=2 (value = selected index, empty title; hosts only tabs), tab=3 (child
 * of tabs, non-empty title). With action > 0, expander and tabs queue
 * 1000 + action with the new state as event text; set_input writes it. */
int32_t tarn_ui_gtk_add_container(void *session, const char *parent, const char *key, uint32_t kind,
                                  const char *title, uint32_t action, int64_t value);
/* Region nodes (between tree_begin and tree_end) for controls, frames,
 * expanders and multiline text, with the arguments of their static forms.
 * Existing keys keep kind, parent, action and range; values are rewritten
 * without events. Tabs are static-only. */
int32_t tarn_ui_gtk_tree_control(void *session, const char *parent, const char *key, uint32_t kind, const char *text,
                                 uint32_t action, int64_t value, int64_t min, int64_t max);
int32_t tarn_ui_gtk_tree_container(void *session, const char *parent, const char *key, uint32_t kind,
                                   const char *title, uint32_t action, int64_t value);
int32_t tarn_ui_gtk_tree_text_area(void *session, const char *parent, const char *key, const char *text,
                                   uint32_t action, uint32_t lines);
#endif
