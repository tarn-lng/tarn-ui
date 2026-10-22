#ifndef TARN_UI_TEST_HOOKS_H
#define TARN_UI_TEST_HOOKS_H
#include <gtk/gtk.h>
typedef struct {
    unsigned allocated, freed, windows, windows_finalized;
    unsigned headings, headings_finalized, connected, disconnected;
    unsigned counter_widgets, counter_widgets_finalized;
    unsigned counter_updates, heading_updates;
    unsigned components, components_finalized;
    unsigned component_text_calls, page_calls;
} TestStats;
TestStats test_stats(void);
void test_fail_at(int stage);
GtkWindow *test_window(void *raw);
int32_t test_clean(void);
int32_t test_activate(int32_t count);
int32_t test_activate_native(void);
int32_t test_label_equals(int64_t value);
uint32_t test_counter_updates(void);
uint32_t test_heading_updates(void);
uint32_t test_component_text_calls(void);
uint32_t test_page_calls(void);
#endif
