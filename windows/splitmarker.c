#include <windows.h>

static const char *const overlayClass = "OverlayClass";
static HWND split_marker_hwnd = NULL;

extern HWND frame_hwnd;
extern HINSTANCE inst;

static LRESULT CALLBACK SplitMarkerProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        GetClientRect(hwnd, &r);
        HDC hdc = BeginPaint(hwnd, &ps);
        SelectObject(hdc, GetStockObject(DKGRAY_BRUSH));
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        Rectangle(hdc, 0, 0, r.right, r.bottom);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

void split_marker_init() {
    WNDCLASS wndclass;

    wndclass.style = 0;
    wndclass.lpfnWndProc = SplitMarkerProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hinst;
    wndclass.hIcon = NULL;
    wndclass.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW));
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = overlayClass;
    RegisterClass(&wndclass);
}

void split_marker_show(const RECT *rect) {
    if (split_marker_hwnd) {
        SetWindowPos(split_marker_hwnd, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
    } else {
       split_marker_hwnd = CreateWindowEx(
            0,
            overlayClass,
            NULL,
            WS_CHILD,
            rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            frame_hwnd,
            NULL,
            hinst,
            NULL);
        SetWindowPos(split_marker_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
    }
}

void split_marker_hide() {
    if (split_marker_hwnd) {
        DestroyWindow(split_marker_hwnd);
        split_marker_hwnd = NULL;
    }
}
