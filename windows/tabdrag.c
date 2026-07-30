#include <stdbool.h>
#include <windows.h>

#include "tabdrag.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static const WCHAR TAB_DRAG_CLASS_NAME[] = L"TabDrag";

static HWND drag_hwnd = NULL;
static HBITMAP drag_bitmap = NULL;
static int drag_width;
static int drag_height;
static int drag_hotspot_x;
static int drag_hotspot_y;

static bool is_in_frame_client(int x, int y) {
    POINT pt = {x, y};
    ScreenToClient(frame_hwnd, &pt);

    RECT client;
    GetClientRect(frame_hwnd, &client);
    return PtInRect(&client, pt);
}

static LRESULT CALLBACK tab_drag_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC memdc = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(memdc, drag_bitmap);
        BitBlt(hdc, 0, 0, drag_width, drag_height, memdc, 0, 0, SRCCOPY);
        SelectObject(memdc, old);
        DeleteDC(memdc);

        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_DPICHANGED:
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void tab_drag_init() {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = tab_drag_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = TAB_DRAG_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);
}

void tab_drag_show(HBITMAP bitmap, int x, int y) {
    if (drag_hwnd) {
        DeleteObject(bitmap);
        return;
    }

    BITMAP bm = {0};
    GetObjectW(bitmap, sizeof(bm), &bm);
    drag_bitmap = bitmap;
    drag_width = bm.bmWidth;
    drag_height = bm.bmHeight;
    drag_hotspot_x = bm.bmWidth / 2;
    drag_hotspot_y = bm.bmHeight;

    drag_hwnd = CreateWindowExW(
        0,
        TAB_DRAG_CLASS_NAME,
        NULL,
        WS_POPUP,
        x - drag_hotspot_x, y - drag_hotspot_y,
        drag_width, drag_height,
        frame_hwnd, NULL, hinst, NULL);

    SetWindowPos(drag_hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                (is_in_frame_client(x, y) ? SWP_SHOWWINDOW : 0));
}

void tab_drag_hide(void) {
    if (!drag_hwnd) {
        return;
    }

    DestroyWindow(drag_hwnd);
    drag_hwnd = NULL;

    DeleteObject(drag_bitmap);
    drag_bitmap = NULL;
}

void tab_drag_move(int x, int y) {
    if (!drag_hwnd) {
        return;
    }

    if (is_in_frame_client(x, y)) {
        SetWindowPos(drag_hwnd, NULL,
            x - drag_hotspot_x, y - drag_hotspot_y, 0, 0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    } else {
        if (IsWindowVisible(drag_hwnd)) {
            ShowWindow(drag_hwnd, SW_HIDE);
        }
    }
}
