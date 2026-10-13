#include "confdlg_res.h"
#include <string.h>

typedef enum TermRemoteAction {
  TERM_REMOTE_RESIZE,
  TERM_REMOTE_MAXIMISE,
  TERM_REMOTE_RESTORE,
  TERM_REMOTE_MINIMISE,
  TERM_REMOTE_MOVE,
  TERM_REMOTE_Z_TOP,
  TERM_REMOTE_Z_BOTTOM,
} TermRemoteAction;

typedef struct TermRemoteResult {
  TermRemoteAction action;
  bool delayed;
  union {
      struct {
          int width;
          int height;
      } resize;
      struct {
          int x;
          int y;
      } move;
  } param;
} TermRemoteResult;

typedef struct ConfDlgParam {
    Conf *conf;
    const char **session_name;
    bool reconfig;
} ConfDlgParam;

static int resize_action_to_control_id(ResizeAction resize_action)
{
    switch (resize_action) {
      case RESIZE_DISABLED:
        return IDC_CONF_RESIZE_DISABLED;
      case RESIZE_TERM:
        return IDC_CONF_RESIZE_TERM;
      case RESIZE_FONT:
        return IDC_CONF_RESIZE_FONT;
      case RESIZE_EITHER:
        return IDC_CONF_RESIZE_EITHER;
    }
    return IDC_CONF_RESIZE_TERM;
}

static ResizeAction control_id_to_resize_action(int control_id)
{
    switch (control_id) {
      case IDC_CONF_RESIZE_DISABLED:
        return RESIZE_DISABLED;
      case IDC_CONF_RESIZE_TERM:
        return RESIZE_TERM;
      case IDC_CONF_RESIZE_FONT:
        return RESIZE_FONT;
      case IDC_CONF_RESIZE_EITHER:
        return RESIZE_EITHER;
    }
    return RESIZE_TERM;
}

static void set_conf_dialog_bool(HWND hwnd, int control_id, bool value)
{
    CheckDlgButton(hwnd, control_id, value ? BST_CHECKED : BST_UNCHECKED);
}

static bool get_conf_dialog_bool(HWND hwnd, int control_id)
{
    return IsDlgButtonChecked(hwnd, control_id) == BST_CHECKED;
}

static void set_conf_dialog_text(HWND hwnd, int control_id, const char *text)
{
    SetDlgItemTextA(hwnd, control_id, text ? text : "");
}

static bool get_conf_dialog_text(HWND hwnd, int control_id, char *buffer,
                                 size_t buffer_size)
{
    if (GetDlgItemTextA(hwnd, control_id, buffer, (int)buffer_size) < 0)
        return false;
    return true;
}

static bool read_conf_dialog_int(HWND hwnd, int control_id,
                                 const WCHAR *field_name, int *value)
{
    BOOL translated;
    int dialog_value = GetDlgItemInt(hwnd, control_id, &translated, TRUE);

    if (!translated) {
        WCHAR message[128];
        wsprintfW(message, L"%s must be an integer.", field_name);
        MessageBoxW(hwnd, message, L"Configuration", MB_ICONERROR | MB_OK);
        SetFocus(GetDlgItem(hwnd, control_id));
        return false;
    }

    *value = dialog_value;
    return true;
}

static void hide_conf_dialog_host(HWND hwnd)
{
    HWND host_edit = GetDlgItem(hwnd, IDC_CONF_HOST);
    ShowWindow(host_edit, SW_HIDE);
    ShowWindow(GetWindow(host_edit, GW_HWNDPREV), SW_HIDE);
}

static void init_conf_dialog_controls(HWND hwnd, const ConfDlgParam *param)
{
    Conf *conf = param->conf;
    if (param->reconfig) {
        hide_conf_dialog_host(hwnd);
    } else {
        set_conf_dialog_text(hwnd, IDC_CONF_HOST, conf_get_str(conf, CONF_host));
    }
    CheckRadioButton(hwnd, IDC_CONF_RESIZE_DISABLED, IDC_CONF_RESIZE_EITHER,
                     resize_action_to_control_id(conf->resize_action));
    SetDlgItemInt(hwnd, IDC_CONF_WIDTH, conf->width, TRUE);
    SetDlgItemInt(hwnd, IDC_CONF_HEIGHT, conf->height, TRUE);
    set_conf_dialog_bool(hwnd, IDC_CONF_SCROLLBAR, conf->scrollbar);
    set_conf_dialog_bool(hwnd, IDC_CONF_SCROLLBAR_FULLSCREEN,
                         conf->scrollbar_in_fullscreen);
    SetDlgItemInt(hwnd, IDC_CONF_FONTSPEC_HEIGHT, conf->fontspec.height, TRUE);
    SetDlgItemInt(hwnd, IDC_CONF_WINDOW_BORDER, conf->window_border, TRUE);
    set_conf_dialog_bool(hwnd, IDC_CONF_SUNKEN_EDGE, conf->sunken_edge);

    if (*param->session_name) {
        set_conf_dialog_text(hwnd, IDC_CONF_SESSION_NAME, *param->session_name);
    }
}

static bool save_conf_dialog_controls(HWND hwnd, const ConfDlgParam *param)
{
    char host[256];
    char session_name[256];
    int width = 0, height = 0, fontspec_height = 0, window_border = 0;
    int resize_control_id = IDC_CONF_RESIZE_TERM;

    for (int control_id = IDC_CONF_RESIZE_DISABLED;
         control_id <= IDC_CONF_RESIZE_EITHER; control_id++) {
        if (IsDlgButtonChecked(hwnd, control_id) == BST_CHECKED) {
            resize_control_id = control_id;
            break;
        }
    }

    if ((!param->reconfig && !get_conf_dialog_text(hwnd, IDC_CONF_HOST, host, sizeof(host))) ||
        !get_conf_dialog_text(hwnd, IDC_CONF_SESSION_NAME, session_name, sizeof(session_name)) ||
        !read_conf_dialog_int(hwnd, IDC_CONF_WIDTH, L"Width", &width) ||
        !read_conf_dialog_int(hwnd, IDC_CONF_HEIGHT, L"Height", &height) ||
        !read_conf_dialog_int(hwnd, IDC_CONF_FONTSPEC_HEIGHT, L"Font height", &fontspec_height) ||
        !read_conf_dialog_int(hwnd, IDC_CONF_WINDOW_BORDER, L"Window border", &window_border)) {
        return false;
    }

    if (!param->reconfig) {
        conf_set_str(param->conf, CONF_host, host);
    }
    param->conf->fontspec.height = fontspec_height;
    conf_set_int(param->conf, CONF_resize_action, control_id_to_resize_action(resize_control_id));
    conf_set_int(param->conf, CONF_width, width);
    conf_set_int(param->conf, CONF_height, height);
    conf_set_bool(param->conf, CONF_scrollbar, get_conf_dialog_bool(hwnd, IDC_CONF_SCROLLBAR));
    conf_set_bool(param->conf, CONF_scrollbar_in_fullscreen, get_conf_dialog_bool(hwnd, IDC_CONF_SCROLLBAR_FULLSCREEN));
    conf_set_int(param->conf, CONF_window_border, window_border);
    conf_set_bool(param->conf, CONF_sunken_edge, get_conf_dialog_bool(hwnd, IDC_CONF_SUNKEN_EDGE));

    if (!param->reconfig && !conf_launchable(param->conf)) {
        return false;
    }

    if (param->reconfig || *session_name) {
        *param->session_name = strdup(session_name);
    }
    return true;
 }

static INT_PTR CALLBACK ConfDlgProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        ConfDlgParam *param = (ConfDlgParam *)lParam;
        SetWindowLongPtr(hwnd, DWLP_USER, lParam);
        init_conf_dialog_controls(hwnd, param);
        return TRUE;
      }

      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK: {
            ConfDlgParam *param = (ConfDlgParam *)GetWindowLongPtr(hwnd, DWLP_USER);
            if (save_conf_dialog_controls(hwnd, param)) {
                EndDialog(hwnd, true);
            }
            return TRUE;
          }

          case IDCANCEL:
            EndDialog(hwnd, false);
            return TRUE;
        }
        break;

      case WM_CLOSE:
        EndDialog(hwnd, false);
        return TRUE;
    }

    return FALSE;
}

bool do_config_pp(HWND hwnd, Conf *conf, const char **session_name) {
  assert(session_name && *session_name == NULL);
  ConfDlgParam param = {conf, session_name, false};

  return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_CONF), hwnd,
                        ConfDlgProc, (LPARAM)&param) == true;
}

bool do_reconfig_pp(HWND hwnd, Conf *conf, const char **session_name, int protcfginfo)
{
    assert(session_name && *session_name);
    ConfDlgParam param = {conf, session_name, true};

    return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_CONF), hwnd,
                          ConfDlgProc, (LPARAM)&param) == true;
}

static void init_term_remote_dialog_controls(HWND hwnd)
{
    SetDlgItemInt(hwnd, IDC_TERM_REMOTE_WIDTH, 80, TRUE);
    SetDlgItemInt(hwnd, IDC_TERM_REMOTE_HEIGHT, 24, TRUE);
    SetDlgItemInt(hwnd, IDC_TERM_REMOTE_X, 0, TRUE);
    SetDlgItemInt(hwnd, IDC_TERM_REMOTE_Y, 0, TRUE);
    set_conf_dialog_bool(hwnd, IDC_TERM_REMOTE_DELAYED, false);
}

static void end_term_remote_dialog(HWND hwnd, TermRemoteResult *result,
                                   TermRemoteAction action)
{
    result->action = action;
    result->delayed = get_conf_dialog_bool(hwnd, IDC_TERM_REMOTE_DELAYED);
    EndDialog(hwnd, TRUE);
}

static INT_PTR CALLBACK TermRemoteDlgProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG: {
        SetWindowLongPtr(hwnd, DWLP_USER, lParam);
        init_term_remote_dialog_controls(hwnd);
        return TRUE;
      }

      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_TERM_REMOTE_RESIZE: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            int width = 0, height = 0;

            if (!read_conf_dialog_int(hwnd, IDC_TERM_REMOTE_WIDTH, L"Width",
                                      &width) ||
                !read_conf_dialog_int(hwnd, IDC_TERM_REMOTE_HEIGHT, L"Height",
                                      &height)) {
                return TRUE;
            }
            result->param.resize.width = width;
            result->param.resize.height = height;
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_RESIZE);
            return TRUE;
          }

          case IDC_TERM_REMOTE_MAXIMISE: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_MAXIMISE);
            return TRUE;
          }

          case IDC_TERM_REMOTE_RESTORE: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_RESTORE);
            return TRUE;
          }

          case IDC_TERM_REMOTE_MINIMISE: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_MINIMISE);
            return TRUE;
          }

          case IDC_TERM_REMOTE_MOVE: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            int x = 0, y = 0;

            if (!read_conf_dialog_int(hwnd, IDC_TERM_REMOTE_X, L"X", &x) ||
                !read_conf_dialog_int(hwnd, IDC_TERM_REMOTE_Y, L"Y", &y)) {
                return TRUE;
            }
            result->param.move.x = x;
            result->param.move.y = y;
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_MOVE);
            return TRUE;
          }

          case IDC_TERM_REMOTE_Z_TOP: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_Z_TOP);
            return TRUE;
          }

          case IDC_TERM_REMOTE_Z_BOTTOM: {
            TermRemoteResult *result =
                (TermRemoteResult *)GetWindowLongPtr(hwnd, DWLP_USER);
            end_term_remote_dialog(hwnd, result, TERM_REMOTE_Z_BOTTOM);
            return TRUE;
          }

          case IDCANCEL:
            EndDialog(hwnd, FALSE);
            return TRUE;
        }
        break;

      case WM_CLOSE:
        EndDialog(hwnd, FALSE);
        return TRUE;
    }

    return FALSE;
}

bool do_term_remote(HWND hwnd, TermRemoteResult *result)
{
    return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_TERM_REMOTE), hwnd,
                          TermRemoteDlgProc, (LPARAM)result) == TRUE;
}
