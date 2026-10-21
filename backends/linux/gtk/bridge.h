#ifndef TARN_UI_GTK_BRIDGE_H
#define TARN_UI_GTK_BRIDGE_H
#include <stdint.h>

/* ABI 4: pointers are opaque; strings are copied during create.
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
/* After handling an Increment in Tarn, including committing its owned state. */
int32_t tarn_ui_gtk_complete_event(void *session, int32_t status);
#endif
