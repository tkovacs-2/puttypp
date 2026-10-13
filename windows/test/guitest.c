#include <windows.h>
#include <stdbool.h>
#include <Commctrl.h>
#include <stdio.h>
#include <windowsx.h>
#include <assert.h>

#include "tabbar.h"
#include "pane.h"
#include "panesizetip.h"
#include "split.h"
#include "splitmarker.h"
#include "pastedlg.h"
#include "finddlg.h"

static Split *root_split = NULL;
static Pane *focused_pane = NULL;
static HFONT tab_bar_font = NULL;
static bool resizing = false;
static bool is_zoomed = false;
static bool is_minimized = false;
static bool is_fullscr = false;
static WINDOWPLACEMENT fullscr_placement = {sizeof(WINDOWPLACEMENT)};
static bool skip_update_split_layout = false;
static RECT split_layout_rect = {0, 0, 0, 0};

HWND frame_hwnd = NULL;
HINSTANCE hinst = NULL;
POINT dpi_info = {0, 0};

#include "stubputty.c"
typedef struct WinGuiSession WinGuiSession;

#include "frame.h"
#include "confdlg.c"
#include "stubwindow.c"

static void add_initial_session(Pane *pane, const char *saved_session, int index) {
  Conf *conf = conf_new();
  const char *session_name = dupstr(saved_session);
  do_defaults(saved_session, conf);
  WinGuiSession *wgs = add_stale_session(pane, conf, session_name, index);
  set_palette(wgs);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
  hinst = inst;
  init_winfuncs();
  register_term_hwnd_class();
  register_frame_class();
  create_frame_window();

  init_window_dpi_info(frame_hwnd, &dpi_info);
  tab_bar_font = get_dpi_aware_tab_bar_font();
  tab_bar_common_init(tab_bar_font);
  size_tip_common_init(tab_bar_font);
  split_common_init(session_set_index);
  split_marker_init();

  RECT rect;
  GetClientRect(frame_hwnd, &rect);
  root_split = split_create(&rect);
  Pane *pane = split_get_pane(root_split);
  focused_pane = pane;

  add_initial_session(pane, "resize_term", 0);
  add_initial_session(pane, "resize_term", 1);
  add_initial_session(pane, "resize_either", 2);
  add_initial_session(pane, "disabled", 2);
  pane_set_session_unusable(pane, 2, true);
  add_initial_session(pane, "resize_font", 2);
  pane_set_session_unusable(pane, 2, true);
  set_term_hwnd_style(pane, pane_get_session(pane, 0));
  pane_select_session(pane, 0);
  realize_palette(pane_get_active_session(pane));
  snap_frame_to_term(pane);

  popup_menus[0].menu = CreatePopupMenu();
  AppendMenu(popup_menus[0].menu, MF_ENABLED, IDM_FULLSCREEN, "Full Screen");
  AppendMenu(popup_menus[0].menu, MF_ENABLED, IDM_ALWAYSONTOP, "Always on top");

  ShowWindow(frame_hwnd, show);
  UpdateWindow(frame_hwnd);

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0))
  {
    if (msg.message == WM_QUIT)
    {
      break;
    }
    if (finddlg_is_dialog_message(&msg)) {continue;}
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return 0;
}

#include "frame.c"
