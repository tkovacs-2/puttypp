#ifndef TABBAR_H
#define TABBAR_H

#define TAB_BAR_NOTIFY_ID 0
#define TCN_TABDELETE 1
#define TCN_TABEXCHANGE 2
#define TCN_OUTSIDE_DRAG 3
#define TCN_OUTSIDE_RELEASE 4
#define TCN_OUTSIDE_CANCEL 5

typedef struct TabBarNotify {
    NMHDR hdr;
    int tab_origin;
    POINT point;
} TabBarNotify;

typedef struct TabBar {
    HWND hwnd;
    UINT_PTR notify_blink_timer;
    void *user_data;
    bool focused;
} TabBar;

void tab_bar_common_init(HFONT dpiAwareFont);
void tab_bar_common_uninit();
void tab_bar_common_dpi_changed(HFONT dpiAwareFont);
int tab_bar_common_height();

void tab_bar_init(TabBar *tab_bar, const RECT *rect, void *user_data);
void tab_bar_uninit(TabBar *tab_bar);
void tab_bar_dpi_changed(TabBar *tab_bar);
HDWP tab_bar_adjust_window(TabBar *tab_bar, const RECT *parentRect, HDWP hdwp);
void tab_bar_insert_tab(TabBar *tab_bar, int index, const char *title, int image);
void tab_bar_remove_tab(TabBar *tab_bar, int index);
void tab_bar_select_tab(TabBar *tab_bar, int index);
int tab_bar_get_active_tab(TabBar *tab_bar);
void tab_bar_set_tab_title(TabBar *tab_bar, int index, const char *title);
void tab_bar_set_tab_unusable(TabBar *tab_bar, int index, bool unusable);
void tab_bar_set_tab_notified(TabBar *tab_bar, int index);
void tab_bar_clear_tab_notified(TabBar *tab_bar, int index);
void tab_bar_import_tab(TabBar *tab_bar, TabBar *source, int target_index, int source_index);
TabBar *tab_bar_get_from_hwnd(HWND hwnd);
void tab_bar_set_focused(TabBar *tab_bar, bool focused);
void tab_bar_cancel_dragging(TabBar *tab_bar);

#endif
