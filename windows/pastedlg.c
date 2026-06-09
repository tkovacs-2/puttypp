#include <stdbool.h>
#include <windows.h>
#include "anchor.h"
#include "pastedlg_res.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;
void init_window_dpi_info(HWND hwnd, POINT *dialog_dpi_info);

static AnchorInfo  anchor_info[3] = {
    {IDC_TEXT, ANCHOR_BOTTOM_RIGHT, OP_SIZE},
    {IDOK, ANCHOR_BOTTOM_MIDDLE, OP_MOVE},
    {IDCANCEL, ANCHOR_BOTTOM_MIDDLE, OP_MOVE}
};

static SIZE dialog_size = {0, 0};
static POINT dialog_dpi_info;
static HWND text_hwnd = NULL;
static HFONT text_hfont = NULL;

typedef struct {
    const RECT *screen_size;
    wchar_t *text;
    size_t text_length;
} InitData;

static void set_text_font() {
    HFONT default_hfont = (HFONT)SendMessage(text_hwnd, WM_GETFONT, 0, 0);
    LOGFONT lf;

    GetObject(default_hfont, sizeof(LOGFONT), &lf);
    lf.lfHeight = MulDiv(lf.lfHeight, dialog_dpi_info.y, 96);
    text_hfont = CreateFont(lf.lfHeight, 0, 0, 0, FW_DONTCARE, false, false, false,
                                 lf.lfCharSet, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, lf.lfQuality,
                                 FIXED_PITCH | FF_DONTCARE, "Courier New");
    SendMessage(text_hwnd, WM_SETFONT, (WPARAM)text_hfont, (LPARAM)FALSE);
}

static void delete_text_font() {
    DeleteObject(text_hfont);
}

static void push_into_interval(int *start, int *size, int min, int max) {
   if (min+*size > max) {
        *start = min;
        *size = max-min;
    }
    else {
        if (*start < min) {
            *start = min;
        }
        else if (*start+*size > max) {
            *start = max-*size;
        }
    }
}

static void set_inital_size(HWND hwnd, const RECT *ss) {
    int w;
    int h;
    if (dialog_size.cx == 0 || dialog_size.cy == 0) {
        RECT r;
        GetWindowRect(hwnd, &r);
        w = r.right-r.left;
        h = r.bottom-r.top;
    }
    else {
        w = MulDiv(dialog_size.cx, dialog_dpi_info.x, 96);
        h = MulDiv(dialog_size.cy, dialog_dpi_info.y, 96);
    }
    RECT r;
    GetWindowRect(GetParent(hwnd), &r);
    int x = (r.right+r.left-w)/2;
    int y = (r.bottom+r.top-h)/2;
    push_into_interval(&x, &w, ss->left, ss->right);
    push_into_interval(&y, &h, ss->top, ss->bottom);
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);
}

static void save_size(HWND hwnd) {
    RECT r;
    GetWindowRect(hwnd, &r);
    dialog_size.cx = MulDiv(r.right-r.left, 96, dialog_dpi_info.x);
    dialog_size.cy = MulDiv(r.bottom-r.top, 96, dialog_dpi_info.y);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static INT_PTR ClipProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG: {
        dialog_dpi_info.x = 0;
        init_window_dpi_info(hwnd, &dialog_dpi_info);
        SetWindowLongPtr(hwnd, DWLP_USER, lParam);
        anchor_init(hwnd, &dialog_dpi_info, anchor_info, 3);
        InitData *init_data = (InitData *)lParam;
        set_inital_size(hwnd, init_data->screen_size);
        text_hwnd = GetDlgItem(hwnd, IDC_TEXT);
        set_text_font();
        SetWindowTextW(text_hwnd, init_data->text);
        free(init_data->text);
        init_data->text = NULL;
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK: {
            InitData *init_data = (InitData *)GetWindowLongPtr(hwnd, DWLP_USER);
            init_data->text_length = GetWindowTextLengthW(text_hwnd);
            init_data->text = malloc((init_data->text_length+1)*sizeof(wchar_t));
            GetWindowTextW(text_hwnd, init_data->text, init_data->text_length+1);
            save_size(hwnd);
            EndDialog(hwnd, true);
            delete_text_font();
            break;
          }
          case IDCANCEL:
            save_size(hwnd);
            EndDialog(hwnd, false);
            delete_text_font();
            return 0;
        }
        return 0;
      case WM_CLOSE:
        save_size(hwnd);
        EndDialog(hwnd, false);
        delete_text_font();
        return 0;
      case WM_SIZE:
        anchor_apply(hwnd, anchor_info, 3);
        return 0;
      case WM_DPICHANGED:
        dialog_dpi_info.x = LOWORD(wParam);
        dialog_dpi_info.y = HIWORD(wParam);
        delete_text_font();
        set_text_font();
        anchor_change_dpi(&dialog_dpi_info, anchor_info, 3);
        anchor_apply(hwnd, anchor_info, 3);
        return 0;
    }
    return 0;
}

bool show_paste_confirm(const RECT *screen_size, wchar_t **text, size_t *text_length) {
    InitData init_data = {screen_size, *text, 0};
    bool r = DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_PASTE_CONFIRM), frame_hwnd, (DLGPROC)ClipProc, (LPARAM)&init_data);
    *text = init_data.text;
    *text_length = init_data.text_length;
    return r;
}
