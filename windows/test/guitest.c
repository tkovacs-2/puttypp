#include <windows.h>
#include <stdbool.h>
#include <Commctrl.h>
#include <windowsx.h>
#include <assert.h>

#include "tabbar.h"
#include "pane.h"
#include "panesizetip.h"
#include "split.h"
#include "splitmarker.h"
#include "pastedlg.h"
#include "finddlg.h"

#include "stubterminal.h"
#include "stubdialog.h"

typedef struct WinGuiSession WinGuiSession;

#include "frame.h"
#include "stubwindow.c"

#define GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, name) \
    (p_##name = module ?                                \
    (t_##name) GetProcAddress(module, #name) : NULL)

static void init_winfuncs() {
    HMODULE user32_module = GetModuleHandle("user32.dll");
    HMODULE shcore_module = GetModuleHandle("shcore.dll");
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetMonitorInfoA);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, MonitorFromWindow);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(shcore_module, GetDpiForMonitor);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, SystemParametersInfoForDpi);
}

static void add_initial_session(Pane *pane, const char *saved_session, int index, bool try_palette) {
    Conf *conf = conf_new();
    const char *session_name = dupstr(saved_session);
    do_defaults(saved_session, conf);
    conf_set_bool(conf, CONF_try_palette, try_palette);
    WinGuiSession *wgs = add_stale_session(pane, conf, session_name, index);
    set_palette(wgs);
}

static bool get_initial_try_palette(const char *cmdline) {
    while (*cmdline == ' ') {cmdline++;}
    if (strncmp(cmdline, "--try-palette", 13) == 0) {
        cmdline += 13;
        if (*cmdline == 0 || *cmdline == ' ') {
            return true;
        }
    }
    return false;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
    hinst = inst;
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

    add_initial_session(pane, "resize_term", 0, get_initial_try_palette(cmdline));
    add_initial_session(pane, "resize_term", 1, false);
    add_initial_session(pane, "resize_either", 2, false);
    add_initial_session(pane, "disabled", 2, false);
    pane_set_session_unusable(pane, 2, true);
    add_initial_session(pane, "resize_font", 2, false);
    pane_set_session_unusable(pane, 2, true);
    set_term_hwnd_style(pane, pane_get_session(pane, 0));
    pane_select_session(pane, 0);
    realize_palette(pane_get_active_session(pane));
    snap_frame_to_term(pane);

    popup_menus[0].menu = CreatePopupMenu();
    AppendMenu(popup_menus[0].menu, MF_ENABLED, IDM_FULLSCREEN, "Full Screen");
    AppendMenu(popup_menus[0].menu, MF_ENABLED, IDM_ALWAYSONTOP, "Always on top");
    AppendMenu(popup_menus[0].menu, MF_ENABLED, IDM_VISUALIZECARET, "Visualize caret");

    ShowWindow(frame_hwnd, show);
    UpdateWindow(frame_hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            break;
        }
        if (finddlg_is_dialog_message(&msg)) {continue;}
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

#include "frame.c"
