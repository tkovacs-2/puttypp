#include <stddef.h>
#include <stdbool.h>
#include <windows.h>
#include <commctrl.h>

#include "tabbar.h"
#include "tabbar_res.h"

extern HINSTANCE hinst;
extern HWND frame_hwnd;
extern POINT dpi_info;

static int tab_extra_width = 0;
static int tab_extra_height = 0;

enum {
    NOTIFY_NORMAL = 0,
    NOTIFY_SET,
    NOTIFY_SET_BLINK,
    NOTIFY_BLINK
};

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
static UINT_PTR notifyBlinkTimer = 0;

static HWND _hSelf = NULL;
static HFONT _hFont = NULL;
static HIMAGELIST _hImglst = NULL;
static WNDPROC _tabBarDefaultProc = NULL;

static COLORREF _activeTopBarFocusedColour = RGB(250, 170, 60);
static COLORREF _inactiveBgColour = RGB(192, 192, 192);
static COLORREF _inactiveTextColour = RGB(128, 128, 128);

static bool _isCloseHover = false;
static int _currentHoverTabItem = -1;
static RECT _currentHoverTabRect;

static int _whichCloseClickDown = -1;

static bool _isDragging = false;
static bool _mightBeDragging = false;
static int _dragCount = 0;
static int _nTabDragged = -1;
static int _previousTabSwapped = -1;

static SIZE _closeButtonZone = { 0, 0 };

static RECT getItemRect(int tabIndex)
{
    RECT rect;
    TabCtrl_GetItemRect(_hSelf, tabIndex, &rect);
    rect.top += GetSystemMetrics(SM_CYEDGE);
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

static RECT getNotifyBlinkRect(int tabIndex)
{
    RECT rect = getItemRect(tabIndex);
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

static bool CloseButtonZone_isHit(int x, int y, const RECT *tabRect)
{
    RECT buttonRect = CloseButtonZone_getButtonRectFrom(tabRect);

    if (x >= buttonRect.left && x <= buttonRect.right && y >= buttonRect.top && y <= buttonRect.bottom)
        return true;

    return false;
}

static int TabBarPlus_getTabIndexAt(int x, int y)
{
    TCHITTESTINFO hitInfo;
    hitInfo.pt.x = x;
    hitInfo.pt.y = y;
    return SendMessage(_hSelf, TCM_HITTEST, 0, (LPARAM)&hitInfo);
};

static void TabBarPlus_notify(int notifyCode, int tabIndex)
{
    struct TBHDR nmhdr;
    nmhdr._hdr.hwndFrom = _hSelf;
    nmhdr._hdr.code = notifyCode;
    nmhdr._hdr.idFrom = 0;
    nmhdr._tabOrigin = tabIndex;
    SendMessage(GetParent(_hSelf), WM_NOTIFY, 0, (LPARAM)(&nmhdr));
}

static void TabBarPlus_trackMouseEvent(DWORD event2check)
{
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = event2check;
    tme.hwndTrack = _hSelf;
    TrackMouseEvent(&tme);
}

static void TabBarPlus_drawItem(DRAWITEMSTRUCT *pDrawItemStruct)
{
    RECT rect = pDrawItemStruct->rcItem;

    int nTab = pDrawItemStruct->itemID;
    if (nTab < 0)
    {
        MessageBox(NULL, TEXT("nTab < 0"), TEXT(""), MB_OK);
    }
    bool isSelected = (nTab == SendMessage(_hSelf, TCM_GETCURSEL, 0, 0));

    TCHAR label[MAX_PATH] = { '\0' };
    TabCtrlItem tci;
    tci.header.mask = TCIF_TEXT|TCIF_IMAGE|TCIF_PARAM;
    tci.header.pszText = label;
    tci.header.cchTextMax = MAX_PATH-1;

    if (!SendMessage(_hSelf, TCM_GETITEM, nTab, (LPARAM)(&tci)))
    {
        MessageBox(NULL, TEXT("! TCM_GETITEM"), TEXT(""), MB_OK);
    }

    const COLORREF colorActiveBg = GetSysColor(COLOR_BTNFACE);
    const COLORREF colorInactiveBg = _inactiveBgColour;
    const COLORREF colorActiveText = (tci.unusable ? unusableTextColor : GetSysColor(COLOR_BTNTEXT));
    const COLORREF colorInactiveText = (tci.unusable ? unusableTextColor : (tci.notifyState != NOTIFY_NORMAL ? notifiedTextColor : _inactiveTextColour));

    HDC hDC = pDrawItemStruct->hDC;

    int nSavedDC = SaveDC(hDC);

    SetBkMode(hDC, TRANSPARENT);
    HBRUSH hBrush;

    int cxEdge = GetSystemMetrics(SM_CXEDGE);
    int cyEdge = GetSystemMetrics(SM_CYEDGE);
    // equalize drawing areas of active and inactive tabs
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

    // draw highlights on tabs (top bar for active tab / darkened background for inactive tab)
    if (isSelected)
    {
        RECT r = rect;
        r.bottom = r.top + DPIManager_scaleX(2);
        r.top -= cyEdge;

        hBrush = CreateSolidBrush(_activeTopBarFocusedColour); // #FAAA3C
        FillRect(hDC, &r, hBrush);
        DeleteObject((HGDIOBJ)hBrush);

        r.top = r.bottom;
        r.bottom = pDrawItemStruct->rcItem.bottom;
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
    HIMAGELIST hImgLst = (HIMAGELIST)SendMessage(_hSelf, TCM_GETIMAGELIST, 0, 0);
    POINT imagePos = getImagePointFrom(&rect);
    ImageList_Draw(hImgLst, tci.header.iImage, hDC, imagePos.x, imagePos.y, isSelected ? ILD_TRANSPARENT : ILD_SELECTED);

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
    int textDescent = textMetrics.tmDescent / DPIManager_scaleX(2);;

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

static void TabBarPlus_exchangeTabItemData(int oldTab, int newTab)
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
    TabBarPlus_notify(TCN_TABEXCHANGE, oldTab);
}

static void TabBarPlus_exchangeItemData(POINT point)
{
    // Find the destination tab...
    int nTab = TabBarPlus_getTabIndexAt(point.x, point.y);

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

            TabBarPlus_exchangeTabItemData(_nTabDragged, nTab);
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

static LRESULT TabBarPlus_runProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
    switch (Message)
    {
        case WM_LBUTTONDOWN :
        {
            int xPos = LOWORD(lParam);
            int yPos = HIWORD(lParam);

            if (CloseButtonZone_isHit(xPos, yPos, &_currentHoverTabRect))
            {
                _whichCloseClickDown = TabBarPlus_getTabIndexAt(xPos, yPos);
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
                        _isDragging = true;

                        // TLS_BUTTONS is already captured on Windows and will break on ::SetCapture
                        // However, this is not the case for WINE/ReactOS and must ::SetCapture
                        if (GetCapture() != _hSelf)
                        {
                            SetCapture(hwnd);
                        }
                    }
                }
            }
            POINT p;
            p.x = LOWORD(lParam);
            p.y = HIWORD(lParam);
            {
                RECT r;
                GetWindowRect(_hSelf, &r);
                SendMessage(GetParent(_hSelf), WM_NCMOUSEMOVE, (WPARAM)HTCLIENT, MAKELPARAM(r.left+p.x, r.top+p.y));
            }

            if (_isDragging)
            {
                TabBarPlus_exchangeItemData(p);
                return TRUE;
            }
            else
            {
                bool isFromTabToTab = false;

                int iTabNow = TabBarPlus_getTabIndexAt(p.x, p.y); // _currentHoverTabItem keeps previous value, and it need to be updated

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
                    _currentHoverTabRect = getItemRect(_currentHoverTabItem);
                    _isCloseHover = CloseButtonZone_isHit(p.x, p.y, &_currentHoverTabRect);
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
                TabBarPlus_trackMouseEvent(TME_LEAVE);
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
            _mightBeDragging = false;
            _dragCount = 0;

            int xPos = LOWORD(lParam);
            int yPos = HIWORD(lParam);
            int currentTabOn = TabBarPlus_getTabIndexAt(xPos, yPos);
            if (_isDragging)
            {
                if (GetCapture() == _hSelf)
                {
                    ReleaseCapture();
                }
                else
                {
                    _isDragging = false;
                }

                return TRUE;
            }

            if ((_whichCloseClickDown == currentTabOn) && CloseButtonZone_isHit(xPos, yPos, &_currentHoverTabRect))
            {
                TabBarPlus_notify(TCN_TABDELETE, currentTabOn);
                _whichCloseClickDown = -1;

                // Get the next tab at same position
                // If valid tab is found then
                //     update the current hover tab RECT (_currentHoverTabRect)
                //     update close hover flag (_isCloseHover), so that x will be highlighted or not based on new _currentHoverTabRect
                int nextTab = TabBarPlus_getTabIndexAt(xPos, yPos);
                if (nextTab != -1)
                {
                    _currentHoverTabRect = getItemRect(nextTab);
                    _isCloseHover = CloseButtonZone_isHit(xPos, yPos, &_currentHoverTabRect);
                }
                return TRUE;
            }
            _whichCloseClickDown = -1;
            break;
        }

        case WM_CAPTURECHANGED :
        {
            if (_isDragging)
            {
                _isDragging = false;
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM :
        {
            TabBarPlus_drawItem((DRAWITEMSTRUCT *)lParam);
            return TRUE;
        }

        case WM_NCHITTEST:
        {
            // Windows sends WM_MOUSEMOVE only in the strict tab zone.
            // Around that we can only catch WM_NCHITTEST.
            LRESULT result = CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
            if (result == HTTRANSPARENT) {
                SendMessage(GetParent(_hSelf), WM_NCMOUSEMOVE, (WPARAM)HTCLIENT, lParam);
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
                    RECT rect = getNotifyBlinkRect(i);
                    InvalidateRect(_hSelf, &rect, FALSE);
                } else if (tci.notifyState == NOTIFY_BLINK) {
                    tci.notifyState = NOTIFY_SET;
                    TabCtrl_SetItem(_hSelf, i, &tci);
                    RECT rect = getNotifyBlinkRect(i);
                    InvalidateRect(_hSelf, &rect, FALSE);
                }
            }
            if (!needNotifyTimer) {
                KillTimer(_hSelf, 1);
                notifyBlinkTimer = 0;
            }
        }
    }

    return CallWindowProc(_tabBarDefaultProc, hwnd, Message, wParam, lParam);
}

void create_tab_bar() {
    INITCOMMONCONTROLSEX icce;
    icce.dwSize = sizeof(icce);
    icce.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icce);

    int style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE |\
        TCS_TOOLTIPS | TCS_FOCUSNEVER | TCS_TABS | TCS_OWNERDRAWFIXED;

    RECT rect;
    GetClientRect(frame_hwnd, &rect);

    _hSelf = CreateWindowEx(
                0,
                WC_TABCONTROL,
                TEXT("Tab"),
                style,
                0, 0, rect.right, rect.bottom,
                frame_hwnd,
                NULL,
                hinst,
                0);

    _tabBarDefaultProc = (WNDPROC)(SetWindowLongPtr(_hSelf, GWLP_WNDPROC, (LONG_PTR)TabBarPlus_runProc));
    _hImglst = ImageList_Create(1, 1, ILC_COLOR32 | ILC_MASK, 0, 10);

    TabCtrl_SetItemExtra(_hSelf, sizeof(TabCtrlItem)-sizeof(TCITEMHEADER));
}

void destroy_tab_bar() {
    DestroyWindow(_hSelf);
}

void tab_bar_set_measurement(HFONT dpiAwareFont) {
    imageZone.cx = DPIManager_scaleX(16);
    imageZone.cy = DPIManager_scaleY(13);
    notifyBlinkZone.cx = DPIManager_scaleX(8);
    notifyBlinkZone.cy = DPIManager_scaleY(8);
    _closeButtonZone.cx = DPIManager_scaleX(11);
    _closeButtonZone.cy = DPIManager_scaleY(11);
    imagePaddingX = DPIManager_scaleX(4);
    closeButtonPaddingX = DPIManager_scaleX(5);

    if (_hFont) {
        DeleteObject(_hFont);
    }
    if (dpiAwareFont) {
        _hFont = dpiAwareFont;
    } else {
        _hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
    SendMessage(_hSelf, WM_SETFONT, (WPARAM)_hFont, (LPARAM)FALSE);

    ImageList_SetIconSize(_hImglst, imageZone.cx, imageZone.cy);

    for (int i=IDI_BACKEND_FIRST; i<=IDI_BACKEND_LAST; i++) {
        HICON hIcon = LoadImage(hinst, MAKEINTRESOURCE(i), IMAGE_ICON, imageZone.cx, imageZone.cy, 0);
        ImageList_AddIcon(_hImglst, hIcon);
        DestroyIcon(hIcon);
    }
    SendMessage(_hSelf, TCM_SETPADDING, 0, MAKELPARAM(DPIManager_scaleX(15), DPIManager_scaleY(5)));
    SendMessage(_hSelf, TCM_SETIMAGELIST, 0, (LPARAM)_hImglst);

    RECT r = {0, 0, 0, 0};
    TabCtrl_AdjustRect(_hSelf, TRUE, &r);
    tab_extra_width = 0;
    tab_extra_height = -r.top + r.bottom;
}

int tab_bar_get_extra_width() {
    return tab_extra_width;
}

int tab_bar_get_extra_height() {
    return tab_extra_height;
}

void tab_bar_adjust_window() {
    RECT rect;
    GetClientRect(frame_hwnd, &rect);
    SetWindowPos(_hSelf, NULL, 0, 0, rect.right, tab_extra_height, SWP_NOMOVE | SWP_NOZORDER);
}

void tab_bar_insert_tab(int index, const char *title, int image) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_TEXT|TCIF_IMAGE|TCIF_PARAM;
    tci.header.pszText = (LPSTR)title;
    tci.header.iImage = image;
    tci.notifyState = NOTIFY_NORMAL;
    tci.unusable = false;

    TabCtrl_InsertItem(_hSelf, index, &tci);
}

void tab_bar_remove_tab(int index) {
    TabCtrl_DeleteItem(_hSelf, index);
}

void tab_bar_select_tab(int index) {
    TabCtrl_SetCurSel(_hSelf, index);
}

int tab_bar_get_current_tab() {
    return TabCtrl_GetCurSel(_hSelf);
}

void tab_bar_set_tab_title(int index, const char *title) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_TEXT;
    tci.header.pszText = (LPSTR)title;

    TabCtrl_SetItem(_hSelf, index, &tci);
}

void tab_bar_set_tab_unusable(int index, bool unusable) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    tci.notifyState = NOTIFY_NORMAL;
    tci.unusable = unusable;

    if (TabCtrl_SetItem(_hSelf, index, &tci)) {
        RECT rect = getItemRect(index);
        InvalidateRect(_hSelf, &rect, FALSE);
    }
}

void tab_bar_set_tab_notified(int index) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    if (!TabCtrl_GetItem(_hSelf, index, &tci)) {
        return;
    }
    if (tci.unusable) {
        return;
    }
    if (tci.notifyState == NOTIFY_NORMAL) {
        tci.notifyState = NOTIFY_SET;
        TabCtrl_SetItem(_hSelf, index, &tci);
        RECT rect = getItemRect(index);
        InvalidateRect(_hSelf, &rect, FALSE);
        if (!notifyBlinkTimer) {
            notifyBlinkTimer = SetTimer(_hSelf, 1, 500, NULL);
        }
    } else if (tci.notifyState == NOTIFY_SET) {
        tci.notifyState = NOTIFY_SET_BLINK;
        TabCtrl_SetItem(_hSelf, index, &tci);
    }
}

void tab_bar_clear_tab_notified(int index) {
    TabCtrlItem tci;

    tci.header.mask = TCIF_PARAM;
    if (!TabCtrl_GetItem(_hSelf, index, &tci)) {
        return;
    }
    if (tci.notifyState != NOTIFY_NORMAL) {
        tci.notifyState = NOTIFY_NORMAL;
        TabCtrl_SetItem(_hSelf, index, &tci);
        RECT rect = getItemRect(index);
        InvalidateRect(_hSelf, &rect, FALSE);
    }
}
