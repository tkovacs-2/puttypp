#include "finddlg.h"
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

static const WCHAR dlg_class_name[] = L"FindDialog";

static HWND finddlg_hwnd = NULL;
static int frame_top_offset = 0;
static bool disable_notification = false;
static HFONT finddlg_hfont = NULL;
static HWND last_focus = NULL;

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
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, {146, 1, 18, 14}
    },
    {
        IDC_FINDDLG_DOWN, L"BUTTON", L"D",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, {164, 1, 18, 14}
    },
    {
        IDC_FINDDLG_CLOSE, L"BUTTON", L"x",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, {4, 1, 16, 14}
    },
    {
        IDC_FINDDLG_IGNORE_CASE, L"BUTTON", L"Ignore case",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, {24, 17, 60, 10}
    },
    {
        IDC_FINDDLG_WHOLE_WORD, L"BUTTON", L"Whole word",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, {86, 17, 60, 10}
    },
};

static const int dlg_control_count = sizeof(dlg_controls) / sizeof(dlg_controls[0]);
static const DlgRect dlg_rect = {0, 0, 186, 28};
static const int dlg_font_size = 8;
static const WCHAR *dlg_font_name = L"MS Shell Dlg";

static void make_font(void)
{
    assert(finddlg_hfont == NULL);
    finddlg_hfont = CreateFontW(-MulDiv(dlg_font_size, dpi_info.y, 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, dlg_font_name);
}

static void get_base_units(SIZE *base_units)
{
    HDC hdc = GetDC(frame_hwnd);
    HFONT old_font = SelectObject(hdc, finddlg_hfont);
    SIZE text_size;

    GetTextExtentPoint32W(
        hdc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
        52, &text_size);

    base_units->cx = (text_size.cx / 26 + 1) / 2;
    base_units->cy = text_size.cy;

    SelectObject(hdc, old_font);
    ReleaseDC(frame_hwnd, hdc);
}

static int dlu_scale_x(int dlu, const SIZE *base_units)
{
    return MulDiv(dlu, base_units->cx, 4);
}

static int dlu_scale_y(int dlu, const SIZE *base_units)
{
    return MulDiv(dlu, base_units->cy, 8);
}

static DlgRect dlu_scale_rect(const DlgRect *dlu_rect, const SIZE *base_units)
{
    DlgRect pixel_rect;

    pixel_rect.x = dlu_scale_x(dlu_rect->x, base_units);
    pixel_rect.y = dlu_scale_y(dlu_rect->y, base_units);
    pixel_rect.width = dlu_scale_x(dlu_rect->width, base_units);
    pixel_rect.height = dlu_scale_y(dlu_rect->height, base_units);
    return pixel_rect;
}

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

static void apply_alpha(HWND hwnd, bool active)
{
    BYTE alpha = active ? (BYTE)255 : (BYTE)128;
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

static void notify_frame(HWND hwnd, UINT code)
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

static LRESULT CALLBACK finddlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ACTIVATE:
        apply_alpha(hwnd, LOWORD(wParam) != WA_INACTIVE);
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND focus = GetFocus();
            if (focus && IsChild(hwnd, focus)) {
                last_focus = focus;
            }
        }
        return FALSE;
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
        last_focus = NULL;
        DeleteObject(finddlg_hfont);
        finddlg_hfont = NULL;
        return 0;
    case WM_DPICHANGED:
        return 0;
    case WM_SETFOCUS:
        if (!last_focus) {
            last_focus = GetDlgItem(hwnd, IDC_FINDDLG_EDIT);
        }
        SetFocus(last_focus);
        return FALSE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void register_finddlg_class()
{
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
    wc.lpszClassName = dlg_class_name;

    RegisterClassW(&wc);
    registered = true;
}

static HWND create_dialog() {
    register_finddlg_class();

    make_font();

    SIZE base_units;
    get_base_units(&base_units);

    DlgRect dr = dlu_scale_rect(&dlg_rect, &base_units);
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED, dlg_class_name, L"", WS_POPUP,
        dr.x, dr.y, dr.width, dr.height, frame_hwnd, NULL, hinst, NULL);
    apply_alpha(hwnd, GetForegroundWindow() == hwnd);
    for (int i = 0; i < dlg_control_count; i++) {
        const DlgControl *control = &dlg_controls[i];
        dr = dlu_scale_rect(&control->rect, &base_units);
        HWND child_hwnd = CreateWindowExW(
            control->exstyle, control->class_name, control->text,
            control->style, dr.x, dr.y, dr.width, dr.height,
            hwnd, (HMENU)(INT_PTR)control->id, hinst, NULL);
        SendMessageW(child_hwnd, WM_SETFONT, (WPARAM)finddlg_hfont, TRUE);
    }
    pin_to_frame(hwnd);
    return hwnd;
}

void finddlg_create(WCHAR *pattern, bool activate, bool ignore_case, bool whole_word)
{
    if (finddlg_hwnd == NULL) {
        finddlg_hwnd = create_dialog();
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

void finddlg_relayout_to_dpi()
{
    if (finddlg_hwnd == NULL) {
        return;
    }
    HWND hwnd = finddlg_hwnd;
    HFONT old_font = finddlg_hfont;
    finddlg_hfont = NULL;
    make_font();

    SIZE base_units;
    get_base_units(&base_units);

    DlgRect dr = dlu_scale_rect(&dlg_rect, &base_units);
    SetWindowPos(hwnd, NULL, 0, 0, dr.width, dr.height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    HDWP hdwp = BeginDeferWindowPos(dlg_control_count);
    for (int i = 0; i < dlg_control_count; i++) {
        const DlgControl *control = &dlg_controls[i];
        HWND child_hwnd = GetDlgItem(hwnd, control->id);
        SendMessageW(child_hwnd, WM_SETFONT, (WPARAM)finddlg_hfont, FALSE);
        dr = dlu_scale_rect(&control->rect, &base_units);
        hdwp = DeferWindowPos(
            hdwp, child_hwnd, NULL, dr.x, dr.y, dr.width, dr.height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    EndDeferWindowPos(hdwp);
    InvalidateRect(hwnd, NULL, TRUE);
    DeleteObject(old_font);
}
