#include <windows.h>
#include <stdbool.h>
#include <assert.h>

#include "tabbar.h"
#include "pointerarray.h"
#include "panesizetip.h"
#include "finddlg.h"
#include "pane.h"

typedef struct Pane {
    TabBar tabbar;
    HWND term_hwnd;
    PointerArray tabbar_data;
    WinGuiSession *active_session;
    SizeTip size_tip;
    FindDlg finddlg;
    RECT rect;
} Pane;

HWND create_term_hwnd(const RECT *rect, void *user_data);
void possible_term_dimensions(WinGuiSession *wgf, const RECT *term_rect, int *cols, int *rows);

static void adjust_finddlg_rect(Pane *pane, RECT *rect) {
    if (GetWindowLongPtr(pane->term_hwnd, GWL_STYLE) & WS_VSCROLL) {
        RECT window_rect;
        RECT client_rect;
        GetWindowRect(pane->term_hwnd, &window_rect);
        GetClientRect(pane->term_hwnd, &client_rect);
        ClientToScreen(pane->term_hwnd, ((POINT *)&client_rect)+1);
        rect->right -= window_rect.right - client_rect.right + 1;
    }
    RECT client_rect;
    GetClientRect(GetParent(pane->term_hwnd), &client_rect);
    IntersectRect(rect, rect, &client_rect);
}

Pane *pane_create(const RECT *rect, PaneSetIndexCallback set_index_callback) {
    Pane *pane = malloc(sizeof(Pane));
    tab_bar_init(&pane->tabbar, rect, pane);
    pointer_array_init(&pane->tabbar_data, (PointerArraySetIndex)set_index_callback);
    RECT r = *rect;
    r.top += tab_bar_common_height();
    pane->active_session = NULL;
    pane->term_hwnd = create_term_hwnd(&r, pane);
    size_tip_init(&pane->size_tip);
    finddlg_init(&pane->finddlg, pane);
    pane->rect = *rect;
    return pane;
}

void pane_destroy(Pane *pane) {
    tab_bar_uninit(&pane->tabbar);
    pointer_array_uninit(&pane->tabbar_data);
    HWND term_hwnd = pane->term_hwnd;
    pane->term_hwnd = NULL;
    DestroyWindow(term_hwnd);
    size_tip_uninit(&pane->size_tip);
    finddlg_uninit(&pane->finddlg);
    free(pane);
}

void pane_dpi_changed(Pane *pane) {
    tab_bar_dpi_changed(&pane->tabbar);
    finddlg_dpi_changed(&pane->finddlg);
}

HDWP pane_adjust_window(Pane *pane, const RECT *rect, HDWP hdwp) {
    hdwp = tab_bar_adjust_window(&pane->tabbar, rect, hdwp);

    RECT r = {rect->left, rect->top + tab_bar_common_height(), rect->right, rect->bottom};
    hdwp = DeferWindowPos(hdwp, pane->term_hwnd, NULL,
                          r.left, r.top,
                          r.right - r.left, r.bottom - r.top,
                          SWP_NOACTIVATE | SWP_NOZORDER);

    if (pane->finddlg.hwnd) {
        adjust_finddlg_rect(pane, &r);
        finddlg_adjust_window(&pane->finddlg, &r);
    }
    pane->rect = *rect;
    return hdwp;
}

void pane_select_session(Pane *pane, int index) {
    tab_bar_clear_tab_notified(&pane->tabbar, index);
    tab_bar_select_tab(&pane->tabbar, index);
    pane->active_session = ((WinGuiSession *)pointer_array_get(&pane->tabbar_data, index));
}

int pane_get_active_session_index(Pane *pane) {
    return tab_bar_get_active_tab(&pane->tabbar);
}

void pane_sessions_exchanged(Pane *pane, int old_index, int new_index) {
    pointer_array_exchange(&pane->tabbar_data, old_index, new_index);
}

int pane_delete_session(Pane *pane, int index) {
    int new_index = -1;
    int current_index = tab_bar_get_active_tab(&pane->tabbar);
    if (pointer_array_size(&pane->tabbar_data) > 1 && current_index == index) {
        if (current_index+1 == pointer_array_size(&pane->tabbar_data)) {
            new_index = current_index-1;
        } else {
            new_index = current_index;
        }
    }
    tab_bar_cancel_dragging(&pane->tabbar);
    tab_bar_remove_tab(&pane->tabbar, index);
    pointer_array_remove(&pane->tabbar_data, index);
    if (pointer_array_size(&pane->tabbar_data) == 0 || new_index >= 0) {
        pane->active_session = NULL;
    }
    return new_index;
}

void pane_insert_session(Pane *pane, int index, const char *title, int image, WinGuiSession *wgf) {
    tab_bar_insert_tab(&pane->tabbar, index, title, image);
    pointer_array_insert(&pane->tabbar_data, index, wgf);
}

void pane_set_session_title(Pane *pane, int index, const char *title) {
    tab_bar_set_tab_title(&pane->tabbar, index, title);
}

void pane_set_session_unusable(Pane *pane, int index, bool unusable) {
    tab_bar_set_tab_unusable(&pane->tabbar, index, unusable);
}

void pane_set_session_notified(Pane *pane, int index) {
    tab_bar_set_tab_notified(&pane->tabbar, index);
}

void pane_term_hwnd_style(Pane *pane, bool scrollbar, bool sunken_edge) {
    LONG nflg, flag = GetWindowLongPtr(pane->term_hwnd, GWL_STYLE);
    LONG nexflag, exflag = GetWindowLongPtr(pane->term_hwnd, GWL_EXSTYLE);
    nflg = flag;
    nexflag = exflag;

    if (scrollbar) {
        nflg |= WS_VSCROLL;
    } else {
        nflg &= ~WS_VSCROLL;
    }
    if (sunken_edge) {
        nexflag |= WS_EX_CLIENTEDGE;
    } else {
        nexflag &= ~WS_EX_CLIENTEDGE;
    }
    if (nflg != flag || nexflag != exflag) {
        if (nflg != flag) {
            SetWindowLongPtr(pane->term_hwnd, GWL_STYLE, nflg);
        }
        if (nexflag != exflag) {
            SetWindowLongPtr(pane->term_hwnd, GWL_EXSTYLE, nexflag);
        }
        SetWindowPos(pane->term_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOCOPYBITS |
                     SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER |
                     SWP_FRAMECHANGED);
    }
}

void pane_set_scrollbar(Pane *pane, int total, int start, int page, bool redraw) {
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    SetScrollInfo(pane->term_hwnd, SB_VERT, &si, redraw);
}

int pane_get_scrollbar_track_pos(Pane *pane) {
    SCROLLINFO si;

    si.cbSize = sizeof(si);
    si.fMask = SIF_TRACKPOS;
    if (GetScrollInfo(pane->term_hwnd, SB_VERT, &si) == 0) {
        return -1;
    }
    return si.nTrackPos;
}

WinGuiSession *pane_get_active_session(Pane *pane) {
    return pane->active_session;
}

WinGuiSession *pane_get_session(Pane *pane, int index) {
    return (WinGuiSession *)pointer_array_get(&pane->tabbar_data, index);
}

int pane_get_session_count(Pane *pane) {
    return pointer_array_size(&pane->tabbar_data);
}

int pane_import_session(Pane *pane, Pane *source, int index) {
    assert(pane != source);

    int target_index = pointer_array_size(&pane->tabbar_data);
    tab_bar_import_tab(&pane->tabbar, &source->tabbar, target_index, index);
    pointer_array_insert(&pane->tabbar_data, target_index, pointer_array_get(&source->tabbar_data, index));
    return target_index;
}

Pane *pane_get_from_tab_bar_hwnd(HWND hwnd) {
    TabBar *tab_bar = tab_bar_get_from_hwnd(hwnd);
    return (Pane *)tab_bar->user_data;
}

Pane *pane_get_from_term_hwnd(HWND hwnd) {
    return (Pane *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

Pane *pane_get_from_finddlg_hwnd(HWND hwnd) {
    FindDlg *finddlg = finddlg_get_from_hwnd(hwnd);
    return (Pane *)finddlg->user_data;
}

void pane_set_focused(Pane *pane, bool focused) {
    if (pane->term_hwnd == NULL) {
        return;
    }
    tab_bar_set_focused(&pane->tabbar, focused);
    if (focused) {
        SetFocus(pane->term_hwnd);
    }
}

void pane_update_sizetip(Pane *pane, const RECT *planned_rect, const RECT *rect) {
    RECT term_rect;
    pane_get_possible_term_rect(pane, planned_rect, &term_rect);
    int cols, rows;
    possible_term_dimensions(pane->active_session, &term_rect, &cols, &rows);
    RECT sizetip_rect = {rect->left, rect->top + tab_bar_common_height(), rect->right, rect->bottom};
    size_tip_update(&pane->size_tip, cols, rows, &sizetip_rect);
}

HDWP pane_move_sizetip(Pane *pane, const POINT *pos, HDWP hdwp) {
    POINT p = {pos->x, pos->y + tab_bar_common_height()};
    return size_tip_move(&pane->size_tip, &p, hdwp);
}

void pane_hide_sizetip(Pane *pane) {
    size_tip_hide(&pane->size_tip);
}

void pane_get_term_rect(Pane *pane, RECT *rect) {
    GetClientRect(pane->term_hwnd, rect);
}

void pane_get_possible_term_rect(Pane *pane, const RECT *pane_rect, RECT *rect) {
    RECT window_rect;
    RECT client_rect;
    GetWindowRect(pane->term_hwnd, &window_rect);
    GetClientRect(pane->term_hwnd, &client_rect);
    int ew = (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
    int eh = (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
    rect->right = (pane_rect->right - pane_rect->left) - ew;
    rect->bottom = (pane_rect->bottom - (pane_rect->top + tab_bar_common_height())) - eh;
    rect->left = 0;
    rect->top = 0;
}

void pane_get_requied_rect(Pane *pane, const RECT *term_rect, RECT *rect) {
    RECT window_rect;
    RECT client_rect;
    GetWindowRect(pane->term_hwnd, &window_rect);
    GetClientRect(pane->term_hwnd, &client_rect);
    int ew = (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
    int eh = (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
    rect->right = (term_rect->right - term_rect->left) + ew;
    rect->bottom = (term_rect->bottom - term_rect->top) + eh + tab_bar_common_height();
    rect->left = 0;
    rect->top = 0;
}

void pane_clear_active_session(Pane *pane) {
    pane->active_session = NULL;
}

HDWP pane_pin_window(Pane *pane, HDWP hdwp) {
    return finddlg_pin_window(&pane->finddlg, hdwp);
}

void pane_show_finddlg(Pane *pane, WCHAR *pattern, bool activate, bool ignore_case, bool whole_word) {
    RECT r = {pane->rect.left, pane->rect.top + tab_bar_common_height(), pane->rect.right, pane->rect.bottom};
    adjust_finddlg_rect(pane, &r);
    finddlg_show(&pane->finddlg, &r, pattern, activate, ignore_case, whole_word);
}

void pane_hide_finddlg(Pane *pane) {
    finddlg_hide(&pane->finddlg);
}

int pane_get_finddlg_text(Pane *pane, WCHAR *buffer, int buffer_chars) {
    return finddlg_get_text(&pane->finddlg, buffer, buffer_chars);
}

bool pane_get_finddlg_ignore_case(Pane *pane) {
    return finddlg_get_ignore_case(&pane->finddlg);
}

bool pane_get_finddlg_whole_word(Pane *pane) {
    return finddlg_get_whole_word(&pane->finddlg);
}

HWND pane_get_term_hwnd(Pane *pane) {
    return pane->term_hwnd;
}

void pane_get_active_session_hotspot(Pane *pane, POINT *hotspot) {
    tab_bar_get_selected_tab_hotspot(&pane->tabbar, hotspot);
}
