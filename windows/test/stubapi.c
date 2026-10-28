/*
 * stubwinapi.c - In-process stubs for Win32 APIs used by the guitest harness.
 *
 * HWND-related calls maintain a logical window tree (geometry, styles, focus,
 * messaging). GDI calls only track object lifetimes for leak detection.
 *
 * Link this instead of user32/gdi32/comctl32 when building a fully stubbed test.
 */

#define DECLSPEC_IMPORT __declspec()

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>

static LRESULT CALLBACK stub_tab_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

WINUSERAPI BOOL WINAPI PostMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return TRUE;
}

WINUSERAPI void WINAPI PostQuitMessage(int code) {
}

WINUSERAPI DWORD WINAPI GetMessagePos(void) {
    return 0;
}

typedef struct StubHdwp {
    int count;
    int capacity;
} StubHdwp;

WINUSERAPI HDWP WINAPI BeginDeferWindowPos(int count) {
    StubHdwp *h = calloc(1, sizeof(*h));
    h->capacity = count > 0 ? count : 8;
    return (HDWP)h;
}

WINUSERAPI HDWP WINAPI DeferWindowPos(HDWP hdwp, HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    StubHdwp *h = (StubHdwp *)hdwp;
    if (h->count >= h->capacity) {
        int count = h->count;
        int capacity = h->capacity *= 2;
        StubHdwp *new = malloc(sizeof(StubHdwp));
        free(h);
        h = new;
        h->count = count;
        h->capacity = capacity;
    }
    SetWindowPos(hwnd, after, x, y, cx, cy, flags);
    h->count++;
    return (HDWP)h;
}

WINUSERAPI BOOL WINAPI EndDeferWindowPos(HDWP hdwp) {
    StubHdwp *h = (StubHdwp *)hdwp;
    free(h);
    return TRUE;
}


WINUSERAPI BOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetCursorPos(LPPOINT pt) {
    pt->x = 100;
    pt->y = 100;
    return TRUE;
}

WINUSERAPI HCURSOR WINAPI SetCursor(HCURSOR c) {
    return c;
}


static HWND stub_caret_hwnd;
static HBITMAP stub_caret_bm;
static int stub_caret_w, stub_caret_h;
static int stub_caret_x, stub_caret_y;
static bool stub_caret_visible;

WINUSERAPI BOOL WINAPI CreateCaret(HWND hwnd, HBITMAP bm, int w, int h) {
    stub_caret_hwnd = hwnd;
    stub_caret_bm = bm;
    stub_caret_w = w;
    stub_caret_h = h;
    stub_caret_visible = false;
    return TRUE;
}

WINUSERAPI BOOL WINAPI DestroyCaret(void) {
    stub_caret_hwnd = NULL;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ShowCaret(HWND) {
    stub_caret_visible = true;
    return TRUE;
}

WINUSERAPI BOOL WINAPI HideCaret(HWND) {
    stub_caret_visible = false;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetCaretPos(int x, int y) {
    stub_caret_x = x;
    stub_caret_y = y;
    return TRUE;
}

/* Dialog controls */
WINUSERAPI BOOL WINAPI CheckDlgButton(HWND dlg, int id, UINT state) {
    HWND c = GetDlgItem(dlg, id);
    if (!c) {
        return FALSE;
    }
    SendMessageA(c, BM_SETCHECK, state, 0);
    return TRUE;
}

WINUSERAPI UINT WINAPI IsDlgButtonChecked(HWND dlg, int id) {
    HWND c = GetDlgItem(dlg, id);
    if (!c) {
        return BST_UNCHECKED;
    }
    return (UINT)SendMessageA(c, BM_GETCHECK, 0, 0);
}

static INT_PTR stub_modal_result;
static bool stub_modal_done;

WINUSERAPI BOOL WINAPI EndDialog(HWND dlg, INT_PTR result) {
    stub_modal_result = result;
    stub_modal_done = true;
    DestroyWindow(dlg);
    return TRUE;
}

WINUSERAPI BOOL WINAPI IsDialogMessageW(HWND dlg, LPMSG msg) {
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    return FALSE;
}

WINUSERAPI INT_PTR WINAPI DialogBoxParam(HINSTANCE inst, LPCSTR tmpl, HWND parent,
    DLGPROC proc, LPARAM param) {
    (void)inst;
    (void)tmpl;
    (void)parent;
    (void)proc;
    (void)param;
    return IDCANCEL;
}

WINUSERAPI int WINAPI MessageBox(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    (void)hwnd; (void)text; (void)caption;
    if (type & MB_OKCANCEL) {
        return IDOK;
    }
    return IDOK;
}

WINUSERAPI BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *) {
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        WNDCLASSW tab_class = {0, stub_tab_proc, 0, 0, NULL, NULL, NULL, NULL, NULL, WC_TABCONTROLW};
        RegisterClassW(&tab_class);
    }
    return TRUE;
}

WINUSERAPI int WINAPI MulDiv(int n, int mul, int div) {
    if (div == 0) {
        return -1;
    }
    return (int)(((long long)n * mul) / div);
}

WINUSERAPI int WINAPI GetSystemMetrics(int idx) {
    switch (idx) {
      case SM_CXEDGE:
      case SM_CYEDGE:
        return 2;
      default:
        assert(0);
    }
    return 0;
}

WINUSERAPI DWORD WINAPI GetSysColor(int idx) {
    (void)idx;
    return RGB(192, 192, 192);
}

WINUSERAPI SHORT WINAPI GetKeyState(int vk) {
    (void)vk;
    return 0;
}

WINUSERAPI BOOL WINAPI GetKeyboardState(PBYTE keystate) {
    if (!keystate) {
        return FALSE;
    }
    memset(keystate, 0, 256);
    return TRUE;
}

WINUSERAPI BOOL WINAPI SystemParametersInfoW(UINT action, UINT param, PVOID ptr, UINT winini) {
    (void)winini;
    if (action == SPI_GETNONCLIENTMETRICS && ptr) {
        NONCLIENTMETRICSW *ncm = (NONCLIENTMETRICSW *)ptr;
        memset(ncm, 0, param);
        ncm->cbSize = param;
        ncm->lfMenuFont.lfHeight = -12;
        wcscpy(ncm->lfMenuFont.lfFaceName, L"Segoe UI");
        return TRUE;
    }
    return FALSE;
}

typedef struct StubMonitorInfo {
    int screen_width;
    int screen_height;
    int screen_dpi;
} StubMonitorInfo;

static StubMonitorInfo stub_monitor_info = {1920, 1080, 96};

WINUSERAPI BOOL WINAPI GetMonitorInfo(HMONITOR mon, LPMONITORINFO info) {
    StubMonitorInfo *m = (StubMonitorInfo *)mon;
    info->rcMonitor.left = 0;
    info->rcMonitor.top = 0;
    info->rcMonitor.right = m->screen_width;
    info->rcMonitor.bottom = m->screen_height;
    info->rcWork = info->rcMonitor;
    return TRUE;
}

WINUSERAPI HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD flags) {
    return (HMONITOR)&stub_monitor_info;
}

WINUSERAPI HRESULT WINAPI GetDpiForMonitor(HMONITOR mon, int, UINT *dx, UINT *dy) {
    StubMonitorInfo *m = (StubMonitorInfo *)mon;
    *dx = m->screen_dpi;
    *dy = m->screen_dpi;
    return S_OK;
}

WINUSERAPI BOOL WINAPI SystemParametersInfoForDpi(UINT action, UINT param, PVOID ptr, UINT winini, UINT dpi) {
    (void)dpi;
    (void)winini;
    if (action == SPI_GETNONCLIENTMETRICS && ptr) {
        NONCLIENTMETRICSW *ncm = (NONCLIENTMETRICSW *)ptr;
        memset(ncm, 0, param);
        ncm->cbSize = param;
        ncm->lfMenuFont.lfHeight = -12;
        wcscpy(ncm->lfMenuFont.lfFaceName, L"Segoe UI");
        return TRUE;
    }
    return FALSE;
}

static int stub_cursor_show_count = 0;

WINUSERAPI int WINAPI ShowCursor(BOOL show) {
    if (show) {
        stub_cursor_show_count++;
    } else {
        stub_cursor_show_count--;
    }
    return stub_cursor_show_count;
}

WINUSERAPI BOOL WINAPI PtInRect(const RECT *r, POINT p) {
    return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom;
}

WINUSERAPI BOOL WINAPI IntersectRect(RECT *dst, const RECT *a, const RECT *b) {
    dst->left = a->left > b->left ? a->left : b->left;
    dst->top = a->top > b->top ? a->top : b->top;
    dst->right = a->right < b->right ? a->right : b->right;
    dst->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    return dst->left < dst->right && dst->top < dst->bottom;
}

WINUSERAPI BOOL WINAPI SetRectEmpty(RECT *r) {
    r->left = r->top = r->right = r->bottom = 0;
    return TRUE;
}

WINUSERAPI BOOL WINAPI OffsetRect(RECT *r, int dx, int dy) {
    r->left += dx;
    r->right += dx;
    r->top += dy;
    r->bottom += dy;
    return TRUE;
}

#include "stubapi_wnd.c"
#include "stubapi_menu.c"
#include "stubapi_imagelist.c"
#include "stubapi_resource.c"
#include "stubapi_gdi.c"
