#ifndef TARN_UI_GTK_BRIDGE_H
#define TARN_UI_GTK_BRIDGE_H
#include <stdint.h>

/* ABI 6: pointers are opaque; input strings are copied during each call.
 * All operations require the Linux process main thread. release consumes a
 * valid session on that thread, including one whose window is already closed.
 * Status: 0 success; 1 invalid input, 2 wrong thread, 3 session active,
 * 4 no display, 5 allocation/creation failure, 6 closed, 8 event queue full.
 * next: 1 Opened, 2 Closed, 3 Increment, or negative status.
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
/* After applying an Increment or keyed component action in Tarn. */
int32_t tarn_ui_gtk_complete_event(void *session, int32_t status);
#endif
