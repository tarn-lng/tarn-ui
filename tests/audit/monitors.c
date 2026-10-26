#include <gtk/gtk.h>
#include <stdio.h>

int main(void) {
    gtk_init();
    GListModel *monitors = gdk_display_get_monitors(gdk_display_get_default());
    printf("{\"monitors\":[");
    for (guint i = 0; i < g_list_model_get_n_items(monitors); ++i) {
        GdkMonitor *monitor = g_list_model_get_item(monitors, i);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        printf("%s{\"width\":%d,\"height\":%d,\"scale\":%d}", i ? "," : "",
               geometry.width, geometry.height, gdk_monitor_get_scale_factor(monitor));
        g_object_unref(monitor);
    }
    puts("]}");
    return 0;
}
