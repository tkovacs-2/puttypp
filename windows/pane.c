typedef struct WinGuiFrontend WinGuiFrontend;

typedef struct Pane {
    TabBar tabbar;
    HWND term_hwnd;
    PointerArray tabbar_data;
    WinGuiFrontend *wgf_active;
    SizeTip size_tip;
} Pane;

HWND create_term_hwnd(const RECT *rect, void *user_data);
void possible_term_dimensions(WinGuiFrontend *wgf, const RECT *term_rect, int *cols, int *rows);

Pane *pane_create(const RECT *rect, PointerArraySetIndex set_index_callback) {
    Pane *pane = malloc(sizeof(Pane));
    tab_bar_init(&pane->tabbar, rect, pane);
    pointer_array_init(&pane->tabbar_data, set_index_callback);
    RECT r = *rect;
    r.top += tab_bar_common_height();
    pane->wgf_active = NULL;
    pane->term_hwnd = create_term_hwnd(&r, pane);
    size_tip_init(&pane->size_tip);
    return pane;
}

void pane_destroy(Pane *pane) {
    tab_bar_uninit(&pane->tabbar);
    pointer_array_uninit(&pane->tabbar_data);
    DestroyWindow(pane->term_hwnd);
    size_tip_uninit(&pane->size_tip);
    free(pane);
}

void pane_dpi_changed(Pane *pane) {
    tab_bar_dpi_changed(&pane->tabbar);
}

HDWP pane_adjust_window(Pane *pane, const RECT *rect, HDWP hdwp) {
    hdwp = tab_bar_adjust_window(&pane->tabbar, rect, hdwp);
    return DeferWindowPos(hdwp, pane->term_hwnd, NULL,
                          rect->left, rect->top + tab_bar_common_height(),
                          rect->right - rect->left, rect->bottom - rect->top - tab_bar_common_height(),
                          SWP_NOACTIVATE | SWP_NOZORDER);
}

void pane_select_tab(Pane *pane, int index) {
    tab_bar_clear_tab_notified(&pane->tabbar, index);
    tab_bar_select_tab(&pane->tabbar, index);
    pane->wgf_active = ((WinGuiFrontend *)pointer_array_get(&pane->tabbar_data, index));
}

int pane_get_active_tab(Pane *pane) {
    return tab_bar_get_active_tab(&pane->tabbar);
}

void pane_tabs_exchanged(Pane *pane, int old_index, int new_index) {
    pointer_array_exchange(&pane->tabbar_data, old_index, new_index);
}

int pane_delete_tab(Pane *pane, int index) {
    int new_index = -1;
    int current_index = tab_bar_get_active_tab(&pane->tabbar);
    if (pointer_array_size(&pane->tabbar_data) > 1 && current_index == index) {
        if (current_index+1 == pointer_array_size(&pane->tabbar_data)) {
            new_index = current_index-1;
        } else {
            new_index = current_index;
        }

    }
    tab_bar_remove_tab(&pane->tabbar, index);
    pointer_array_remove(&pane->tabbar_data, index);
    if (pointer_array_size(&pane->tabbar_data) == 0) {
        pane->wgf_active = NULL;
    }
    return new_index;
}

void pane_insert_tab(Pane *pane, int index, const char *title, int image, WinGuiFrontend *wgf) {
    tab_bar_insert_tab(&pane->tabbar, index, title, image);
    pointer_array_insert(&pane->tabbar_data, index, wgf);
}

void pane_set_tab_title(Pane *pane, int index, const char *title) {
    tab_bar_set_tab_title(&pane->tabbar, index, title);
}

void pane_set_tab_unusable(Pane *pane, int index, bool unusable) {
    tab_bar_set_tab_unusable(&pane->tabbar, index, unusable);
}

void pane_set_tab_notified(Pane *pane, int index) {
    tab_bar_set_tab_notified(&pane->tabbar, index);
}

void pane_show_scrollbar(Pane *pane, bool show) {
    LONG nflg, flag = GetWindowLongPtr(pane->term_hwnd, GWL_STYLE);
    if (show) {
        nflg |= WS_VSCROLL;
    } else {
        nflg &= ~WS_VSCROLL;
    }
    if (nflg != flag) {
        SetWindowLongPtr(pane->term_hwnd, GWL_STYLE, nflg);
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

WinGuiFrontend *pane_get_active_wgf(Pane *pane) {
    return pane->wgf_active;
}

WinGuiFrontend *pane_get_wgf(Pane *pane, int index) {
    return (WinGuiFrontend *)pointer_array_get(&pane->tabbar_data, index);
}

int pane_get_tab_count(Pane *pane) {
    return pointer_array_size(&pane->tabbar_data);
}

int pane_import_tab(Pane *pane, Pane *source, int index) {
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

void pane_set_focused(Pane *pane, bool focused) {
    tab_bar_set_focused(&pane->tabbar, focused);
    if (focused) {
        SetFocus(pane->term_hwnd);
    }
}

void pane_update_sizetip(Pane *pane, const RECT *rect, const POINT *pos) {
    RECT term_rect;
    pane_get_possible_term_rect(pane, rect, &term_rect);
    int cols, rows;
    possible_term_dimensions(pane->wgf_active, &term_rect, &cols, &rows);
    POINT p = {pos->x, pos->y + tab_bar_common_height()};
    size_tip_update(&pane->size_tip, cols, rows, &p);
}

void pane_move_sizetip(Pane *pane, const POINT *pos) {
    POINT p = {pos->x, pos->y + tab_bar_common_height()};
    size_tip_move(&pane->size_tip, &p);
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
