#include <windows.h>
#include <windowsx.h>
#include <stdbool.h>
#include <assert.h>

#include "pane.h"
#include "split.h"
#include "tabbar.h"

#define SPLITTER_WIDTH 6
#define SPLITTER_HEIGHT 6
#define MIN_PANE_WIDTH 40
#define MIN_PANE_HEIGHT 50
#define IDM_SPLITTER_RATIO_50_50 1
#define IDM_SPLITTER_RATIO_25_75 2
#define IDM_SPLITTER_RATIO_75_25 3
#define IDM_SPLITTER_MERGE 4
#define IDM_SPLITTER_DRAG_PANES 5

#define DEFER_BUFFER_SIZE 64

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

extern HINSTANCE hinst;
extern HWND frame_hwnd;
extern POINT dpi_info;

static const WCHAR SPLITTER_CLASS_NAME[] = L"SplitterWindow";
static const WCHAR SPLITTER_MOVE_CLASS_NAME[] = L"SplitterMoveWindow";
static Split *dragging_splitter = NULL;
static BOOL showing_splitter_menu = FALSE;
static BOOL drag_full_windows = FALSE;
static HWND splitter_move_hwnd = NULL;
static PaneSetIndexCallback pane_set_index_callback;
static int splitter_width;
static int splitter_height;
static int min_pane_width;
static int min_pane_height;
static const float equal_split_ratio = 0.5f;

typedef struct Pane Pane;

typedef struct Split {
    SplitType type;
    RECT rect;
    RECT planned_rect;
    Split *first;
    Split *second;
    Pane *pane;
    HWND splitter_hwnd;
    HCURSOR splitter_cursor;
    float split_ratio;
    float planned_split_ratio;
} Split;

static int dpi_scalex_splitter(int x)
{
    return MulDiv(x, dpi_info.x, 96);
};

static int dpi_scaley_splitter(int y)
{
    return MulDiv(y, dpi_info.y, 96);
};

static int get_rect_split_from_point(SplitType type, const RECT *rect, const POINT *point) {
    int x;
    int left;
    int right;
    int splitter_size;
    if (type == SPLIT_TYPE_VERTICAL) {
        x = point->x;
        left = rect->left;
        right = rect->right;
        splitter_size = splitter_width;
    } else {
        x = point->y;
        left = rect->top;
        right = rect->bottom;
        splitter_size = splitter_height;
    }

    int rect_split = x - (splitter_size / 2);
    if (rect_split > right - splitter_size) {
        rect_split = right - splitter_size;
    }
    if (rect_split < left) {
        rect_split = left;
    }
    return rect_split;
}

static int get_rect_split_from_ratio(SplitType type, float split_ratio, const RECT *rect) {
    if (type == SPLIT_TYPE_VERTICAL) {
        return (rect->right - rect->left - splitter_width) * split_ratio + rect->left;
    } else {
        return (rect->bottom - rect->top - splitter_height) * split_ratio + rect->top;
    }
}

static bool is_split_allowed(SplitType type, const RECT *rect) {
    int rect_split = get_rect_split_from_ratio(type, equal_split_ratio, rect);
    if (type == SPLIT_TYPE_VERTICAL) {
        return (rect_split - rect->left >= min_pane_width);
    } else {
        return (rect_split - rect->top >= min_pane_height);
    }
}

static void apply_rect_split(SplitType type, const RECT *rect, int rect_split, RECT *first, RECT *second) {
    if (type == SPLIT_TYPE_VERTICAL) {
        first->left = rect->left;
        first->top = rect->top;
        first->right = rect_split;
        first->bottom = rect->bottom;
        second->left = first->right + splitter_width;
        second->top = rect->top;
        second->right = rect->right;
        second->bottom = rect->bottom;
    } else {
        first->left = rect->left;
        first->top = rect->top;
        first->right = rect->right;
        first->bottom = rect_split;
        second->left = rect->left;
        second->top = first->bottom + splitter_height;
        second->right = rect->right;
        second->bottom = rect->bottom;
    }
}

static Split *create_split(const RECT *rect, Pane *pane) {
    Split *split = (Split *)malloc(sizeof(Split));
    split->type = SPLIT_TYPE_PANE;
    split->rect = *rect;
    split->planned_rect = *rect;
    split->first = NULL;
    split->second = NULL;
    split->pane = pane;
    split->splitter_hwnd = NULL;
    split->splitter_cursor = NULL;
    split->split_ratio = 0.0f;
    split->planned_split_ratio = 0.0f;
    return split;
}

static void create_splitter(Split *split) {
    int x, y, width, height;
    if (split->type == SPLIT_TYPE_VERTICAL) {
        x = split->first->rect.right;
        y = split->rect.top;
        width = splitter_width;
        height = split->rect.bottom - split->rect.top;
    } else {
        x = split->rect.left;
        y = split->first->rect.bottom;
        width = split->rect.right - split->rect.left;
        height = splitter_height;
    }
    split->splitter_hwnd = CreateWindowExW(0, SPLITTER_CLASS_NAME,
                            NULL,
                            WS_CHILD | WS_VISIBLE,
                            x, y, width, height,
                            frame_hwnd, NULL, hinst, split);
}

static HDWP adjust_splitter_window(Split *split, HDWP hdwp) {
    int x, y;
    int width, height;
    if (split->type == SPLIT_TYPE_VERTICAL) {
        x = split->first->rect.right;
        y = split->rect.top;
        width = splitter_width;
        height = split->rect.bottom - split->rect.top;
    } else {
        x = split->rect.left;
        y = split->first->rect.bottom;
        width = split->rect.right - split->rect.left;
        height = splitter_height;
    }
    return DeferWindowPos(hdwp, split->splitter_hwnd, NULL, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOCOPYBITS);
}

static void create_splitter_move(Split *split) {
    if (splitter_move_hwnd) {
        return;
    }

    RECT rect;
    GetWindowRect(split->splitter_hwnd, &rect);
    splitter_move_hwnd = CreateWindowExW(0, SPLITTER_MOVE_CLASS_NAME,
                            NULL,
                            WS_POPUP,
                            rect.left, rect.top,
                            rect.right - rect.left, rect.bottom - rect.top,
                            frame_hwnd, NULL, hinst, NULL);
    SetWindowPos(splitter_move_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE|SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
}

static void destroy_splitter_move() {
    if (!splitter_move_hwnd) {
        return;
    }

    DestroyWindow(splitter_move_hwnd);
    splitter_move_hwnd = NULL;
}

static void move_splitter_move(Split *split) {
    POINT pt;
    if (split->type == SPLIT_TYPE_VERTICAL) {
        pt.x = split->first->planned_rect.right;
        pt.y = split->planned_rect.top;
    } else {
        pt.x = split->planned_rect.left;
        pt.y = split->first->planned_rect.bottom;
    }
    ClientToScreen(frame_hwnd, &pt);
    SetWindowPos(splitter_move_hwnd, NULL, pt.x, pt.y, 0, 0, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSIZE | SWP_NOCOPYBITS);
}

static void plan_split_ratio(Split *split) {
    int rect_split;
    int left;
    int right;
    int splitter_size;
    if (split->type == SPLIT_TYPE_VERTICAL) {
        rect_split = split->first->planned_rect.right;
        left = split->planned_rect.left;
        right = split->planned_rect.right;
        splitter_size = splitter_width;
    } else {
        rect_split = split->first->planned_rect.bottom;
        left = split->planned_rect.top;
        right = split->planned_rect.bottom;
        splitter_size = splitter_height;
    }
    split->planned_split_ratio = (float)(rect_split - left) / (right - left - splitter_size);
}

static HDWP apply_layout(Split *split, HDWP hdwp) {
    split->rect = split->planned_rect;
    split->split_ratio = split->planned_split_ratio;
    if (split->type == SPLIT_TYPE_PANE) {
        hdwp = pane_adjust_window(split->pane, &split->rect, hdwp);
    } else {
        hdwp = apply_layout(split->first, hdwp);
        hdwp = adjust_splitter_window(split, hdwp);
        hdwp = apply_layout(split->second, hdwp);
    }
    return hdwp;
}

static HDWP pin_layout(Split *split, HDWP hdwp) {
    if (split->type == SPLIT_TYPE_PANE) {
        return pane_pin_window(split->pane, hdwp);
    }
    hdwp = pin_layout(split->first, hdwp);
    hdwp = pin_layout(split->second, hdwp);
    return hdwp;
}

static HDWP move_sizetips(Split *split, HDWP hdwp) {
    if (split->type == SPLIT_TYPE_PANE) {
        return pane_move_sizetip(split->pane, (const POINT *)&split->rect, hdwp);
    }
    hdwp = move_sizetips(split->first, hdwp);
    hdwp = move_sizetips(split->second, hdwp);
    return hdwp;
}

static void apply_split_ratio(Split *split, float split_ratio) {
    split->split_ratio = split_ratio;
    split_plan_layout(split, &split->rect);
    split->planned_split_ratio = split_ratio;
    split_apply_layout(split);
}

static void notify_frame(int code, HWND splitter_hwnd) {
    NMHDR hdr;
    hdr.hwndFrom = splitter_hwnd;
    hdr.idFrom = SPLITTER_NOTIFY_ID;
    hdr.code = code;
    SendMessage(frame_hwnd, WM_NOTIFY, (WPARAM)splitter_hwnd, (LPARAM)&hdr);
}

static void show_splitter_menu(Split *split, HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuA(menu, MF_STRING | (drag_full_windows ? MF_CHECKED : MF_UNCHECKED), IDM_SPLITTER_DRAG_PANES, "Drag panes");
    AppendMenuA(menu, MF_SEPARATOR, 0, 0);
    AppendMenuA(menu, MF_STRING, IDM_SPLITTER_RATIO_50_50, "50:50");
    AppendMenuA(menu, MF_STRING, IDM_SPLITTER_RATIO_25_75, "25:75");
    AppendMenuA(menu, MF_STRING, IDM_SPLITTER_RATIO_75_25, "75:25");
    if (split->first->pane && split->second->pane) {
        AppendMenuA(menu, MF_STRING, IDM_SPLITTER_MERGE, "Merge");
    }
    showing_splitter_menu = TRUE;
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 x, y, 0, hwnd, NULL);
    showing_splitter_menu = FALSE;
    DestroyMenu(menu);

    switch (command) {
    case IDM_SPLITTER_DRAG_PANES:
        drag_full_windows = !drag_full_windows;
        break;
    case IDM_SPLITTER_RATIO_50_50:
        apply_split_ratio(split, 0.5f);
        break;
    case IDM_SPLITTER_RATIO_25_75:
        apply_split_ratio(split, 0.25f);
        break;
    case IDM_SPLITTER_RATIO_75_25:
        apply_split_ratio(split, 0.75f);
        break;
    case IDM_SPLITTER_MERGE:
        notify_frame(SPLITTER_NOTIFY_MERGE, hwnd);
        break;
    }
}

static void plan_layout(Split *split, const RECT *rect, int rect_split) {
    split->planned_rect = *rect;

    RECT first, second;
    apply_rect_split(split->type, rect, rect_split, &first, &second);

    split_plan_layout(split->first, &first);
    if (split->type == SPLIT_TYPE_VERTICAL) {
        if (split->first->planned_rect.right > first.right) {
            second.left = split->first->planned_rect.right + splitter_width;
            split_plan_layout(split->second, &second);
        } else {
            split_plan_layout(split->second, &second);
            if (split->second->planned_rect.right > second.right) {
                first.right -= (split->second->planned_rect.right - second.right);
                int planned_right = split->first->planned_rect.right;
                split_plan_layout(split->first, &first);
                if (split->first->planned_rect.right < planned_right) {
                    int d = planned_right - split->first->planned_rect.right;
                    second = split->second->planned_rect;
                    second.left -= d;
                    second.right -= d;
                    split_plan_layout(split->second, &second);
                }
            }
        }
        split->planned_rect.right = split->second->planned_rect.right;
        Split *replan = NULL;
        if (split->first->planned_rect.top < split->second->planned_rect.top) {
            split->planned_rect.top = split->first->planned_rect.top;
            replan = split->second;
        } else if (split->second->planned_rect.top < split->first->planned_rect.top) {
            split->planned_rect.top = split->second->planned_rect.top;
            replan = split->first;
        } else if (split->first->planned_rect.bottom > split->second->planned_rect.bottom) {
            split->planned_rect.bottom = split->first->planned_rect.bottom;
            replan = split->second;
        } else if (split->second->planned_rect.bottom > split->first->planned_rect.bottom) {
            split->planned_rect.bottom = split->second->planned_rect.bottom;
            replan = split->first;
        }
        if (replan) {
            RECT r = replan->planned_rect;
            r.top = split->planned_rect.top;
            r.bottom = split->planned_rect.bottom;
            split_plan_layout(replan, &r);
        }
        if (split->first->planned_rect.top < split->planned_rect.top) {
            split->planned_rect.top = split->first->planned_rect.top;
        }
        if (split->first->planned_rect.bottom > split->planned_rect.bottom) {
            split->planned_rect.bottom = split->first->planned_rect.bottom;
        }
    } else {
        if (split->first->planned_rect.bottom > first.bottom) {
            second.top = split->first->planned_rect.bottom + splitter_height;
            split_plan_layout(split->second, &second);
        } else {
            split_plan_layout(split->second, &second);
            if (split->second->planned_rect.bottom > second.bottom) {
                first.bottom -= (split->second->planned_rect.bottom - second.bottom);
                int planned_bottom = split->first->planned_rect.bottom;
                split_plan_layout(split->first, &first);
                if (split->first->planned_rect.bottom < planned_bottom) {
                    int d = planned_bottom - split->first->planned_rect.bottom;
                    second = split->second->planned_rect;
                    second.top -= d;
                    second.bottom -= d;
                    split_plan_layout(split->second, &second);
                }
            }
        }
        split->planned_rect.bottom = split->second->planned_rect.bottom;
        Split *replan = NULL;
        if (split->first->planned_rect.left < split->second->planned_rect.left) {
            split->planned_rect.left = split->first->planned_rect.left;
            replan = split->second;
        } else if (split->second->planned_rect.left < split->first->planned_rect.left) {
            split->planned_rect.left = split->second->planned_rect.left;
            replan = split->first;
        } else if (split->first->planned_rect.right > split->second->planned_rect.right) {
            split->planned_rect.right = split->first->planned_rect.right;
            replan = split->second;
        } else if (split->second->planned_rect.right > split->first->planned_rect.right) {
            split->planned_rect.right = split->second->planned_rect.right;
            replan = split->first;
        }
        if (replan) {
            RECT r = replan->planned_rect;
            r.left = split->planned_rect.left;
            r.right = split->planned_rect.right;
            split_plan_layout(replan, &r);
        }
        if (split->first->planned_rect.left < split->planned_rect.left) {
            split->planned_rect.left = split->first->planned_rect.left;
        }
        if (split->first->planned_rect.right > split->planned_rect.right) {
            split->planned_rect.right = split->first->planned_rect.right;
        }
    }
}

static void paint_splitter(HWND hwnd, HDC hdc) {
    RECT client;
    GetClientRect(hwnd, &client);
    FillRect(hdc, &client, (HBRUSH)(COLOR_BTNFACE + 1));
    DrawEdge(hdc, &client, EDGE_BUMP, BF_RECT);
}

static void end_splitter_move() {
    if (dragging_splitter) {
        HWND splitter_hwnd = dragging_splitter->splitter_hwnd;
        if (splitter_move_hwnd) {
            InvalidateRect(splitter_hwnd, NULL, FALSE);
            destroy_splitter_move();
        }
        split_hide_sizetips(dragging_splitter);
        dragging_splitter = NULL;
        ReleaseCapture();
        notify_frame(SPLITTER_NOTIFY_EXIT_DRAG, splitter_hwnd);
    }
}

static LRESULT CALLBACK splitter_move_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        paint_splitter(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_DPICHANGED:
        return 0;
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK splitter_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    Split *split = (Split *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (message) {
      case WM_CREATE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA *)lparam)->lpCreateParams);
        return 0;

      case WM_SETCURSOR:
        SetCursor(showing_splitter_menu ? LoadCursor(NULL, IDC_ARROW) : split->splitter_cursor);
        return TRUE;

      case WM_LBUTTONDOWN:
        dragging_splitter = split;
        if (!drag_full_windows) {
            create_splitter_move(split);
        }
        SetCapture(hwnd);
        SetCursor(split->splitter_cursor);
        notify_frame(SPLITTER_NOTIFY_ENTER_DRAG, hwnd);
        return 0;

      case WM_MOUSEMOVE: {
        POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        MapWindowPoints(hwnd, frame_hwnd, &point, 1);
        SendMessage(frame_hwnd, WM_MOUSEMOVE, wparam, MAKELPARAM(point.x, point.y));
        if (dragging_splitter) {
            plan_layout(split, &split->rect, get_rect_split_from_point(split->type, &split->rect, &point));
            if (!splitter_move_hwnd) {
                plan_split_ratio(split);
                split_apply_layout(split);
                split_move_sizetips(split);
            } else {
                move_splitter_move(split);
            }
            split_update_sizetips(split);
            return 0;
        }
        break;
      }
      case WM_LBUTTONUP:
        if (dragging_splitter && splitter_move_hwnd) {
            plan_split_ratio(split);
            split_apply_layout(split);
        }
        end_splitter_move();
        return 0;

      case WM_CAPTURECHANGED:
        end_splitter_move();
        return 0;

      case WM_RBUTTONDOWN: {
        if (dragging_splitter) {
            end_splitter_move();
        } else {
            POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ClientToScreen(hwnd, &point);
            show_splitter_menu(split, hwnd, point.x, point.y);
        }
        return 0;
      }

      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (splitter_move_hwnd && dragging_splitter == split) {
            FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(GRAY_BRUSH));
        } else {
            paint_splitter(hwnd, hdc);
        }
        EndPaint(hwnd, &ps);
        return 0;
      }
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void split_common_init(PaneSetIndexCallback set_index_callback) {
    WNDCLASSW splitter_class;
    ZeroMemory(&splitter_class, sizeof(splitter_class));
    splitter_class.lpfnWndProc = splitter_proc;
    splitter_class.hInstance = hinst;
    splitter_class.hCursor = NULL;
    splitter_class.hbrBackground = NULL;
    splitter_class.lpszClassName = SPLITTER_CLASS_NAME;
    RegisterClassW(&splitter_class);

    splitter_class.lpfnWndProc = splitter_move_proc;
    splitter_class.lpszClassName = SPLITTER_MOVE_CLASS_NAME;
    RegisterClassW(&splitter_class);

    pane_set_index_callback = set_index_callback;

    split_common_dpi_changed();
}

void split_common_dpi_changed() {
    splitter_width = dpi_scalex_splitter(SPLITTER_WIDTH);
    splitter_height = dpi_scaley_splitter(SPLITTER_HEIGHT);
    min_pane_width = dpi_scalex_splitter(MIN_PANE_WIDTH);
    min_pane_height = dpi_scaley_splitter(MIN_PANE_HEIGHT);
}

Split *split_create(const RECT *rect) {
    return create_split(rect, pane_create(rect, pane_set_index_callback));
}

void split_destroy(Split *split) {
    if (split->type == SPLIT_TYPE_PANE) {
        if (split->pane) {
            pane_destroy(split->pane);
        }
    } else {
        split_destroy(split->first);
        split_destroy(split->second);
        DestroyWindow(split->splitter_hwnd);
    }
    free(split);
}

void split_split(Split *split, SplitType type, SplitPart new_pane) {
    assert(split->type == SPLIT_TYPE_PANE && type != SPLIT_TYPE_PANE);
    int rect_split = get_rect_split_from_ratio(type, equal_split_ratio, &split->rect);
    if (rect_split < (type == SPLIT_TYPE_VERTICAL ? min_pane_width : min_pane_height)) {
        return;
    }

    RECT first, second;
    apply_rect_split(type, &split->rect, rect_split, &first, &second);

    if (new_pane == SPLIT_PART_SECOND) {
        split->first = create_split(&first, split->pane);
        split_apply_layout(split->first);
        split->second = create_split(&second, pane_create(&second, pane_set_index_callback));
    } else {
        split->first = create_split(&first, pane_create(&first, pane_set_index_callback));
        split->second = create_split(&second, split->pane);
        split_apply_layout(split->second);
    }
    split->type = type;
    split->split_ratio = equal_split_ratio;
    split->planned_split_ratio = equal_split_ratio;
    split->pane = NULL;
    split->splitter_cursor = LoadCursor(NULL, type == SPLIT_TYPE_VERTICAL ? IDC_SIZEWE : IDC_SIZENS);
    create_splitter(split);
}

Pane *split_merge(Split *split) {
    assert(split->type != SPLIT_TYPE_PANE);
    end_splitter_move();

    Split *destroyed;
    Split *extended;
    if (split->first->pane && split->second->pane &&
        pane_get_session_count(split->first->pane) > 0 &&
        pane_get_session_count(split->second->pane) > 0) {
        destroyed = split->second;
        extended = split->first;
    } else if (split->first->pane && pane_get_session_count(split->first->pane) == 0) {
        destroyed = split->first;
        extended = split->second;
    } else if (split->second->pane && pane_get_session_count(split->second->pane) == 0) {
        destroyed = split->second;
        extended = split->first;
    } else {
        assert(false);
    }

    DestroyWindow(split->splitter_hwnd);
    RECT rect = split->rect;

    split->pane = extended->pane;
    split->type = extended->type;
    split->split_ratio = extended->split_ratio;
    split->first = extended->first;
    split->second = extended->second;
    split->splitter_hwnd = extended->splitter_hwnd;
    split->splitter_cursor = extended->splitter_cursor;
    split->rect = extended->rect;
    free(extended);
    if (split->splitter_hwnd) {
        SetWindowLongPtr(split->splitter_hwnd, GWLP_USERDATA, (LONG_PTR)split);
    }

    Pane *pane = destroyed->pane;
    destroyed->pane = NULL;
    split_destroy(destroyed);
    split_plan_layout(split, &rect);
    split_apply_layout(split);
    return pane;
}

void split_plan_layout(Split *split, const RECT *rect) {
    if (split->type == SPLIT_TYPE_PANE) {
        split->planned_rect.left = rect->left;
        split->planned_rect.top = rect->top;
        if (rect->right - rect->left < min_pane_width) {
            split->planned_rect.right = rect->left + min_pane_width;
        } else {
            split->planned_rect.right = rect->right;
        }
        if (rect->bottom - rect->top < min_pane_height) {
            split->planned_rect.bottom = rect->top + min_pane_height;
        } else {
            split->planned_rect.bottom = rect->bottom;
        }
        return;
    }
    plan_layout(split, rect, get_rect_split_from_ratio(split->type, split->split_ratio, rect));
}

void split_apply_layout(Split *split) {
    HDWP hdwp = BeginDeferWindowPos(DEFER_BUFFER_SIZE);
    hdwp = apply_layout(split, hdwp);
    EndDeferWindowPos(hdwp);
}

const RECT *split_get_rect(Split *split) {
    return &split->rect;
}

Pane *split_get_pane(Split *split) {
    return split->pane;
}

Split *split_get_first(Split *split) {
    return split->first;
}

Split *split_get_second(Split *split) {
    return split->second;
}

void split_dpi_changed(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        pane_dpi_changed(pane);
    } else {
        split_dpi_changed(split_get_first(split));
        split_dpi_changed(split_get_second(split));
    }
}

Split *split_get_from_point(Split *split, const POINT *point) {
    if (point->x < split->rect.left || point->x >= split->rect.right ||
        point->y < split->rect.top || point->y >= split->rect.bottom) {
        return NULL;
    }
    if (split->type == SPLIT_TYPE_PANE) {
        return split;
    }
    Split *result = NULL;
    if ((result = split_get_from_point(split->first, point))) {
        return result;
    }
    if ((result = split_get_from_point(split->second, point))) {
        return result;
    }
    return NULL;
}

SplitType split_get_possible_split(Split *split, bool slim, const POINT *point, SplitPart *part) {
    int top = split->rect.top + (slim ? tab_bar_common_height() : 0);
    int w = (split->rect.right - split->rect.left) / 4;
    int h = (split->rect.bottom - top) / 4;
    int x = point->x - (split->rect.left + split->rect.right) / 2;
    int y = point->y - (top + split->rect.bottom) / 2;
    int abs_x = x < 0 ? -x : x;
    int abs_y = y < 0 ? -y : y;

    if (abs_x <= w && abs_y <= h) {
        return SPLIT_TYPE_PANE;
    }
    bool vertical = is_split_allowed(SPLIT_TYPE_VERTICAL, &split->rect);
    bool horizontal = is_split_allowed(SPLIT_TYPE_HORIZONTAL, &split->rect);
    if (!vertical && !horizontal) {
        return SPLIT_TYPE_PANE;
    }

    if (vertical && ((abs_x > w && abs_y <= h) || (!horizontal && abs_x > w))) {
        *part = x < 0 ? SPLIT_PART_FIRST : SPLIT_PART_SECOND;
        return SPLIT_TYPE_VERTICAL;
    }
    if (horizontal && ((abs_y > h && abs_x <= w) || (!vertical && abs_y > h))) {
        *part = y < 0 ? SPLIT_PART_FIRST : SPLIT_PART_SECOND;
        return SPLIT_TYPE_HORIZONTAL;
    }

    bool below_diagonal = abs_y * w > abs_x * h;
    if (horizontal && below_diagonal) {
        *part = y < 0 ? SPLIT_PART_FIRST : SPLIT_PART_SECOND;
        return SPLIT_TYPE_HORIZONTAL;
    }
    if (vertical && !below_diagonal) {
        *part = x < 0 ? SPLIT_PART_FIRST : SPLIT_PART_SECOND;
        return SPLIT_TYPE_VERTICAL;
    }
    return SPLIT_TYPE_PANE;
}

SplitType split_get_possible_split_rect(Split *split, bool slim, const POINT *point, RECT *rect) {
    SplitType type;
    SplitPart part;
    type = split_get_possible_split(split, slim, point, &part);
    if (type == SPLIT_TYPE_PANE) {
        *rect = split->rect;
        return type;
    }
    RECT first, second;
    apply_rect_split(type, &split->rect, get_rect_split_from_ratio(type, equal_split_ratio, &split->rect), &first, &second);
    *rect = (part == SPLIT_PART_FIRST ? first : second);
    if (slim && (type != SPLIT_TYPE_HORIZONTAL || part != SPLIT_PART_SECOND)) {
        rect->top += tab_bar_common_height();
    }
    return type;
}

Split *split_find_parent(Split *split, Pane *pane) {
    if (split->type == SPLIT_TYPE_PANE) {
        return NULL;
    }
    if (split->first->pane == pane || split->second->pane == pane) {
        return split;
    }
    Split *result = split_find_parent(split->first, pane);
    if (result) {
        return result;
    }
    return split_find_parent(split->second, pane);
}

Split *split_get_from_hwnd(HWND hwnd) {
    return (Split *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

void split_update_sizetips(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        pane_update_sizetip(pane, &split->planned_rect, &split->rect);
    } else {
        split_update_sizetips(split_get_first(split));
        split_update_sizetips(split_get_second(split));
    }
}

void split_move_sizetips(Split *split) {
    HDWP hdwp = BeginDeferWindowPos(DEFER_BUFFER_SIZE);
    hdwp = move_sizetips(split, hdwp);
    EndDeferWindowPos(hdwp);
}

void split_hide_sizetips(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        pane_hide_sizetip(pane);
    } else {
        split_hide_sizetips(split_get_first(split));
        split_hide_sizetips(split_get_second(split));
    }
}

Pane *split_find_pane(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        return pane;
    }
    pane = split_find_pane(split_get_first(split));
    if (pane) {
        return pane;
    }
    return split_find_pane(split_get_second(split));
}

void split_pin_layout(Split *split) {
    HDWP hdwp = BeginDeferWindowPos(DEFER_BUFFER_SIZE);
    hdwp = pin_layout(split, hdwp);
    EndDeferWindowPos(hdwp);
}