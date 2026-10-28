#define DECLSPEC_IMPORT __declspec()

#include <windows.h>
#include <stdbool.h>
#include <Commctrl.h>
#include <windowsx.h>
#include <assert.h>
#include <stdio.h>

#include "tabbar.h"
#include "pane.h"
#include "panesizetip.h"
#include "split.h"
#include "splitmarker.h"
#include "pastedlg.h"
#include "finddlg.h"
#include "tabbar_res.h"

#include "stubterminal.h"
#include "stubdialog.h"
#include "stubapi.h"

#include "frame.h"
#include "stubwindow.c"

WINUSERAPI BOOL WINAPI GetMonitorInfo(HMONITOR mon, LPMONITORINFO info);
WINUSERAPI HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD flags);
WINUSERAPI HRESULT WINAPI GetDpiForMonitor(HMONITOR mon, enum MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY);
WINUSERAPI BOOL WINAPI SystemParametersInfoForDpi(UINT action, UINT param, PVOID ptr, UINT winini, UINT dpi);

static void init_winfuncs() {
    p_GetMonitorInfoA = GetMonitorInfoA;
    p_MonitorFromWindow = MonitorFromWindow;
    p_GetDpiForMonitor = GetDpiForMonitor;
    p_SystemParametersInfoForDpi = SystemParametersInfoForDpi;
}

INT_PTR CALLBACK ConfDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return 0;
}

bool do_term_remote(HWND hwnd, TermRemoteResult *result) {
    return false;
}

static void add_initial_session(Pane *pane, const char *saved_session, int index) {
    Conf *conf = conf_new();
    do_defaults(saved_session, conf);
    const char *session_name = dupstr(saved_session);
    WinGuiSession *wgs = add_stale_session(pane, conf, session_name, index);
    set_palette(wgs);
}

static void check_session_window_layout(Pane *pane) {
    WinGuiSession *wgs = pane_get_active_session(pane);

    RECT rect;
    RECT required;
    required.left = 5;
    required.top = 35 + tab_bar_common_height();
    required.right = required.left + 14 * 80 + 1 * 2;
    required.bottom = required.top + 24 * 24 + 1 * 2;
    GetWindowRect(wgs->term_hwnd, &rect);
    assert(rect.left == required.left);
    assert(rect.top == required.top);
    assert(rect.right == required.right);
    assert(rect.bottom == required.bottom);

    required.left = 0;
    required.top = 0;
    required.right = required.right + 5;
    required.bottom = required.bottom + 5;
    GetWindowRect(frame_hwnd, &rect);
    assert(rect.left == required.left);
    assert(rect.top == required.top);
    assert(rect.right == required.right);
    assert(rect.bottom == required.bottom);
}

int main() {
    for (int i=IDI_BACKEND_FIRST; i<=IDI_BACKEND_LAST; i++) {
        stub_resource_add_icon(i);
    }
    stub_resource_add_bitmap(IDR_CLOSETAB, 11, 11);
    stub_resource_add_bitmap(IDR_CLOSETAB_INACT, 11, 11);
    stub_resource_add_bitmap(IDR_CLOSETAB_HOVER, 11, 11);
    stub_resource_add_bitmap(IDR_CLOSETAB_PUSH, 11, 11);
    stub_resource_add_bitmap(IDR_BLINKTAB, 8, 8);

    init_winfuncs();
    register_term_hwnd_class();
    register_frame_class();
    create_frame_window();

    init_window_dpi_info(frame_hwnd, &dpi_info);
    menu_font = get_dpi_aware_menu_font();
    tab_bar_common_init(menu_font);
    size_tip_common_init(menu_font);
    split_common_init(session_set_index);
    split_marker_init();

    RECT rect;
    GetClientRect(frame_hwnd, &rect);
    root_split = split_create(&rect);
    Pane *pane = split_get_pane(root_split);
    focused_pane = pane;

    add_initial_session(pane, "resize_term", 0);
    set_term_hwnd_style(pane, pane_get_session(pane, 0));
    pane_select_session(pane, 0);
    realize_palette(pane_get_active_session(pane));
    snap_frame_to_term(pane);

    ShowWindow(frame_hwnd, SW_SHOWNORMAL);
    stubwinapi_update_windows();
    check_session_window_layout(pane);

    DestroyWindow(frame_hwnd);

    return 0;
}

#include "frame.c"
