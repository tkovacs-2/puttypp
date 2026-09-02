#include "finddlg.h"
#include "anchor.h"
#include <assert.h>

#define IDC_FINDDLG_EDIT 1001
#define IDC_FINDDLG_UP 1002
#define IDC_FINDDLG_DOWN 1003
#define IDC_FINDDLG_CLOSE 1004
#define IDC_FINDDLG_GRIP 1005
#define IDC_FINDDLG_IGNORE_CASE 1006
#define IDC_FINDDLG_WHOLE_WORD 1007

extern HINSTANCE hinst;
extern HWND frame_hwnd;
extern POINT dpi_info;

static const WCHAR FIND_DLG_CLASS_NAME[] = L"FindDialog";
static FindDlg *active_finddlg = NULL;

typedef enum AdjustState {
    ADJUST_STATE_NORMAL,
    ADJUST_STATE_COMPACT,
    ADJUST_STATE_HIDDEN
} AdjustState;

typedef struct DlgRect {
    int x;
    int y;
    int width;
    int height;
} DlgRect;

typedef struct DlgControl {
    int id;
    const WCHAR *class_name;
    const WCHAR *text;
    DWORD style;
    DWORD exstyle;
    DlgRect rect;
} DlgControl;

static const DlgControl dlg_controls[] = {
    {
        IDC_FINDDLG_EDIT, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        WS_EX_CLIENTEDGE, {24, 1, 118, 14}
    },
    {
        IDC_FINDDLG_UP, L"BUTTON", L"U",
        WS_CHILD | WS_VISIBLE| WS_TABSTOP | BS_PUSHBUTTON,
        0, {146, 1, 18, 14}
    },
    {
        IDC_FINDDLG_DOWN, L"BUTTON", L"D",
        WS_CHILD | WS_VISIBLE| WS_TABSTOP | BS_PUSHBUTTON,
        0, {164, 1, 18, 14}
    },
    {
        IDC_FINDDLG_CLOSE, L"BUTTON", L"x",
        WS_CHILD | WS_VISIBLE| WS_TABSTOP | BS_PUSHBUTTON,
        0, {4, 1, 16, 14}
    },
    {
        IDC_FINDDLG_IGNORE_CASE, L"BUTTON", L"Ignore case",
        WS_CHILD | WS_VISIBLE| WS_TABSTOP | BS_AUTOCHECKBOX,
        0, {24, 17, 60, 10}
    },
    {
        IDC_FINDDLG_WHOLE_WORD, L"BUTTON", L"Whole word",
        WS_CHILD | WS_VISIBLE| WS_TABSTOP | BS_AUTOCHECKBOX,
        0, {86, 17, 60, 10}
    },
};

static const int dlg_control_count = sizeof(dlg_controls) / sizeof(dlg_controls[0]);
static const DlgRect dlg_rect = {0, 0, 186, 28};
static const int dlg_font_size = 8;
static const WCHAR *dlg_font_name = L"MS Shell Dlg";
static const DlgRect dlg_compact_rect = {0, 0, 93, 16};
static const DlgRect dlg_compact_edit_rect = {4, 1, 85, 14};

static AnchorInfo anchor_info[] = {
    {IDC_FINDDLG_EDIT, ANCHOR_TOP_RIGHT, OP_SIZE},
    {IDC_FINDDLG_UP, ANCHOR_TOP_RIGHT, OP_MOVE},
    {IDC_FINDDLG_DOWN, ANCHOR_TOP_RIGHT, OP_MOVE}
};
const int anchor_info_size = sizeof(anchor_info) / sizeof(anchor_info[0]);

static void set_font(FindDlg *finddlg) {
    assert(finddlg->hfont == NULL);
    finddlg->hfont = CreateFontW(-MulDiv(dlg_font_size, dpi_info.y, 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, dlg_font_name);
}

static void set_base_units(FindDlg *finddlg) {
    HDC hdc = GetDC(frame_hwnd);
    HFONT old_font = SelectObject(hdc, finddlg->hfont);
    SIZE text_size;

    GetTextExtentPoint32W(
        hdc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        52, &text_size);

    finddlg->base_units.cx = (text_size.cx / 26 + 1) / 2;
    finddlg->base_units.cy = text_size.cy;

    SelectObject(hdc, old_font);
    ReleaseDC(frame_hwnd, hdc);
}

static int dlu_scale_x(int dlu, const SIZE *base_units) {
    return MulDiv(dlu, base_units->cx, 4);
}

static int dlu_scale_y(int dlu, const SIZE *base_units) {
    return MulDiv(dlu, base_units->cy, 8);
}

static DlgRect dlu_scale_rect(const DlgRect *dlu_rect, const SIZE *base_units) {
    DlgRect pixel_rect;

    pixel_rect.x = dlu_scale_x(dlu_rect->x, base_units);
    pixel_rect.y = dlu_scale_y(dlu_rect->y, base_units);
    pixel_rect.width = dlu_scale_x(dlu_rect->width, base_units);
    pixel_rect.height = dlu_scale_y(dlu_rect->height, base_units);
    return pixel_rect;
}

static void apply_alpha(HWND hwnd, bool active) {
    BYTE alpha = active ? (BYTE)255 : (BYTE)128;
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

static void notify_frame(FindDlg *finddlg, HWND hwnd, UINT code) {
    if (finddlg->disable_notification) {
        return;
    }
    NMHDR nm;
    nm.hwndFrom = hwnd;
    nm.idFrom = FINDDLG_NOTIFY_ID;
    nm.code = code;
    SendMessage(frame_hwnd, WM_NOTIFY, (WPARAM)nm.idFrom, (LPARAM)&nm);
}

static AdjustState adjust_window(FindDlg *finddlg, const RECT *parent_rect, POINT *pos, SIZE *size) {
    int parent_width = parent_rect->right - parent_rect->left;
    int parent_height = parent_rect->bottom - parent_rect->top;
    if (parent_width < finddlg->compact_window_size.cx / 4 || parent_height < finddlg->compact_window_size.cy) {
        return ADJUST_STATE_HIDDEN;
    }

    AdjustState state = ADJUST_STATE_NORMAL;
    *size = finddlg->window_size;
    int x = parent_rect->right - size->cx;
    if (x < parent_rect->left) {
        x = parent_rect->left;
        if (x + size->cx > parent_rect->right) {
            size->cx = parent_rect->right - x;
        }
    }
    if (size->cx < finddlg->window_size.cx / 2) {
        state = ADJUST_STATE_COMPACT;
        size->cy = finddlg->compact_window_size.cy;
    }
    if (parent_rect->top + size->cy > parent_rect->bottom) {
        state = ADJUST_STATE_COMPACT;
        size->cy = finddlg->compact_window_size.cy;
    }
    pos->x = x;
    pos->y = parent_rect->top;
    return state;
}

static void layout_controls(FindDlg *finddlg, bool compact) {
    if (finddlg->compact == compact) {
        return;
    }
    const SIZE *window_size = compact ? &finddlg->compact_window_size : &finddlg->window_size;
    const DlgRect *edit_rect = compact ? &dlg_compact_edit_rect : &dlg_controls[0].rect;
    const int show_flag = compact ? SWP_HIDEWINDOW : SWP_SHOWWINDOW;

    HWND hwnd = finddlg->hwnd;
    DlgRect dr = dlu_scale_rect(edit_rect, &finddlg->base_units);
    HDWP hdwp = BeginDeferWindowPos(dlg_control_count);
    hdwp = DeferWindowPos(
        hdwp, GetDlgItem(hwnd, IDC_FINDDLG_EDIT), NULL,
        dr.x, dr.y, dr.width, dr.height,
        SWP_NOZORDER | SWP_NOACTIVATE);
    for (int i = 0; i < dlg_control_count; i++) {
        const DlgControl *control = &dlg_controls[i];
        if (control->id == IDC_FINDDLG_EDIT) {
            continue;
        }
        hdwp = DeferWindowPos(
            hdwp, GetDlgItem(hwnd, control->id), NULL,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | show_flag);
    }
    EndDeferWindowPos(hdwp);
    RECT parent_rect = {0, 0, window_size->cx, window_size->cy};
    RECT rect = {dr.x, dr.y, dr.x + dr.width, dr.y + dr.height};
    anchor_preinit_item(&dpi_info, &anchor_info[0], &parent_rect, &rect);
}

static void layout_window(FindDlg *finddlg, const RECT *parent_rect, bool activate) {
    POINT pos;
    SIZE size;
    AdjustState state = adjust_window(finddlg, parent_rect, &pos, &size);
    int flags = SWP_NOZORDER;
    if (state == ADJUST_STATE_HIDDEN) {
        if (finddlg->hidden) {
            return;
        }
        finddlg->hidden = true;
        flags |= SWP_NOSIZE | SWP_NOMOVE | SWP_HIDEWINDOW | SWP_NOACTIVATE;
    } else {
        if (finddlg->hidden) {
            finddlg->hidden = false;
            flags |= SWP_SHOWWINDOW;
        }
        bool compact = (state == ADJUST_STATE_COMPACT);
        if (finddlg->compact != compact) {
            layout_controls(finddlg, compact);
            finddlg->compact = compact;
        }
        if (!activate) {
            flags |= SWP_NOACTIVATE;
        }
    }
    finddlg->pin_offset = pos;
    ClientToScreen(frame_hwnd, &pos);
    SetWindowPos(finddlg->hwnd, NULL, pos.x, pos.y, size.cx, size.cy, flags);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static LRESULT CALLBACK finddlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    FindDlg *finddlg = (FindDlg *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
      case WM_CREATE:
        finddlg = (FindDlg *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)finddlg);
        return TRUE;
      case WM_SIZE:
        anchor_apply(hwnd, anchor_info, anchor_info_size);
        return TRUE;
      case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE) {
            active_finddlg = NULL;
            apply_alpha(hwnd, false);
            HWND focus = GetFocus();
            if (focus && IsChild(hwnd, focus)) {
                finddlg->last_focus = focus;
            }
        } else {
            active_finddlg = finddlg;
            apply_alpha(hwnd, true);
        }
        return FALSE;
      case WM_COMMAND:
        switch (LOWORD(wparam)) {
          case IDC_FINDDLG_EDIT:
            if (HIWORD(wparam) == EN_CHANGE) {
                notify_frame(finddlg, hwnd, FINDDLG_EDIT_CHANGED);
                return TRUE;
            }
            break;
          case IDOK:
            notify_frame(finddlg, hwnd, (GetKeyState(VK_SHIFT) & 0x8000) ?
                         FINDDLG_DOWN : FINDDLG_EDIT_ENTER);
            return TRUE;
          case IDC_FINDDLG_UP:
            notify_frame(finddlg, hwnd, FINDDLG_UP);
            return TRUE;
          case IDC_FINDDLG_DOWN:
            notify_frame(finddlg, hwnd, FINDDLG_DOWN);
            return TRUE;
          case IDC_FINDDLG_IGNORE_CASE:
            if (HIWORD(wparam) == BN_CLICKED) {
                notify_frame(finddlg, hwnd, FINDDLG_IGNORE_CASE);
                return TRUE;
            }
            break;
          case IDC_FINDDLG_WHOLE_WORD:
            if (HIWORD(wparam) == BN_CLICKED) {
                notify_frame(finddlg, hwnd, FINDDLG_WHOLE_WORD);
                return TRUE;
            }
            break;
          case IDCANCEL:
          case IDC_FINDDLG_CLOSE:
            notify_frame(finddlg, hwnd, FINDDLG_CLOSE);
            DestroyWindow(hwnd);
            return TRUE;
          default:
            break;
        }
        return FALSE;
      case WM_NCDESTROY:
        finddlg->hwnd = NULL;
        finddlg->last_focus = NULL;
        DeleteObject(finddlg->hfont);
        finddlg->hfont = NULL;
        active_finddlg = NULL;
        return 0;
      case WM_DPICHANGED:
        return 0;
      case WM_SETFOCUS:
        if (!finddlg->last_focus) {
            finddlg->last_focus = GetDlgItem(hwnd, IDC_FINDDLG_EDIT);
        }
        SetFocus(finddlg->last_focus);
        return FALSE;
      case WM_SYSCOMMAND:
        if (wparam == SC_CLOSE) {
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void register_finddlg_class() {
    static bool registered = false;

    if (registered) {
        return;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = finddlg_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = FIND_DLG_CLASS_NAME;

    RegisterClassW(&wc);
    registered = true;
}

static HWND create_dialog(FindDlg *finddlg) {
    register_finddlg_class();

    set_font(finddlg);
    set_base_units(finddlg);

    DlgRect dr = dlu_scale_rect(&dlg_compact_rect, &finddlg->base_units);
    finddlg->compact_window_size.cx = dr.width;
    finddlg->compact_window_size.cy = dr.height;
    dr = dlu_scale_rect(&dlg_rect, &finddlg->base_units);
    finddlg->window_size.cx = dr.width;
    finddlg->window_size.cy = dr.height;
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED, FIND_DLG_CLASS_NAME, L"", WS_POPUP,
        dr.x, dr.y, dr.width, dr.height, frame_hwnd, NULL, hinst, finddlg);
    apply_alpha(hwnd, GetForegroundWindow() == hwnd);
    for (int i = 0; i < dlg_control_count; i++) {
        const DlgControl *control = &dlg_controls[i];
        dr = dlu_scale_rect(&control->rect, &finddlg->base_units);
        HWND child_hwnd = CreateWindowExW(
            control->exstyle, control->class_name, control->text,
            control->style, dr.x, dr.y, dr.width, dr.height,
            hwnd, (HMENU)(INT_PTR)control->id, hinst, NULL);
        SendMessageW(child_hwnd, WM_SETFONT, (WPARAM)finddlg->hfont, TRUE);
    }
    anchor_init(hwnd, &dpi_info, anchor_info, anchor_info_size);
    return hwnd;
}

void finddlg_init(FindDlg *finddlg, void *user_data) {
    memset(finddlg, 0, sizeof(FindDlg));
    finddlg->hidden = true;
    finddlg->user_data = user_data;
}

void finddlg_uninit(FindDlg *finddlg) {
    finddlg_hide(finddlg);
}

void finddlg_show(FindDlg *finddlg, const RECT *parent_rect, WCHAR *pattern, bool activate, bool ignore_case, bool whole_word) {
    if (finddlg->hwnd == NULL) {
        finddlg->hwnd = create_dialog(finddlg);
        finddlg->compact = false;
        finddlg->hidden = true;
    }
    layout_window(finddlg, parent_rect, activate);

    finddlg->disable_notification = true;
    SetWindowTextW(GetDlgItem(finddlg->hwnd, IDC_FINDDLG_EDIT), pattern);
    CheckDlgButton(finddlg->hwnd, IDC_FINDDLG_IGNORE_CASE,
                   ignore_case ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(finddlg->hwnd, IDC_FINDDLG_WHOLE_WORD,
                   whole_word ? BST_CHECKED : BST_UNCHECKED);
    finddlg->disable_notification = false;
}

void finddlg_hide(FindDlg *finddlg) {
    if (finddlg->hwnd == NULL) {
        return;
    }
    DestroyWindow(finddlg->hwnd);
}

void finddlg_adjust_window(FindDlg *finddlg, const RECT *parent_rect) {
    if (finddlg->hwnd == NULL) {
        return;
    }
    layout_window(finddlg, parent_rect, false);
}

HDWP finddlg_pin_window(FindDlg *finddlg, HDWP hdwp) {
    if (finddlg->hwnd == NULL || finddlg->hidden) {
        return hdwp;
    }
    POINT pos = finddlg->pin_offset;
    ClientToScreen(frame_hwnd, &pos);
    return DeferWindowPos(hdwp, finddlg->hwnd, NULL, pos.x, pos.y, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

int finddlg_get_text(FindDlg *finddlg, WCHAR *buffer, int buffer_chars) {
    if (finddlg->hwnd == NULL) {
        return 0;
    }
    HWND hedit = GetDlgItem(finddlg->hwnd, IDC_FINDDLG_EDIT);
    if (buffer == NULL) {
        return GetWindowTextLengthW(hedit);
    }
    return GetWindowTextW(hedit, buffer, buffer_chars);
}

bool finddlg_get_ignore_case(FindDlg *finddlg) {
    if (finddlg->hwnd == NULL) {
        return false;
    }
    return IsDlgButtonChecked(finddlg->hwnd, IDC_FINDDLG_IGNORE_CASE) == BST_CHECKED;
}

bool finddlg_get_whole_word(FindDlg *finddlg) {
    if (finddlg->hwnd == NULL) {
        return false;
    }
    return IsDlgButtonChecked(finddlg->hwnd, IDC_FINDDLG_WHOLE_WORD) == BST_CHECKED;
}

bool finddlg_is_dialog_message(MSG *msg) {
    return (active_finddlg && IsDialogMessageW(active_finddlg->hwnd, msg));
}

FindDlg *finddlg_get_from_hwnd(HWND hwnd) {
    return (FindDlg *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

void finddlg_dpi_changed(FindDlg *finddlg) {
    if (finddlg->hwnd == NULL) {
        return;
    }
    anchor_change_dpi(&dpi_info, anchor_info, anchor_info_size);

    HWND hwnd = finddlg->hwnd;
    HFONT old_font = finddlg->hfont;
    finddlg->hfont = NULL;
    set_font(finddlg);
    set_base_units(finddlg);

    DlgRect dr = dlu_scale_rect(&dlg_rect, &finddlg->base_units);
    finddlg->window_size.cx = dr.width;
    finddlg->window_size.cy = dr.height;
    dr = dlu_scale_rect(&dlg_compact_rect, &finddlg->base_units);
    finddlg->compact_window_size.cx = dr.width;
    finddlg->compact_window_size.cy = dr.height;

    const DlgRect *edit_rect = finddlg->compact ? &dlg_compact_edit_rect : &dlg_controls[0].rect;

    HDWP hdwp = BeginDeferWindowPos(dlg_control_count);
    for (int i = 0; i < dlg_control_count; i++) {
        const DlgControl *control = &dlg_controls[i];
        HWND child_hwnd = GetDlgItem(hwnd, control->id);
        SendMessageW(child_hwnd, WM_SETFONT, (WPARAM)finddlg->hfont, FALSE);
        dr = dlu_scale_rect((control->id == IDC_FINDDLG_EDIT ? edit_rect : &control->rect), &finddlg->base_units);
        hdwp = DeferWindowPos(
            hdwp, child_hwnd, NULL, dr.x, dr.y, dr.width, dr.height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    EndDeferWindowPos(hdwp);
    InvalidateRect(hwnd, NULL, TRUE);
    DeleteObject(old_font);
}
