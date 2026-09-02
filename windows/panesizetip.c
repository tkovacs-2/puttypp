/*
 * pane_sizetip.c - resize tips for panes.
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

#include "panesizetip.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;

static const WCHAR SIZE_TIP_CLASS_NAME[] = L"PaneSizeTip";
static const int SIZE_TIP_PADDING = 3;

static HFONT size_tip_font = NULL;
static COLORREF size_tip_bg = RGB(255, 255, 225);
static COLORREF size_tip_text = RGB(0, 0, 0);

static LRESULT CALLBACK SizeTipWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SizeTip *size_tip = (SizeTip *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (message) {
      case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(SizeTip *)cs->lpCreateParams);
        return TRUE;
      }

      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH brush = CreateSolidBrush(size_tip_bg);
        HGDIOBJ oldbrush = SelectObject(hdc, brush);
        HGDIOBJ oldfont = SelectObject(hdc, size_tip_font);
        SelectObject(hdc, GetStockObject(BLACK_PEN));

        RECT rect;
        GetClientRect(hwnd, &rect);
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);

        SetTextColor(hdc, size_tip_text);
        SetBkColor(hdc, size_tip_bg);
        TextOutW(hdc, rect.left + SIZE_TIP_PADDING,
                    rect.top + SIZE_TIP_PADDING,
                    size_tip->text, wcslen(size_tip->text));

        SelectObject(hdc, oldfont);
        SelectObject(hdc, oldbrush);
        DeleteObject(brush);

        EndPaint(hwnd, &ps);
        return 0;
      }
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void size_tip_common_init(HFONT dpi_aware_font) {
    WNDCLASSW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SizeTipWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = SIZE_TIP_CLASS_NAME;
    RegisterClassW(&wc);

    size_tip_common_dpi_changed(dpi_aware_font);
    size_tip_bg = GetSysColor(COLOR_INFOBK);
    size_tip_text = GetSysColor(COLOR_INFOTEXT);
}

void size_tip_common_dpi_changed(HFONT dpi_aware_font) {
    size_tip_font = dpi_aware_font;
}

void size_tip_init(SizeTip *size_tip) {
    memset(size_tip, 0, sizeof(*size_tip));
}

void size_tip_uninit(SizeTip *size_tip) {
    size_tip_hide(size_tip);
}

void size_tip_update(SizeTip *size_tip, int width, int height, const POINT *point) {
    bool changed = (size_tip->width != width || size_tip->height != height);

    if (!changed && size_tip->text[0] != L'\0') {
        return;
    }

    size_t text_len = sizeof(size_tip->text) / sizeof(size_tip->text[0]);
    size_tip->width = width;
    size_tip->height = height;
    _snwprintf(size_tip->text, text_len, L"%dx%d", width, height);
    size_tip->text[text_len - 1] = L'\0';

    SIZE size;
    HDC hdc = CreateCompatibleDC(NULL);
    HGDIOBJ oldfont = SelectObject(hdc, size_tip_font);
    GetTextExtentPoint32W(hdc, size_tip->text, wcslen(size_tip->text), &size);
    SelectObject(hdc, oldfont);
    DeleteDC(hdc);
    size.cx += SIZE_TIP_PADDING * 2;
    size.cy += SIZE_TIP_PADDING * 2;

    if (size_tip->hwnd) {
        SetWindowPos(size_tip->hwnd, NULL, 0, 0, size.cx, size.cy,
            SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOREDRAW);
        InvalidateRect(size_tip->hwnd, NULL, FALSE);
    } else {
        size_tip->hwnd = CreateWindowExW(
            0, SIZE_TIP_CLASS_NAME, NULL, WS_CHILD,
            point->x, point->y, size.cx, size.cy,
            frame_hwnd, NULL, hinst, size_tip);
        SetWindowPos(size_tip->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);
    }
}

void size_tip_move(SizeTip *size_tip, const POINT *point) {
    if (size_tip->hwnd) {
        SetWindowPos(size_tip->hwnd, NULL, point->x, point->y, 0, 0,
                     SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    }
}

void size_tip_hide(SizeTip *size_tip) {
    if (size_tip->hwnd) {
        DestroyWindow(size_tip->hwnd);
        size_tip->hwnd = NULL;
    }
    size_tip->text[0] = L'\0';
}
