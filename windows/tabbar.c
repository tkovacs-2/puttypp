#include <stddef.h>
#include <stdbool.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include "tabbar.h"
#include "tabbar_res.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;
extern POINT dpi_info;

static int tab_extra_height = 0;

enum {
    NOTIFY_NORMAL = 0,
    NOTIFY_SET,
    NOTIFY_SET_BLINK,
    NOTIFY_BLINK
};

#define NOTIFY_BLINK_INTERVAL 500
#define NOTIFY_TIMER_ID 1

typedef struct {
    TCITEMHEADER header;
    unsigned char notifyState;
    bool unusable;
} TabCtrlItem;

static COLORREF unusableTextColor = RGB(255, 0, 0);
static COLORREF notifiedTextColor = RGB(0, 0, 255);
static SIZE imageZone = { 0, 0 };
static SIZE notifyBlinkZone = { 0, 0 };
static int imagePaddingX = 0;
static int closeButtonPaddingX = 0;
static int tabPaddingX = 0;
static int tabPaddingY = 0;
static int activeTopBarCorrection = 0;
static int textDescentCorrection = 0;
static int cxEdge = 0;
static int cyEdge = 0;

static HFONT _hFont = NULL;
static HIMAGELIST _hImglst = NULL;
static WNDPROC _tabBarDefaultProc = NULL;

static COLORREF _activeTopBarFocusedColour = RGB(250, 170, 60);
static COLORREF _activeTopBarUnfocusedColour = RGB(250, 210, 150);
static COLORREF _inactiveBgColour = RGB(192, 192, 192);
static COLORREF _inactiveTextColour = RGB(128, 128, 128);

static bool _isCloseHover = false;
static int _currentHoverTabItem = -1;
static RECT _currentHoverTabRect;

static int _whichCloseClickDown = -1;

static HWND _isDragging = NULL;
static bool _isDraggingOutside = false;
static bool _mightBeDragging = false;
static int _dragCount = 0;
static int _nTabDragged = -1;
static int _previousTabSwapped = -1;
static HIMAGELIST _dragImageList = NULL;

static SIZE _closeButtonZone = { 0, 0 };

static RECT getItemRect(HWND _hSelf, int tabIndex)
{
    RECT rect;
    TabCtrl_GetItemRect(_hSelf, tabIndex, &rect);
    rect.top += cyEdge;
    return rect;
}

static POINT getImagePointFrom(const RECT *tabRect)
{
    POINT point;
    LONG fromBorder = (tabRect->bottom - tabRect->top - imageZone.cy + 1) / 2;
    point.y = tabRect->top + fromBorder;
    point.x = tabRect->left + imagePaddingX;
    return point;
}

static POINT getNotifyBlinkPointFrom(const POINT *image)
{
    POINT point;
    point.y = image->y + imageZone.cy - notifyBlinkZone.cy + notifyBlinkZone.cy/4;
    point.x = image->x + imageZone.cx - notifyBlinkZone.cx + notifyBlinkZone.cx/4;
    return point;
}

static RECT getNotifyBlinkRect(HWND _hSelf, int tabIndex)
{
    RECT rect = getItemRect(_hSelf, tabIndex);
    POINT image = getImagePointFrom(&rect);
    POINT blink = getNotifyBlinkPointFrom(&image);
    rect.left = blink.x;
    rect.top = blink.y;
    rect.right = rect.left + notifyBlinkZone.cx;
    rect.bottom = rect.top + notifyBlinkZone.cy;
    return rect;
}

static int DPIManager_scaleX(int x)
{
    return MulDiv(x, dpi_info.x, 96);
};

static int DPIManager_scaleY(int y)
{
    return MulDiv(y, dpi_info.y, 96);
};

static RECT CloseButtonZone_getButtonRectFrom(const RECT *tabRect)
{
    RECT buttonRect;

    int fromBorder;
    fromBorder = (tabRect->bottom - tabRect->top - _closeButtonZone.cy + 1) / 2;
    buttonRect.left = tabRect->right - closeButtonPaddingX - _closeButtonZone.cx;
    buttonRect.top = tabRect->top + fromBorder;
    buttonRect.bottom = buttonRect.top + _closeButtonZone.cy;
    buttonRect.right = buttonRect.left + _closeButtonZone.cx;

    return buttonRect;
}

static bool CloseButtonZone_isHit(const POINT *point, const RECT *tabRect)
{
    RECT buttonRect = CloseButtonZone_getButtonRectFrom(tabRect);

    if (point->x >= buttonRect.left && point->x <= buttonRect.right && point->y >= buttonRect.top && point->y <= buttonRect.bottom)
        return true;

    return false;
}

static int TabBarPlus_getTabIndexAt(HWND _hSelf, const POINT *point)
{
    TCHITTESTINFO hitInfo;
    hitInfo.pt = *point;
    return SendMessage(_hSelf, TCM_HITTEST, 0, (LPARAM)&hitInfo);
};

static bool TabBarPlus_isPointOutside(HWND _hSelf, const POINT *point)
{
    RECT rect;
    GetClientRect(_hSelf, &rect);
    return !PtInRect(&rect, *point);
}

static void TabBarPlus_notify(HWND _hSelf, int notifyCode, int tabIndex, const POINT *point)
{
    TabBarNotify nmhdr;
    nmhdr.hdr.hwndFrom = _hSelf;
    nmhdr.hdr.code = notifyCode;
    nmhdr.hdr.idFrom = TAB_BAR_NOTIFY_ID;
    nmhdr.tab_origin = tabIndex;
    if (point) {
        nmhdr.point = *point;
    } else {
        nmhdr.point.x = 0;
        nmhdr.point.y = 0;
    }
    SendMessage(GetParent(_hSelf), WM_NOTIFY, 0, (LPARAM)(&nmhdr));
}

static void TabBarPlus_trackMouseEvent(HWND _hSelf, DWORD event2check)
{
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = event2check;
    tme.hwndTrack = _hSelf;
    TrackMouseEvent(&tme);
}

static void TabBarPlus_drawItem(HDC hDC, const RECT *rcItem, HWND _hSelf, int nTab, bool drawItem)
{
    RECT rect = *rcItem;

    bool isSelected = (nTab == SendMessage(_hSelf, TCM_GETCURSEL, 0, 0));

    TCHAR label[MAX_PATH] = { '\0' };
    TabCtrlItem tci;
    tci.header.mask = TCIF_TEXT|TCIF_IMAGE|TCIF_PARAM;
    tci.header.pszText = label;
    tci.header.cchTextMax = MAX_PATH-1;
    tci.header.iImage = 0;
    tci.unusable = false;
    tci.notifyState = NOTIFY_NORMAL;

    SendMessage(_hSelf, TCM_GETITEM, nTab, (LPARAM)(&tci));

    const COLORREF colorActiveBg = GetSysColor(COLOR_BTNFACE);
    const COLORREF colorInactiveBg = _inactiveBgColour;
    const COLORREF colorActiveText = (tci.unusable ? unusableTextColor : GetSysColor(COLOR_BTNTEXT));
    const COLORREF colorInactiveText = (tci.unusable ? unusableTextColor : (tci.notifyState != NOTIFY_NORMAL ? notifiedTextColor : _inactiveTextColour));

    int nSavedDC = SaveDC(hDC);

    SetBkMode(hDC, TRANSPARENT);
    HBRUSH hBrush;

    // equalize drawing areas of active and inactive tabs
    if (drawItem)
    {
        if (isSelected)
        {
            // the drawing area of the active tab extends on all borders by default
            rect.top += cyEdge;
            rect.bottom -= cyEdge;
            rect.left += cxEdge;
            rect.right -= cxEdge;
            // the active tab is also slightly higher by default (use this to shift the tab cotent up bx two pixels if tobBar is not drawn)
            rect.top += cyEdge;
        }
        else
        {
            rect.left -= cxEdge;
            rect.right += cxEdge;
            rect.top += cyEdge;
            rect.bottom += cyEdge;
        }
    } else if (isSelected) {
        rect.top += cyEdge;
    }

    // draw highlights on tabs (top bar for active tab / darkened background for inactive tab)
    if (isSelected)
    {
        bool isFocused = tab_bar_get_from_hwnd(_hSelf)->focused;

        RECT r = rect;
        r.bottom = r.top + activeTopBarCorrection;
        r.top -= cyEdge;

        hBrush = CreateSolidBrush(isFocused ? _activeTopBarFocusedColour : _activeTopBarUnfocusedColour); // #FAAA3C
        FillRect(hDC, &r, hBrush);
        DeleteObject((HGDIOBJ)hBrush);

        r.top = r.bottom;
        r.bottom = rcItem->bottom;
        hBrush = CreateSolidBrush(colorActiveBg);
        FillRect(hDC, &r, hBrush);
        DeleteObject((HGDIOBJ)(hBrush));
    }
    else // inactive tabs
    {
        hBrush = CreateSolidBrush(colorInactiveBg);
        FillRect(hDC, &rect, hBrush);
        DeleteObject((HGDIOBJ)hBrush);
    }

    // draw image
    POINT imagePos = getImagePointFrom(&rect);
    ImageList_Draw(_hImglst, tci.header.iImage, hDC, imagePos.x, imagePos.y, isSelected ? ILD_NORMAL : ILD_BLEND50);

    HDC hdcMemory = CreateCompatibleDC(hDC);

    if (tci.notifyState == NOTIFY_BLINK)
    {
        HBITMAP hBmp = LoadBitmap(hinst, MAKEINTRESOURCE(IDR_BLINKTAB));
        BITMAP bmp;
        GetObject(hBmp, sizeof(bmp), &bmp);

        POINT notifyBlinkPos = getNotifyBlinkPointFrom(&imagePos);

        HGDIOBJ gdiObj = SelectObject(hdcMemory, hBmp);
        StretchBlt(hDC, notifyBlinkPos.x, notifyBlinkPos.y, notifyBlinkZone.cx, notifyBlinkZone.cy, hdcMemory, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
        SelectObject(hdcMemory, gdiObj);
        DeleteObject(hBmp);
    }

    // draw close button
    // 3 status for each inactive tab and selected tab close item :
    // normal / hover / pushed
    int idCloseImg;

    if (_isCloseHover && (_currentHoverTabItem == nTab) && (_whichCloseClickDown == -1)) // hover
        idCloseImg = IDR_CLOSETAB_HOVER;
    else if (_isCloseHover && (_currentHoverTabItem == nTab) && (_whichCloseClickDown == _currentHoverTabItem)) // pushed
        idCloseImg = IDR_CLOSETAB_PUSH;
    else
        idCloseImg = isSelected ? IDR_CLOSETAB : IDR_CLOSETAB_INACT;

    HBITMAP hBmp = LoadBitmap(hinst, MAKEINTRESOURCE(idCloseImg));
    BITMAP bmp;
    GetObject(hBmp, sizeof(bmp), &bmp);

    RECT buttonRect = CloseButtonZone_getButtonRectFrom(&rect);

    SelectObject(hdcMemory, hBmp);
    StretchBlt(hDC, buttonRect.left, buttonRect.top, _closeButtonZone.cx, _closeButtonZone.cy, hdcMemory, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);
    DeleteDC(hdcMemory);
    DeleteObject(hBmp);

    // draw text
    SelectObject(hDC, _hFont);

    SIZE charPixel;
    GetTextExtentPoint(hDC, TEXT(" "), 1, &charPixel);
    int spaceUnit = charPixel.cx;

    TEXTMETRIC textMetrics;
    GetTextMetrics(hDC, &textMetrics);
    int textDescent = textMetrics.tmDescent / textDescentCorrection;

    int Flags = DT_SINGLELINE | DT_NOPREFIX;

    // center text vertically
    Flags |= DT_LEFT;
    Flags |= DT_VCENTER;

    // ignoring the descent when centering (text elements below the base line) is more pleasing to the eye
    rect.top += textDescent;
    rect.bottom += textDescent;

    // 1 space distance to save icon
    rect.left = imagePos.x + imageZone.cx;
    rect.left += spaceUnit;

    COLORREF textColor = isSelected ? colorActiveText : colorInactiveText;

    SetTextColor(hDC, textColor);

    DrawText(hDC, label, lstrlen(label), &rect, Flags);
    RestoreDC(hDC, nSavedDC);
}

static void TabBarPlus_setDragCursor(bool outside)
{
    SetCursor(LoadCursor(NULL, outside ? IDC_ARROW : IDC_SIZEWE));
}

static void TabBarPlus_clearDragCursor()
{
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

static void TabBarPlus_endDragImage(void)
{
    if (!_dragImageList)
        return;

    ImageList_DragLeave(NULL);
    ImageList_EndDrag();
    ImageList_Destroy(_dragImageList);
    _dragImageList = NULL;
}

static void TabBarPlus_beginDragImage(HWND _hSelf, int nTab, const POINT *point)
{
    if (_dragImageList) {
        return;
    }

    RECT rect;
    TabCtrl_GetItemRect(_hSelf, nTab, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    HDC hdc = GetDC(_hSelf);
    HDC hdcMemory = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(hdcMemory, hBitmap);
    RECT drawRect = { 0, 0, width, height };
    TabBarPlus_drawItem(hdcMemory, &drawRect, _hSelf, nTab, false);
    SelectObject(hdcMemory, oldBitmap);
    DeleteDC(hdcMemory);
    ReleaseDC(_hSelf, hdc);

    _dragImageList = ImageList_Create(width, height, ILC_COLOR24, 1, 1);
    ImageList_Add(_dragImageList, hBitmap, NULL);
    DeleteObject(hBitmap);

    ImageList_BeginDrag(_dragImageList, 0, width / 2, height);

    POINT screenPoint = *point;
    ClientToScreen(_hSelf, &screenPoint);
    ImageList_DragEnter(NULL, screenPoint.x, screenPoint.y);
}

static void TabBarPlus_moveDragImage(HWND _hSelf, const POINT *point)
{
    if (!_dragImageList)
        return;

    POINT screenPoint = *point;
    ClientToScreen(_hSelf, &screenPoint);
    ImageList_DragMove(screenPoint.x, screenPoint.y);
}

static void TabBarPlus_exchangeTabItemData(HWND _hSelf, int oldTab, int newTab)
{
    //1. shift their data, and insert the source
    TabCtrlItem itemData_nDraggedTab, itemData_shift;

    itemData_nDraggedTab.header.mask = itemData_shift.header.mask = TCIF_IMAGE | TCIF_TEXT | TCIF_PARAM;
    #define stringSize 256
    TCHAR str1[stringSize] = { '\0' };
    TCHAR str2[stringSize] = { '\0' };

    itemData_nDraggedTab.header.pszText = str1;
    itemData_nDraggedTab.header.cchTextMax = (stringSize);

    itemData_shift.header.pszText = str2;
    itemData_shift.header.cchTextMax = (stringSize);
    #undef stringSize
    SendMessage(_hSelf, TCM_GETITEM, oldTab, (LPARAM)(&itemData_nDraggedTab));

    if (oldTab > newTab)
    {
        for (int i = oldTab; i > newTab; i--)
        {
            SendMessage(_hSelf, TCM_GETITEM, i - 1, (LPARAM)(&itemData_shift));
            SendMessage(_hSelf, TCM_SETITEM, i, (LPARAM)(&itemData_shift));
        }
    }
    else
    {
        for (int i = oldTab; i < newTab; ++i)
        {
            SendMessage(_hSelf, TCM_GETITEM, i + 1, (LPARAM)(&itemData_shift));
            SendMessage(_hSelf, TCM_SETITEM, i, (LPARAM)(&itemData_shift));
        }
    }
    SendMessage(_hSelf, TCM_SETITEM, newTab, (LPARAM)(&itemData_nDraggedTab));

    //2. set to focus
    SendMessage(_hSelf, TCM_SETCURSEL, newTab, 0);
    TabBarPlus_notify(_hSelf, TCN_TABEXCHANGE, oldTab, NULL);
}

static void TabBarPlus_exchangeItemData(HWND _hSelf, POINT point)
{
    // Find the destination tab...
    int nTab = TabBarPlus_getTabIndexAt(_hSelf, &point);

    // The position is over a tab.
    //if (hitinfo.flags != TCHT_NOWHERE)
    if (nTab != -1)
    {
        if (nTab != _nTabDragged)
        {
            if (_previousTabSwapped == nTab)
            {
                return;
            }

            TabBarPlus_exchangeTabItemData(_hSelf, _nTabDragged, nTab);
            _previousTabSwapped = _nTabDragged;
            _nTabDragged = nTab;
        }
        else
        {
            _previousTabSwapped = -1;
        }
    }
    else
    {
        _previousTabSwapped = -1;
    }
}

static BOOL TabBarPlus_endDragging(HWND _hSelf)
{
    _mightBeDragging = false;
    _dragCount = 0;
    if (_isDragging)
    {
        TabBarPlus_clearDragCursor();
        if (_isDraggingOutside)
        {
            TabBarPlus_endDragImage();
        }
        _isDragging = NULL;
        if (GetCapture() == _hSelf)
        {
            ReleaseCapture();
        }
        return TRUE;
    }
    return FALSE;
}

static LRESULT TabBarPlus_runProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    HWND _hSelf = hwnd;
    switch (Message)
    {
        case WM_LBUTTONDOWN :
        {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

            if (CloseButtonZone_isHit(&p, &_currentHoverTabRect))
            {
                _whichCloseClickDown = TabBarPlus_getTabIndexAt(hwnd, &p);
                InvalidateRect(hwnd, &_currentHoverTabRect, FALSE);
                return TRUE;
            }

            CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);

            if (wParam == 2)
                return TRUE;

            _mightBeDragging = true;

            return TRUE;
        }

        case WM_RBUTTONDOWN :    //rightclick selects tab aswell
        {
            if (_isDragging) {
                TabBarPlus_endDragging(hwnd);
                if (_isDraggingOutside) {
                    TabBarPlus_notify(hwnd, TCN_OUTSIDE_CANCEL, _nTabDragged, NULL);
                }
                return TRUE;
            }
            CallWindowProc(_tabBarDefaultProc, hwnd, WM_LBUTTONDOWN, wParam, lParam);
            return TRUE;
        }

        case WM_MOUSEMOVE :
        {
            if (_mightBeDragging && !_isDragging)
            {
                // Grrr! Who has stolen focus and eaten the WM_LBUTTONUP?!
                if (GetKeyState(VK_LBUTTON) >= 0)
                {
                    _mightBeDragging = false;
                    _dragCount = 0;
                }
                else if (++_dragCount > 2)
                {
                    int tabSelected = SendMessage(_hSelf, TCM_GETCURSEL, 0, 0);

                    if (tabSelected >= 0)
                    {
                        _nTabDragged = tabSelected;
                        _isDragging = hwnd;
                        _isDraggingOutside = false;
                        TabBarPlus_setDragCursor(false);

                        // TLS_BUTTONS is already captured on Windows and will break on ::SetCapture
                        // However, this is not the case for WINE/ReactOS and must ::SetCapture
                        if (GetCapture() != _hSelf)
                        {
                            SetCapture(hwnd);
                        }
                    }
                }
            }
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            {
                POINT frame_p = p;
                MapWindowPoints(_hSelf, frame_hwnd, &frame_p, 1);
                SendMessage(frame_hwnd, WM_MOUSEMOVE, wParam, MAKELPARAM(frame_p.x, frame_p.y));
            }

            if (_isDragging)
            {
                if (TabBarPlus_isPointOutside(hwnd, &p))
                {
                    if (!_isDraggingOutside) {
                        TabBarPlus_setDragCursor(true);
                        TabBarPlus_beginDragImage(_hSelf, _nTabDragged, &p);
                    } else {
                        TabBarPlus_moveDragImage(hwnd, &p);
                    }
                    TabBarPlus_notify(hwnd, TCN_OUTSIDE_DRAG, _nTabDragged, &p);
                    _isDraggingOutside = true;
                }
                else
                {
                    if (_isDraggingOutside)
                    {
                        TabBarPlus_setDragCursor(false);
                        TabBarPlus_endDragImage();
                        TabBarPlus_notify(hwnd, TCN_OUTSIDE_CANCEL, _nTabDragged, NULL);
                        _isDraggingOutside = false;
                    }
                    TabBarPlus_exchangeItemData(hwnd, p);
                }
                return TRUE;
            }
            else
            {
                bool isFromTabToTab = false;

                int iTabNow = TabBarPlus_getTabIndexAt(hwnd, &p); // _currentHoverTabItem keeps previous value, and it need to be updated

                if (_currentHoverTabItem == iTabNow && _currentHoverTabItem != -1) // mouse moves arround in the same tab
                {
                    // do nothing
                }
                else if (iTabNow == -1 && _currentHoverTabItem != -1) // mouse is no more on any tab, set hover -1
                {
                    _currentHoverTabItem = -1;
                }
                else if (iTabNow != -1 && _currentHoverTabItem == -1) // mouse is just entered in a tab zone
                {
                    _currentHoverTabItem = iTabNow;
                }
                else if (iTabNow != -1 && _currentHoverTabItem != -1 && _currentHoverTabItem != iTabNow) // mouse is being moved from a tab and entering into another tab
                {
                    isFromTabToTab = true;
                    _whichCloseClickDown = -1;

                    // set current hovered
                    _currentHoverTabItem = iTabNow;
                }
                else if (iTabNow == -1 && _currentHoverTabItem == -1) // mouse is already outside
                {
                    // do nothing
                }

                RECT currentHoverTabRectOld = _currentHoverTabRect;
                bool isCloseHoverOld = _isCloseHover;

                if (_currentHoverTabItem != -1) // is hovering
                {
                    _currentHoverTabRect = getItemRect(hwnd, _currentHoverTabItem);
                    _isCloseHover = CloseButtonZone_isHit(&p, &_currentHoverTabRect);
                }
                else
                {
                    SetRectEmpty(&_currentHoverTabRect);
                    _isCloseHover = false;
                }

                if (isFromTabToTab || _isCloseHover != isCloseHoverOld)
                {
                    if (isCloseHoverOld && (isFromTabToTab || !_isCloseHover))
                        InvalidateRect(hwnd, &currentHoverTabRectOld, FALSE);

                    if (_isCloseHover)
                        InvalidateRect(hwnd, &_currentHoverTabRect, FALSE);
                }

                // Mouse moves out from tab zone will send WM_MOUSELEAVE message
                // but it doesn't track mouse moving from a tab to another
                TabBarPlus_trackMouseEvent(hwnd, TME_LEAVE);
            }

            break;
        }

        case WM_MOUSELEAVE:
        {
            if (_isCloseHover)
                InvalidateRect(hwnd, &_currentHoverTabRect, FALSE);

            _currentHoverTabItem = -1;
            _whichCloseClickDown = -1;
            SetRectEmpty(&_currentHoverTabRect);
            _isCloseHover = false;

            break;
        }

        case WM_LBUTTONUP :
        {
            POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int currentTabOn = TabBarPlus_getTabIndexAt(hwnd, &p);
            if (_isDragging)
            {
                TabBarPlus_endDragging(hwnd);
                if (TabBarPlus_isPointOutside(hwnd, &p)) {
                    TabBarPlus_notify(hwnd, TCN_OUTSIDE_RELEASE, _nTabDragged, &p);
                }
                else if (_isDraggingOutside) {
                    TabBarPlus_notify(hwnd, TCN_OUTSIDE_CANCEL, _nTabDragged, NULL);
                }
                return TRUE;
            }

            if ((_whichCloseClickDown == currentTabOn) && CloseButtonZone_isHit(&p, &_currentHoverTabRect))
            {
                TabBarPlus_notify(hwnd, TCN_TABDELETE, currentTabOn, NULL);
                _whichCloseClickDown = -1;

                // Get the next tab at same position
                // If valid tab is found then
                //     update the current hover tab RECT (_currentHoverTabRect)
                //     update close hover flag (_isCloseHover), so that x will be highlighted or not based on new _currentHoverTabRect
                int nextTab = TabBarPlus_getTabIndexAt(hwnd, &p);
                if (nextTab != -1)
                {
                    _currentHoverTabRect = getItemRect(hwnd, nextTab);
                    _isCloseHover = CloseButtonZone_isHit(&p, &_currentHoverTabRect);
                }
                return TRUE;
            }
            _whichCloseClickDown = -1;
            break;
        }

        case WM_CAPTURECHANGED :
        {
            if (_isDragging){
                TabBarPlus_endDragging(_hSelf);
                if (_isDraggingOutside) {
                    TabBarPlus_notify(hwnd, TCN_OUTSIDE_CANCEL, _nTabDragged, NULL);
                }
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM :
        {
            DRAWITEMSTRUCT *pDrawItemStruct = (DRAWITEMSTRUCT *)lParam;
            TabBarPlus_drawItem(pDrawItemStruct->hDC, &pDrawItemStruct->rcItem, hwnd, pDrawItemStruct->itemID, true);
            return TRUE;
        }

        case WM_NCHITTEST:
        {
            // Windows sends WM_MOUSEMOVE only in the strict tab zone.
            // Around that we can only catch WM_NCHITTEST.
            LRESULT result = CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
            if (result == HTTRANSPARENT) {
                POINT p = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(frame_hwnd, &p);
                SendMessage(frame_hwnd, WM_MOUSEMOVE, wParam, MAKELPARAM(p.x, p.y));
            }
            return result;
        }

        case WM_TIMER:
        {
            bool needNotifyTimer = false;
            int itemCount = TabCtrl_GetItemCount(_hSelf);
            TabCtrlItem tci;

            tci.header.mask = TCIF_PARAM;
            for (int i=0; i<itemCount; i++) {
                TabCtrl_GetItem(_hSelf, i, &tci);
                if (tci.notifyState != NOTIFY_NORMAL) {
                    needNotifyTimer = true;
                }
                if (tci.notifyState == NOTIFY_SET_BLINK) {
                    tci.notifyState = NOTIFY_BLINK;
                    TabCtrl_SetItem(_hSelf, i, &tci);
                    RECT rect = getNotifyBlinkRect(hwnd, i);
                    InvalidateRect(_hSelf, &rect, FALSE);
                } else if (tci.notifyState == NOTIFY_BLINK) {
                    tci.notifyState = NOTIFY_SET;
                    TabCtrl_SetItem(_hSelf, i, &tci);
                    RECT rect = getNotifyBlinkRect(hwnd, i);
                    InvalidateRect(_hSelf, &rect, FALSE);
                }
            }
            if (!needNotifyTimer) {
                KillTimer(_hSelf, NOTIFY_TIMER_ID);
                TabBar *tab_bar = tab_bar_get_from_hwnd(_hSelf);
                tab_bar->notify_blink_timer = 0;
            }
        }
    }

    return CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
}

void tab_bar_common_init(HFONT dpi_aware_font) {
    INITCOMMONCONTROLSEX icce;
    icce.dwSize = sizeof(icce);
    icce.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icce);
    _hImglst = ImageList_Create(1, 1, ILC_COLOR4, 0, 10);

    tab_bar_common_dpi_changed(dpi_aware_font);

    cxEdge = GetSystemMetrics(SM_CXEDGE);
    cyEdge = GetSystemMetrics(SM_CYEDGE);
}

void tab_bar_common_uninit() {
    ImageList_Destroy(_hImglst);
}

void tab_bar_common_dpi_changed(HFONT dpi_aware_font) {
    imageZone.cx = DPIManager_scaleX(16);
    imageZone.cy = DPIManager_scaleY(13);
    notifyBlinkZone.cx = DPIManager_scaleX(8);
    notifyBlinkZone.cy = DPIManager_scaleY(8);
    _closeButtonZone.cx = DPIManager_scaleX(11);
    _closeButtonZone.cy = DPIManager_scaleY(11);
    imagePaddingX = DPIManager_scaleX(4);
    closeButtonPaddingX = DPIManager_scaleX(5);
    tabPaddingX = DPIManager_scaleX(15);
    tabPaddingY = DPIManager_scaleY(5);
    activeTopBarCorrection = DPIManager_scaleY(2);
    textDescentCorrection = DPIManager_scaleY(2);

    ImageList_SetIconSize(_hImglst, imageZone.cx, imageZone.cy);
    for (int i=IDI_BACKEND_FIRST; i<=IDI_BACKEND_LAST; i++) {
        HICON hIcon = LoadImage(hinst, MAKEINTRESOURCE(i), IMAGE_ICON, imageZone.cx, imageZone.cy, 0);
        ImageList_AddIcon(_hImglst, hIcon);
        DestroyIcon(hIcon);
    }

    _hFont = dpi_aware_font;
    tab_extra_height = 0;
}

int tab_bar_common_height() {
    return tab_extra_height;
}

void tab_bar_init(TabBar *tab_bar, const RECT *rect, void *user_data) {
    int style = WS_CHILD | \
        TCS_FOCUSNEVER | TCS_TABS | TCS_OWNERDRAWFIXED;

    tab_bar->hwnd = CreateWindowEx(
                0,
                WC_TABCONTROL,
                NULL,
                style,
                rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                frame_hwnd,
                (HMENU)(INT_PTR)TAB_BAR_NOTIFY_ID,
                hinst,
                (LPVOID)tab_bar);
    tab_bar->notify_blink_timer = 0;
    tab_bar->user_data = user_data;
    tab_bar->focused = false;

    SetWindowLongPtr(tab_bar->hwnd, GWLP_USERDATA, (LONG_PTR)tab_bar);
    _tabBarDefaultProc = (WNDPROC)(SetWindowLongPtr(tab_bar->hwnd, GWLP_WNDPROC, (LONG_PTR)TabBarPlus_runProc));
    TabCtrl_SetItemExtra(tab_bar->hwnd, sizeof(TabCtrlItem)-sizeof(TCITEMHEADER));

    tab_bar_dpi_changed(tab_bar);
    SetWindowPos(tab_bar->hwnd, NULL, 0, 0, rect->right - rect->left, tab_extra_height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_SHOWWINDOW);
}

void tab_bar_uninit(TabBar *tab_bar) {
    HWND _hSelf = tab_bar->hwnd;
    DestroyWindow(_hSelf);
    tab_bar->hwnd = NULL;
    tab_bar->notify_blink_timer = 0;
}

void tab_bar_dpi_changed(TabBar *tab_bar) {
    SendMessage(tab_bar->hwnd, WM_SETFONT, (WPARAM)_hFont, (LPARAM)FALSE);
    SendMessage(tab_bar->hwnd, TCM_SETPADDING, 0, MAKELPARAM(tabPaddingX, tabPaddingY));
    SendMessage(tab_bar->hwnd, TCM_SETIMAGELIST, 0, (LPARAM)_hImglst);

    if (tab_extra_height == 0) {
        bool is_empty = (TabCtrl_GetItemCount(tab_bar->hwnd) == 0);
        if (is_empty) {
            TabCtrlItem tci;
            tci.header.mask = TCIF_TEXT;
            tci.header.pszText = "X";
            TabCtrl_InsertItem(tab_bar->hwnd, 0, &tci);
        }
        RECT r = {0, 0, 0, 0};
        TabCtrl_AdjustRect(tab_bar->hwnd, TRUE, &r);
        tab_extra_height = -r.top + r.bottom;
        if (is_empty) {
            TabCtrl_DeleteItem(tab_bar->hwnd, 0);
        }
    }
}

HDWP tab_bar_adjust_window(TabBar *tab_bar, const RECT *rect, HDWP hdwp) {
    return DeferWindowPos(hdwp, tab_bar->hwnd, NULL, rect->left, rect->top,
                          rect->right - rect->left, tab_extra_height,
                          SWP_NOACTIVATE | SWP_NOZORDER);
}

void tab_bar_insert_tab(TabBar *tab_bar, int index, const char *title, int image) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_TEXT|TCIF_IMAGE|TCIF_PARAM;
    tci.header.pszText = (LPSTR)title;
    tci.header.iImage = image;
    tci.notifyState = NOTIFY_NORMAL;
    tci.unusable = false;

    TabCtrl_InsertItem(tab_bar->hwnd, index, &tci);
}

void tab_bar_remove_tab(TabBar *tab_bar, int index) {
    TabCtrl_DeleteItem(tab_bar->hwnd, index);
}

void tab_bar_select_tab(TabBar *tab_bar, int index) {
    TabCtrl_SetCurSel(tab_bar->hwnd, index);
}

int tab_bar_get_active_tab(TabBar *tab_bar) {
    return TabCtrl_GetCurSel(tab_bar->hwnd);
}

void tab_bar_set_tab_title(TabBar *tab_bar, int index, const char *title) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_TEXT;
    tci.header.pszText = (LPSTR)title;

    TabCtrl_SetItem(tab_bar->hwnd, index, &tci);
}

void tab_bar_set_tab_unusable(TabBar *tab_bar, int index, bool unusable) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    tci.notifyState = NOTIFY_NORMAL;
    tci.unusable = unusable;

    if (TabCtrl_SetItem(tab_bar->hwnd, index, &tci)) {
        RECT rect = getItemRect(tab_bar->hwnd, index);
        InvalidateRect(tab_bar->hwnd, &rect, FALSE);
    }
}

void tab_bar_set_tab_notified(TabBar *tab_bar, int index) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    if (!TabCtrl_GetItem(tab_bar->hwnd, index, &tci)) {
        return;
    }
    if (tci.unusable) {
        return;
    }
    if (tci.notifyState == NOTIFY_NORMAL) {
        tci.notifyState = NOTIFY_SET;
        TabCtrl_SetItem(tab_bar->hwnd, index, &tci);
        RECT rect = getItemRect(tab_bar->hwnd, index);
        InvalidateRect(tab_bar->hwnd, &rect, FALSE);
        if (!tab_bar->notify_blink_timer) {
            tab_bar->notify_blink_timer = SetTimer(tab_bar->hwnd, NOTIFY_TIMER_ID, NOTIFY_BLINK_INTERVAL, NULL);
        }
    } else if (tci.notifyState == NOTIFY_SET) {
        tci.notifyState = NOTIFY_SET_BLINK;
        TabCtrl_SetItem(tab_bar->hwnd, index, &tci);
    }
}

void tab_bar_clear_tab_notified(TabBar *tab_bar, int index) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    if (!TabCtrl_GetItem(tab_bar->hwnd, index, &tci)) {
        return;
    }
    if (tci.notifyState != NOTIFY_NORMAL) {
        tci.notifyState = NOTIFY_NORMAL;
        TabCtrl_SetItem(tab_bar->hwnd, index, &tci);
        RECT rect = getItemRect(tab_bar->hwnd, index);
        InvalidateRect(tab_bar->hwnd, &rect, FALSE);
    }
}

void tab_bar_import_tab(TabBar *tab_bar, TabBar *source, int target_index, int source_index) {
    char title[MAX_PATH] = { '\0' };
    TabCtrlItem tci;

    tci.header.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_PARAM;
    tci.header.pszText = title;
    tci.header.cchTextMax = MAX_PATH - 1;

    TabCtrl_GetItem(source->hwnd, source_index, &tci);
    TabCtrl_InsertItem(tab_bar->hwnd, target_index, &tci);
}

TabBar *tab_bar_get_from_hwnd(HWND hwnd) {
    return (TabBar *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

void tab_bar_set_focused(TabBar *tab_bar, bool focused) {
    if (tab_bar->focused != focused) {
        tab_bar->focused = focused;
        int selected = TabCtrl_GetCurSel(tab_bar->hwnd);
        if (selected >= 0) {
            RECT rect;
            TabCtrl_GetItemRect(tab_bar->hwnd, selected, &rect);
            rect.bottom = rect.top + cyEdge + activeTopBarCorrection;
            InvalidateRect(tab_bar->hwnd, &rect, FALSE);
        }
    }
}

void tab_bar_cancel_dragging(TabBar *tab_bar) {
    if (_isDragging != tab_bar->hwnd) {
        return;
    }
    if (_isDragging) {
        TabBarPlus_endDragging(tab_bar->hwnd);
        if (_isDraggingOutside) {
            TabBarPlus_notify(tab_bar->hwnd, TCN_OUTSIDE_CANCEL, _nTabDragged, NULL);
        }
    }
}

void tab_bar_get_selected_tab_hotspot(TabBar *tab_bar, POINT *hotspot) {
    RECT rect;
    TabCtrl_GetItemRect(tab_bar->hwnd, TabCtrl_GetCurSel(tab_bar->hwnd), &rect);
    hotspot->x = rect.left;
    hotspot->y = rect.bottom;
    ClientToScreen(tab_bar->hwnd, hotspot);
}
