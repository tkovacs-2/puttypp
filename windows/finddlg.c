#include "finddlg.h"
#include "finddlg_res.h"
#include "anchor.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;
void init_window_dpi_info(HWND hwnd, POINT *dpi_info);

static POINT dialog_dpi_info;
static HWND finddlg_hwnd = NULL;
static bool in_sizemove = false;
static int zoomed_size = 0;
static int normal_size = 0;
static int initial_size = 0;
static int zoomed_size_dpi = 0;
static int normal_size_dpi = 0;
static int initial_size_dpi = 0;
static bool wm_size_pin = false;
static int frame_top_offset = 0;
static bool disable_notification = false;

static AnchorInfo anchor_info[] = {
    {IDC_FINDDLG_EDIT, ANCHOR_TOP_RIGHT, OP_SIZE},
    {IDC_FINDDLG_UP, ANCHOR_TOP_RIGHT, OP_MOVE},
    {IDC_FINDDLG_DOWN, ANCHOR_TOP_RIGHT, OP_MOVE}
};
const int anchor_info_size = sizeof(anchor_info) / sizeof(anchor_info[0]);

static void pin_to_frame(HWND hwnd)
{
    RECT r;
    POINT pin_to, frame_left;

    GetClientRect(frame_hwnd, &r);
    pin_to.x = r.right;
    pin_to.y = r.top + frame_top_offset;
    ClientToScreen(frame_hwnd, &pin_to);

    frame_left.x = 0;
    frame_left.y = 0;
    ClientToScreen(frame_hwnd, &frame_left);

    GetWindowRect(hwnd, &r);
    int dlg_size = r.right - r.left;
    int dlg_left = pin_to.x - dlg_size;
    if (dlg_left < frame_left.x) {
        dlg_left = frame_left.x;
    }
    SetWindowPos(hwnd, NULL, dlg_left, pin_to.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void size_to_frame(HWND hwnd)
{
    int size = (IsZoomed(frame_hwnd) && zoomed_size_dpi > 0) ? zoomed_size_dpi : normal_size_dpi;
    RECT r;
    GetClientRect(frame_hwnd, &r);
    int frame_size = r.right - r.left;
    if (size > frame_size) {
        size = frame_size;
    }
    GetWindowRect(hwnd, &r);
    wm_size_pin = false;
    SetWindowPos(hwnd, NULL, 0, 0, size, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (wm_size_pin) {
        wm_size_pin = false;
    } else {
        pin_to_frame(hwnd);
    }
}

static int dpi_scale(int x)
{
    return MulDiv(x, dialog_dpi_info.x, 96);
};

static int dpi_inverse_scale(int x)
{
    return MulDiv(x, 96, dialog_dpi_info.x);
};

static void store_initial_size(HWND hwnd)
{
    RECT r;
    GetWindowRect(hwnd, &r);
    initial_size_dpi = r.right - r.left;
    initial_size = dpi_inverse_scale(initial_size_dpi);
    if (normal_size > 0) {
        normal_size_dpi = dpi_scale(normal_size);
    } else {
        normal_size_dpi = initial_size_dpi;
        normal_size = initial_size;
    }
    if (zoomed_size > 0) {
        zoomed_size_dpi = dpi_scale(zoomed_size);
    }
}

static void store_size(HWND hwnd) {
    RECT r;
    GetWindowRect(hwnd, &r);
    int size_dpi = r.right - r.left;
    int size = dpi_inverse_scale(size_dpi);
    if (IsZoomed(frame_hwnd)) {
        zoomed_size_dpi = size_dpi;
        zoomed_size = size;
    } else {
        normal_size_dpi = size_dpi;
        normal_size = size;
    }
}

static void apply_alpha(HWND hwnd, bool active)
{
    BYTE alpha = active ? (BYTE)255 : (BYTE)128;
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

static void draw_grip(HDC hdc, const RECT *rcItem)
{
    int dot = dpi_scale(2);
    int pitch = 2*dot;
    int shift = dpi_scale(1);
    int marginx = shift;
    int marginy = ((rcItem->bottom - rcItem->top)%pitch)/2;
    HBRUSH face = GetSysColorBrush(COLOR_3DFACE);
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HBRUSH shadow = GetSysColorBrush(COLOR_3DSHADOW);

    FillRect(hdc, rcItem, face);
    int x0 = rcItem->left + marginx;
    int y0 = rcItem->top + marginy;

    RECT wr, sr;

    sr.left = x0;
    sr.top = y0;
    sr.right = sr.left + dot;
    sr.bottom = sr.top + dot;

    wr.left = x0 + shift;
    wr.top = y0 + shift;
    wr.right = wr.left + dot;
    wr.bottom = wr.top + dot;

    while (wr.bottom <= rcItem->bottom) {

        while (wr.right <= rcItem->right) {
            FillRect(hdc, &wr, white);
            FillRect(hdc, &sr, shadow);
            wr.left  += pitch;
            wr.right += pitch;
            sr.left += pitch;
            sr.right += pitch;
        }
        wr.top  += pitch;
        wr.bottom += pitch;
        sr.top += pitch;
        sr.bottom += pitch;
        wr.left = x0 + shift;
        wr.right = wr.left + dot;
        sr.left = x0;
        sr.right = sr.left + dot;
    }
}

void notify_frame(HWND hwnd, UINT code)
{
    if (disable_notification) {
        return;
    }
    NMHDR nm;
    nm.hwndFrom = hwnd;
    nm.idFrom = FINDDLG_NOTIFY_ID;
    nm.code = code;
    SendMessage(frame_hwnd, WM_NOTIFY, (WPARAM)nm.idFrom, (LPARAM)&nm);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static INT_PTR CALLBACK finddlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        apply_alpha(hwnd, GetForegroundWindow() == hwnd);
        dialog_dpi_info.x = 0;
        init_window_dpi_info(hwnd, &dialog_dpi_info);
        anchor_init(hwnd, &dialog_dpi_info, anchor_info, anchor_info_size);
        store_initial_size(hwnd);
        size_to_frame(hwnd);
        return FALSE;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = initial_size_dpi/2;
        return TRUE;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
        switch (hit) {
        case HTTOPLEFT:
        case HTBOTTOMLEFT:
            hit = HTLEFT;
            break;
        case HTRIGHT:
        case HTTOP:
        case HTBOTTOM:
        case HTTOPRIGHT:
        case HTBOTTOMRIGHT:
            hit = HTCLIENT;
            break;
        default:
            break;
        }
        SetWindowLongPtr(hwnd, DWLP_MSGRESULT, (LONG_PTR)hit);
        return TRUE;
    }
    case WM_ENTERSIZEMOVE:
        in_sizemove = true;
        return FALSE;
    case WM_EXITSIZEMOVE:
        in_sizemove = false;
        store_size(hwnd);
        pin_to_frame(hwnd);
        return FALSE;
    case WM_SIZE:
        anchor_apply(hwnd, anchor_info, anchor_info_size);
        if (in_sizemove) {
            return FALSE;
        }
        wm_size_pin = true;
        pin_to_frame(hwnd);
        return FALSE;
    case WM_ACTIVATE:
        apply_alpha(hwnd, LOWORD(wParam) != WA_INACTIVE);
        return FALSE;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_STATIC && dis->CtlID == IDC_FINDDLG_GRIP) {
            draw_grip(dis->hDC, &dis->rcItem);
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_FINDDLG_EDIT:
            if (HIWORD(wParam) == EN_CHANGE) {
                notify_frame(hwnd, FINDDLG_EDIT_CHANGED);
                return TRUE;
            }
            break;
        case IDOK:
            notify_frame(hwnd, (GetKeyState(VK_SHIFT) & 0x8000) ?
                         FINDDLG_DOWN : FINDDLG_EDIT_ENTER);
            return TRUE;
        case IDC_FINDDLG_UP:
            notify_frame(hwnd, FINDDLG_UP);
            return TRUE;
        case IDC_FINDDLG_DOWN:
            notify_frame(hwnd, FINDDLG_DOWN);
            return TRUE;
        case IDC_FINDDLG_IGNORE_CASE:
            if (HIWORD(wParam) == BN_CLICKED) {
                notify_frame(hwnd, FINDDLG_IGNORE_CASE);
                return TRUE;
            }
            break;
        case IDC_FINDDLG_WHOLE_WORD:
            if (HIWORD(wParam) == BN_CLICKED) {
                notify_frame(hwnd, FINDDLG_WHOLE_WORD);
                return TRUE;
            }
            break;
        case IDCANCEL:
        case IDC_FINDDLG_CLOSE:
            notify_frame(hwnd, FINDDLG_CLOSE);
            DestroyWindow(hwnd);
            return TRUE;
        default:
            break;
        }
        return FALSE;
    case WM_NCDESTROY:
        finddlg_hwnd = NULL;
        return FALSE;
    case WM_DPICHANGED:
        dialog_dpi_info.x = LOWORD(wParam);
        dialog_dpi_info.y = HIWORD(wParam);
        anchor_change_dpi(&dialog_dpi_info, anchor_info, anchor_info_size);
        anchor_apply(hwnd, anchor_info, anchor_info_size);
        zoomed_size_dpi = dpi_scale(zoomed_size);
        normal_size_dpi = dpi_scale(normal_size);
        initial_size_dpi = dpi_scale(initial_size);
        return FALSE;
    default:
        return FALSE;
    }
}

void finddlg_create(WCHAR *pattern, bool activate, bool ignore_case, bool whole_word)
{
    if (finddlg_hwnd == NULL) {
        finddlg_hwnd = CreateDialog(hinst, MAKEINTRESOURCE(IDD_FINDDLG), frame_hwnd, finddlg_proc);
    }
    disable_notification = true;
    SetWindowTextW(GetDlgItem(finddlg_hwnd, IDC_FINDDLG_EDIT), pattern);
    CheckDlgButton(finddlg_hwnd, IDC_FINDDLG_IGNORE_CASE,
                   ignore_case ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(finddlg_hwnd, IDC_FINDDLG_WHOLE_WORD,
                   whole_word ? BST_CHECKED : BST_UNCHECKED);
    disable_notification = false;
    if (IsWindowVisible(finddlg_hwnd)) {
        if (activate) {
            SetActiveWindow(finddlg_hwnd);
        }
    } else if (activate) {
        ShowWindow(finddlg_hwnd, SW_SHOW);
    } else {
        ShowWindow(finddlg_hwnd, SW_SHOWNA);
    }
}

void finddlg_destroy(void)
{
    if (finddlg_hwnd == NULL) {
        return;
    }
    DestroyWindow(finddlg_hwnd);
}

void finddlg_pin_to_frame(int top_offset)
{
    frame_top_offset = top_offset;
    if (finddlg_hwnd == NULL) {
        return;
    }
    pin_to_frame(finddlg_hwnd);
}

void finddlg_size_to_frame(int top_offset)
{
    frame_top_offset = top_offset;
    if (finddlg_hwnd == NULL) {
        return;
    }
    size_to_frame(finddlg_hwnd);
}

int finddlg_get_text(WCHAR *buffer, int buffer_chars)
{
    if (finddlg_hwnd == NULL) {
        return 0;
    }
    HWND hedit = GetDlgItem(finddlg_hwnd, IDC_FINDDLG_EDIT);
    if (buffer == NULL) {
        return GetWindowTextLengthW(hedit);
    }
    return GetWindowTextW(hedit, buffer, buffer_chars);
}

bool finddlg_get_ignore_case()
{
    if (finddlg_hwnd == NULL) {
        return false;
    }
    return IsDlgButtonChecked(finddlg_hwnd, IDC_FINDDLG_IGNORE_CASE) == BST_CHECKED;
}

bool finddlg_get_whole_word()
{
    if (finddlg_hwnd == NULL) {
        return false;
    }
    return IsDlgButtonChecked(finddlg_hwnd, IDC_FINDDLG_WHOLE_WORD) == BST_CHECKED;
}

bool finddlg_is_dialog_message(MSG *msg)
{
    return (finddlg_hwnd && IsDialogMessageW(finddlg_hwnd, msg));
}
