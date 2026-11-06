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
#include "testassert.h"

static Conf initial_test_conf = {RESIZE_TERM, 80, 24, FALSE, false, {12, ""}, 0, false, false, false, PROT_SSH, "b", CONF_COLORS_DEFAULT_PUTTY};
DEFINE_ASSERT_FAIL_COUNTER;

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

typedef bool ConfDlgFunction(Conf *conf);
ConfDlgFunction *conf_dlg_function = NULL;

INT_PTR CALLBACK ConfDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ConfDlgParam *param = (ConfDlgParam *)lParam;
    return conf_dlg_function(param->conf) ? IDOK : IDCANCEL;
}

bool change_term_window_style(Conf *conf) {
    conf_set_int(conf, CONF_window_border, 10);
    conf_set_bool(conf, CONF_scrollbar, true);
    conf_set_bool(conf, CONF_sunken_edge, true);
    return true;
}

bool do_term_remote(HWND hwnd, TermRemoteResult *result) {
    return false;
}

static void add_initial_session(Pane *pane, const char *saved_session, int index) {
    Conf *conf = conf_new();
    *conf = initial_test_conf;
    const char *session_name = dupstr("initial_test_conf");
    WinGuiSession *wgs = add_stale_session(pane, conf, session_name, index);
    set_palette(wgs);
}

static bool check_root_pane_layout(int cols, int rows, int font_width, int font_height) {
    WinGuiSession *wgs = pane_get_active_session(split_get_pane(root_split));
    ASSERT_STASH_FAIL_COUNT();
    ASSERT_EQUALS(cols, conf_get_int(wgs->conf, CONF_width));
    ASSERT_EQUALS(rows, conf_get_int(wgs->conf, CONF_height));
    ASSERT_EQUALS(cols, wgs->term->cols);
    ASSERT_EQUALS(rows, wgs->term->rows);
    ASSERT_EQUALS(font_width, wgs->font_width);
    ASSERT_EQUALS(font_height, wgs->font_height);

    int border_width = conf_get_int(wgs->conf, CONF_window_border);
    int term_width = cols * font_width + 2 * border_width;
    int term_height = rows * font_height + 2 * border_width;
    if (conf_get_bool(wgs->conf, CONF_scrollbar)) {
        term_width += 10;
    }
    if (conf_get_bool(wgs->conf, CONF_sunken_edge)) {
        term_width += 2 * 2;
        term_height += 2 * 2;
    }

    RECT rect;
    GetWindowRect(wgs->term_hwnd, &rect);
    ASSERT_EQUALS(rect.left, 5);
    ASSERT_EQUALS(rect.top, 5 + 30 + 24);
    ASSERT_EQUALS(rect.right, rect.left + term_width);
    ASSERT_EQUALS(rect.bottom, rect.top + term_height);

    GetWindowRect(frame_hwnd, &rect);
    ASSERT_EQUALS(rect.left, 0);
    ASSERT_EQUALS(rect.top, 0);
    ASSERT_EQUALS(rect.right, term_width + 2 * 5);
    ASSERT_EQUALS(rect.bottom, term_height + 2 * 5 + 30 + 24);

    return !ASSERT_HAS_NEW_FAILURE();
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

    ASSERT_TRUE(check_root_pane_layout(80, 24, 14, 24));

    conf_dlg_function = change_term_window_style;
    SendMessage(pane_get_term_hwnd(focused_pane), WM_KEYDOWN, 'R', 0);
    ASSERT_TRUE(conf_get_bool(pane_get_active_session(pane)->conf, CONF_scrollbar));
    ASSERT_TRUE(check_root_pane_layout(80, 24, 14, 24));

    SendMessage(frame_hwnd, WM_CLOSE, 0, 0);
    tab_bar_common_uninit();
    DeleteObject(menu_font);

    ASSERT_CHECK_FAIL_COUNT();

    return 0;
}

#include "frame.c"
