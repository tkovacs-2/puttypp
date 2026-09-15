
static int session_counter = 1;

static void destroy_sessions(Split *split);
static void handle_finddlg_notify(NMHDR *hdr);
static void handle_tab_bar_notify(TabBarNotify *notify);
static void handle_wm_mouse_move(WPARAM wparam, LPARAM lparam);
static void handle_wm_ncmouse_move(WPARAM wparam, LPARAM lparam);
static void message_term_hwnds(Split *split, UINT message, WPARAM wparam, LPARAM lparam);
static void handle_minimised_change();

static void calculate_term_dimensions(WinGuiSession *wgs, const RECT *term_rect, int *cols, int *rows) {
    int border = conf_get_int(wgs->conf, CONF_window_border);
    int w = term_rect->right - border*2;
    int h = term_rect->bottom - border*2;
    *cols = w/wgs->font_width;
    *rows = h/wgs->font_height;
    if (*cols < 5) {
        *cols = 5;
    }
    if (*rows < 1) {
        *rows = 1;
    }
}

static void calculate_font_size(WinGuiSession *wgs, const RECT *term_rect, int *width, int *height) {
    int border = conf_get_int(wgs->conf, CONF_window_border);
    int w = term_rect->right - border*2;
    int h = term_rect->bottom - border*2;
    *width = w/wgs->term->cols;
    *height = h/wgs->term->rows;
    if (*width < 2) {
        *width = 2;
    }
    if (*height < 2) {
        *height = 2;
    }
}

static void gaps_font(WinGuiSession *wgs, const RECT *term_rect, int *width, int *height) {
    calculate_font_size(wgs, term_rect, width, height);
    *width *= wgs->term->cols;
    *height *= wgs->term->rows;
}

static void gaps_term(WinGuiSession *wgs, const RECT *term_rect, int *width, int *height) {
    calculate_term_dimensions(wgs, term_rect, width, height);
    *width *= wgs->font_width;
    *height *= wgs->font_height;
}

static void gaps_term_init(WinGuiSession *wgs, const RECT *term_rect, int *width, int *height) {
    int w = wgs->font_width;
    int h = wgs->font_height;
    wgs->font_width = wgs->resize_either.init_font_width;
    wgs->font_height = wgs->resize_either.init_font_height;
    gaps_term(wgs, term_rect, width, height);
    wgs->font_width = w;
    wgs->font_height = h;
}

static void gaps_either(WinGuiSession *wgs, const RECT *term_rect, int *width, int *height) {
    if (split_get_pane(root_split)) {
        if (is_zoomed || is_fullscr) {
            gaps_font(wgs, term_rect, width, height);
        } else {
            if (wgs->resize_either.maximized) {
                if (wgs->resize_either.alt_pressed && wgs->resize_either.font_resized) {
                    gaps_font(wgs, term_rect, width, height);
                } else {
                    gaps_term_init(wgs, term_rect, width, height);
                }
            } else if (wgs->resize_either.alt_pressed) {
                gaps_font(wgs, term_rect, width, height);
            } else {
                gaps_term(wgs, term_rect, width, height);
            }
        }
    } else {
        gaps_term_init(wgs, term_rect, width, height);
    }
}

static void calculate_term_rect_gaps(WinGuiSession *wgs, const RECT *term_rect, int *ew, int *eh) {
    int width = 0;
    int height = 0;
    switch (conf_get_int(wgs->conf, CONF_resize_action)) {
        case RESIZE_TERM:
            gaps_term(wgs, term_rect, &width, &height);
            break;
        case RESIZE_FONT:
            gaps_font(wgs, term_rect, &width, &height);
            break;
        case RESIZE_EITHER:
            gaps_either(wgs, term_rect, &width, &height);
            break;
        default:
            ew = 0;
            eh = 0;
            return;
    }
    int border = conf_get_int(wgs->conf, CONF_window_border);
    width += border * 2;
    height += border * 2;
    *ew = term_rect->right - width;
    *eh = term_rect->bottom - height;
}

static void init_term_dimensions(WinGuiSession *wgs) {
    term_size(wgs->term, conf_get_int(wgs->conf, CONF_height),
                         conf_get_int(wgs->conf, CONF_width),
                         conf_get_int(wgs->conf, CONF_savelines));
    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_EITHER) {
        wgs->resize_either.init_font_width = wgs->font_width;
        wgs->resize_either.init_font_height = wgs->font_height;
    }
}

void possible_term_dimensions(WinGuiSession *wgs, const RECT *term_rect, int *cols, int *rows) {
    switch (conf_get_int(wgs->conf, CONF_resize_action)) {
        case RESIZE_DISABLED:
        case RESIZE_FONT:
            *cols = wgs->term->cols;
            *rows = wgs->term->rows;
            break;
        case RESIZE_TERM:
            calculate_term_dimensions(wgs, term_rect, cols, rows);
            break;
        case RESIZE_EITHER:
            if (split_get_pane(root_split) && wgs->resize_either.alt_pressed) {
                *cols = wgs->term->cols;
                *rows = wgs->term->rows;
            } else {
                calculate_term_dimensions(wgs, term_rect, cols, rows);
            }
            break;
    }
}

static void resize_font(WinGuiSession *wgs, const RECT *term_rect) {
    int w, h;
    calculate_font_size(wgs, term_rect, &w, &h);
    if (w != wgs->font_width || h != wgs->font_height) {
        deinit_fonts(wgs);
        init_fonts(wgs, w, h);
    } else {
        wgs->font_dpi = 0;
    }
}

static void resize_term_keep_font(WinGuiSession *wgs, const RECT *term_rect) {
    int cols, rows;
    calculate_term_dimensions(wgs, term_rect, &cols, &rows);
    if (resizing) {
        wgs->need_backend_resize = true;
        wgs->backend_rows = rows;
        wgs->backend_cols = cols;
    } else {
        if (wgs->term->cols != cols || wgs->term->rows != rows) {
            term_size(wgs->term, rows, cols, conf_get_int(wgs->conf, CONF_savelines));
            refresh_find_match_mask(wgs);
        }
    }
    if (split_get_pane(root_split) && !is_zoomed && !is_fullscr) {
        conf_set_int(wgs->conf, CONF_width, cols);
        conf_set_int(wgs->conf, CONF_height, rows);
    }
}

static void resize_term_reset_font(WinGuiSession *wgs, const RECT *term_rect) {
    deinit_fonts(wgs);
    init_fonts(wgs, 0, 0);
    resize_term_keep_font(wgs, term_rect);
}

static void resize_term(WinGuiSession *wgs, const RECT *term_rect) {
    if (wgs->font_dpi != dpi_info.y) {
        deinit_fonts(wgs);
        init_fonts(wgs, 0, 0);
    }
    resize_term_keep_font(wgs, term_rect);
}

static void resize_either_set_init_font(WinGuiSession *wgs) {
    deinit_fonts(wgs);
    init_fonts(wgs, 0, 0);
    wgs->resize_either.font_dpi = dpi_info.y;
    wgs->resize_either.init_font_width = wgs->font_width;
    wgs->resize_either.init_font_height = wgs->font_height;
}

static void resize_either(WinGuiSession *wgs, const RECT *term_rect) {
    if (split_get_pane(root_split)) {
        if (is_zoomed || is_fullscr) {
            if (!wgs->resize_either.maximized) {
                wgs->resize_either.maximized = true;
                wgs->resize_either.normal_font_width = wgs->font_width;
                wgs->resize_either.normal_font_height = wgs->font_height;
            }
            if (wgs->resize_either.font_dpi != dpi_info.y) {
                resize_either_set_init_font(wgs);
            }
            resize_font(wgs, term_rect);
        } else {
            if (wgs->resize_either.activate) {
                if (wgs->resize_either.font_dpi != dpi_info.y) {
                    resize_term_reset_font(wgs, term_rect);
                    wgs->resize_either.maximized = false;
                    wgs->resize_either.font_resized = false;
                    wgs->resize_either.font_dpi = dpi_info.y;
                    wgs->resize_either.init_font_width = wgs->font_width;
                    wgs->resize_either.init_font_height = wgs->font_height;
                } else if (wgs->resize_either.maximized) {
                    wgs->resize_either.maximized = false;
                    if (wgs->resize_either.font_resized) {
                        deinit_fonts(wgs);
                        init_fonts(wgs, wgs->resize_either.normal_font_width, wgs->resize_either.normal_font_height);
                        resize_term_keep_font(wgs, term_rect);
                    } else {
                        resize_term_reset_font(wgs, term_rect);
                    }
                } else {
                    resize_term_keep_font(wgs, term_rect);
                }
            } else if (wgs->resize_either.maximized) {
                wgs->resize_either.maximized = false;
                if (wgs->resize_either.alt_pressed && wgs->resize_either.font_resized) {
                    resize_font(wgs, term_rect);
                } else {
                    resize_term_reset_font(wgs, term_rect);
                    wgs->resize_either.font_resized = false;
                }
            } else if (wgs->resize_either.font_dpi != dpi_info.y) {
                resize_either_set_init_font(wgs);
                if (wgs->resize_either.font_resized) {
                    resize_font(wgs, term_rect);
                }
                resize_term_keep_font(wgs, term_rect);
            } else if (wgs->resize_either.alt_pressed) {
                wgs->resize_either.font_resized = true;
                resize_font(wgs, term_rect);
            } else {
                resize_term_keep_font(wgs, term_rect);
            }
        }
        assert((!wgs->resize_either.font_resized && !wgs->resize_either.maximized && wgs->font_dpi > 0) ||
               ((wgs->resize_either.font_resized || wgs->resize_either.maximized) && wgs->font_dpi == 0));
    } else {
        wgs->resize_either.font_resized = false;
        wgs->resize_either.maximized = false;
        resize_term(wgs, term_rect);
        if (wgs->resize_either.font_dpi != dpi_info.y) {
            wgs->resize_either.font_dpi = dpi_info.y;
            wgs->resize_either.init_font_width = wgs->font_width;
            wgs->resize_either.init_font_height = wgs->font_height;
        }
    }
    wgs->resize_either.alt_pressed = false;
    wgs->resize_either.activate = false;
}

static void resize_term_dimensions(WinGuiSession *wgs, const RECT *term_rect) {
    switch (conf_get_int(wgs->conf, CONF_resize_action)) {
        case RESIZE_DISABLED:
            if (wgs->font_dpi != dpi_info.y) {
                deinit_fonts(wgs);
                init_fonts(wgs, 0, 0);
            }
            break;
        case RESIZE_FONT:
            resize_font(wgs, term_rect);
            break;
        case RESIZE_TERM:
            resize_term(wgs, term_rect);
            break;
        case RESIZE_EITHER:
            resize_either(wgs, term_rect);
          break;
    }
}

static void change_focused_pane(Pane *pane) {
    if (focused_pane != pane) {
        focused_pane = pane;
        pane_set_focused(pane, true);
    }
}

static void adjust_frame_rect_to_client(RECT *rect) {
    RECT window_rect;
    GetWindowRect(frame_hwnd, &window_rect);
    RECT client_rect;
    GetClientRect(frame_hwnd, &client_rect);
    int ew = (window_rect.right - window_rect.left) - client_rect.right;
    int eh = (window_rect.bottom - window_rect.top) - client_rect.bottom;
    rect->right += ew;
    rect->bottom += eh;
}

static void get_frame_client_rect(const RECT *rect, RECT *client_rect) {
    client_rect->left = 0;
    client_rect->top = 0;
    client_rect->right = 0;
    client_rect->bottom = 0;
    adjust_frame_rect_to_client(client_rect);
    client_rect->right = (rect->right - rect->left) - client_rect->right;
    client_rect->bottom = (rect->bottom - rect->top) - client_rect->bottom;
}

static void snap_frame_rect_to_possible_term(Pane *pane, RECT *rect, RECT *client_rect, int wmsz) {
    WinGuiSession *wgs = pane_get_active_session(pane);
    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_DISABLED) {
        return;
    }
    int ew = 0, eh = 0;
    RECT term_rect;
    pane_get_possible_term_rect(pane, client_rect, &term_rect);
    calculate_term_rect_gaps(wgs, &term_rect, &ew, &eh);
    if (wmsz == WMSZ_LEFT || wmsz == WMSZ_BOTTOMLEFT || wmsz == WMSZ_TOPLEFT) {
        rect->left += ew;
    } else {
        rect->right -= ew;
    }
    if (wmsz == WMSZ_TOP || wmsz == WMSZ_TOPRIGHT || wmsz == WMSZ_TOPLEFT) {
        rect->top += eh;
    } else {
        rect->bottom -= eh;
    }
    client_rect->right -= ew;
    client_rect->bottom -= eh;
}

static void snap_frame_to_term(Pane *pane) {
    WinGuiSession *wgs = pane_get_active_session(pane);
    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_DISABLED) {
        return;
    }
    RECT rect;
    int border = conf_get_int(wgs->conf, CONF_window_border);
    rect.right = conf_get_int(wgs->conf, CONF_width) * wgs->font_width + border*2;
    rect.bottom = conf_get_int(wgs->conf, CONF_height) * wgs->font_height + border*2;
    rect.left = 0;
    rect.top = 0;
    pane_get_requied_rect(pane, &rect, &rect);
    adjust_frame_rect_to_client(&rect);

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    GetWindowRect(frame_hwnd, &rect);
    int x = rect.left;
    int y = rect.top;
    if (get_workingarea_rect(&rect)) {
        if (x + width > rect.right) {
            x = rect.right - width;
        }
        if (x < rect.left) {
            x = rect.left;
        }
        if (y + height > rect.bottom) {
            y = rect.bottom - height;
        }
        if (y < rect.top) {
            y = rect.top;
        }
    }
    SetWindowPos(frame_hwnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

static bool is_scrollbar_visible(Pane *pane, WinGuiSession *wgs) {
    bool fullscreen = is_fullscr && pane == split_get_pane(root_split);
    return conf_get_bool(wgs->conf, fullscreen ? CONF_scrollbar_in_fullscreen : CONF_scrollbar);
}

static void set_term_hwnd_style(Pane *pane, WinGuiSession *wgs) {
    bool scrollbar = is_scrollbar_visible(pane, wgs);
    pane_term_hwnd_style(pane, scrollbar, conf_get_bool(wgs->conf, CONF_sunken_edge));
}

static void flip_full_screen() {
    skip_update_split_layout = true;
    if (is_fullscr) {
        is_fullscr = false;
        DWORD style = GetWindowLongPtr(frame_hwnd, GWL_STYLE);
        style |= WS_CAPTION | WS_BORDER | WS_THICKFRAME;
        SetWindowLong(frame_hwnd, GWL_STYLE, style);
        SetWindowPos(frame_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        Pane *pane = split_get_pane(root_split);
        if (pane) {
            set_term_hwnd_style(pane, pane_get_active_session(pane));
            if (!is_zoomed) {
                RECT *rect = &fullscr_placement.rcNormalPosition;
                RECT client_rect;
                get_frame_client_rect(rect, &client_rect);
                snap_frame_rect_to_possible_term(pane, rect, &client_rect, WMSZ_BOTTOMRIGHT);
            }
        }
        SetWindowPlacement(frame_hwnd, &fullscr_placement);
      } else {
        is_fullscr = true;
        GetWindowPlacement(frame_hwnd, &fullscr_placement);
        RECT ss;
        get_fullscreen_rect(&ss);
        DWORD style = GetWindowLongPtr(frame_hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
        SetWindowLong(frame_hwnd, GWL_STYLE, style);
        SetWindowPos(frame_hwnd, HWND_TOP, ss.left, ss.top, ss.right - ss.left, ss.bottom - ss.top, SWP_FRAMECHANGED);
        Pane *pane = split_get_pane(root_split);
        if (pane) {
            set_term_hwnd_style(pane, pane_get_active_session(pane));
        }
    }
    skip_update_split_layout = false;
    RECT client_rect;
    GetClientRect(frame_hwnd, &client_rect);
    SendMessage(frame_hwnd, WM_SIZE, fullscr_placement.showCmd == SW_MAXIMIZE ? SIZE_MAXIMIZED : SIZE_RESTORED, MAKELPARAM(client_rect.right, client_rect.bottom));

    check_menu_item(IDM_FULLSCREEN, is_fullscr ? MF_CHECKED : MF_UNCHECKED);
}

static void check_root_pane_merged() {
    Pane *root_pane = split_get_pane(root_split);
    if (root_pane) {
        if (is_fullscr) {
            set_term_hwnd_style(root_pane, pane_get_active_session(root_pane));
        } else if (!is_zoomed) {
            RECT rect;
            RECT client_rect;
            GetWindowRect(frame_hwnd, &rect);
            GetClientRect(frame_hwnd, &client_rect);
            snap_frame_rect_to_possible_term(root_pane, &rect, &client_rect, WMSZ_BOTTOMRIGHT);
            SetWindowPos(frame_hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        }
    }
}

static void root_pane_splitted(Split *old_root_split) {
    Pane *pane = split_get_pane(old_root_split);
    if (is_fullscr) {
        set_term_hwnd_style(pane, pane_get_active_session(pane));
    }
}

static bool is_topmost() {
    return GetWindowLongPtr(frame_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST;
}

static void flip_always_on_top() {
    HWND hwndInsertAfter;
    if (is_topmost()) {
        hwndInsertAfter = HWND_NOTOPMOST;
    } else {
        hwndInsertAfter = HWND_TOPMOST;
    }
    SetWindowPos(frame_hwnd, hwndInsertAfter, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    check_menu_item(IDM_ALWAYSONTOP, is_topmost() ? MF_CHECKED : MF_UNCHECKED);
}

static void update_split_layout(const RECT *rect) {
    if (skip_update_split_layout) {
      return;
    }
    if (rect->right == split_layout_rect.right &&
        rect->bottom == split_layout_rect.bottom &&
        rect->left == split_layout_rect.left &&
        rect->top == split_layout_rect.top) {
        return;
    }
    split_layout_rect = *rect;
    split_plan_layout(root_split, rect);
    split_apply_layout(root_split);
}

static void resize_backends(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        WinGuiSession *wgs = pane_get_active_session(pane);
        if (wgs->need_backend_resize) {
            term_size(wgs->term, wgs->backend_rows, wgs->backend_cols,
                                 conf_get_int(wgs->conf, CONF_savelines));
            refresh_find_match_mask(wgs);
            InvalidateRect(pane_get_term_hwnd(pane), NULL, TRUE);
            wgs->need_backend_resize = false;
        }
        return;
    }
    resize_backends(split_get_first(split));
    resize_backends(split_get_second(split));
}

void handle_splitter_notify(NMHDR *hdr) {
    switch (hdr->code) {
      case SPLITTER_NOTIFY_MERGE: {
        Split *split = split_get_from_hwnd(hdr->hwndFrom);
        Pane *first = split_get_pane(split_get_first(split));
        Pane *second = split_get_pane(split_get_second(split));
        int first_new_index = pane_get_session_count(first) + pane_get_active_session_index(second);
        int second_count = pane_get_session_count(second);
        Pane *deleted_pane = split_merge(split);
        if (deleted_pane == second) {
            for (int i = 0; i < second_count; i++) {
                pane_import_session(first, second, i);
                session_set_pane(pane_get_session(second, i), first);
            }
            pane_clear_active_session(second);
        }
        if (focused_pane == deleted_pane) {
            if (deleted_pane == second) {
                if (second_count > 0) {
                    activate_session(first, first_new_index);
                }
                change_focused_pane(first);
            } else {
                change_focused_pane(second);
            }
        }
        pane_destroy(deleted_pane);
        check_root_pane_merged();
        break;
      }
      case SPLITTER_NOTIFY_ENTER_DRAG: {
        resizing = true;
        break;
      }
      case SPLITTER_NOTIFY_EXIT_DRAG: {
        resizing = false;
        resize_backends(split_get_from_hwnd(hdr->hwndFrom));
        break;
      }
    }
}

static LRESULT CALLBACK frame_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_CLOSE:
        show_mouseptr(pane_get_active_session(focused_pane), true);
        char *title = dupprintf("%s Exit Confirmation", appname);
        int ret = MessageBox(hwnd, "Are you sure you want to close All sessions?", title,
                        MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1);
        sfree(title);
        if (ret != IDOK) {
            return 0;
        }
        SetFocus(NULL);
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        destroy_sessions(root_split);
        split_destroy(root_split);
        find_match_mask_free(&find_match_mask);
        DeleteObject(tab_bar_font);
        PostQuitMessage(0);
        return 0;
      case WM_SIZE: {
        if (wparam == SIZE_MINIMIZED) {
            is_minimized = true;
            is_zoomed = false;
            handle_minimised_change();
            return 0;
        } else if (wparam == SIZE_MAXIMIZED) {
            if (is_minimized) {
                is_minimized = false;
                handle_minimised_change();
            }
            is_zoomed = true;
        } else if (wparam == SIZE_RESTORED) {
            if (is_minimized) {
                is_minimized = false;
                handle_minimised_change();
            }
            if (is_zoomed) {
                is_zoomed = false;
                Pane *pane = split_get_pane(root_split);
                if (pane) {
                    RECT rect;
                    RECT client_rect = {0, 0, LOWORD(lparam), HIWORD(lparam)};
                    WinGuiSession *wgs = pane_get_active_session(pane);
                    GetWindowRect(frame_hwnd, &rect);
                    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_EITHER) {
                        wgs->resize_either.alt_pressed = is_alt_pressed();
                    }
                    snap_frame_rect_to_possible_term(pane, &rect, &client_rect, WMSZ_BOTTOMRIGHT);
                    assert(!skip_update_split_layout);
                    skip_update_split_layout = true;
                    SetWindowPos(frame_hwnd, NULL, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
                    skip_update_split_layout = false;
                    update_split_layout(&client_rect);
                    return 0;
                }
            }
        }
        RECT rect = {0, 0, LOWORD(lparam), HIWORD(lparam)};
        if (resizing) {
            if (rect.right != split_layout_rect.right ||
                rect.bottom != split_layout_rect.bottom ||
                rect.left != split_layout_rect.left ||
                rect.top != split_layout_rect.top) {
                split_plan_layout(root_split, &rect);
                split_layout_rect = rect;
            }
            split_apply_layout(root_split);
            split_move_sizetips(root_split);
        } else {
            update_split_layout(&rect);
        }
        return 0;
      }
      case WM_DRAWITEM:
        return SendMessage(((DRAWITEMSTRUCT *)lparam)->hwndItem, WM_DRAWITEM, wparam, lparam);
      case WM_NOTIFY: {
        switch (((NMHDR *)lparam)->idFrom) {
        case SPLITTER_NOTIFY_ID:
            handle_splitter_notify((NMHDR *)lparam);
            return 0;
        case TAB_BAR_NOTIFY_ID:
            handle_tab_bar_notify((TabBarNotify *)lparam);
            return 0;
        case FINDDLG_NOTIFY_ID:
            handle_finddlg_notify((NMHDR *)lparam);
            return 0;
        }
        break;
      }
      case WM_MOUSEMOVE:
        handle_wm_mouse_move(wparam, lparam);
        return 0;
      case WM_NCMOUSEMOVE:
        handle_wm_ncmouse_move(wparam, lparam);
        return 0;
      case WM_DPICHANGED: {
        RECT *rect = (RECT *)lparam;
        dpi_info.x = LOWORD(wparam);
        dpi_info.y = HIWORD(wparam);
        DeleteObject(tab_bar_font);
        tab_bar_font = get_dpi_aware_tab_bar_font();
        tab_bar_common_dpi_changed(tab_bar_font);
        size_tip_common_dpi_changed(tab_bar_font);
        split_common_dpi_changed();
        split_dpi_changed(root_split);
        SetWindowPos(hwnd, NULL, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      case WM_MOVE:
        split_pin_layout(root_split);
        return 0;
      case WM_ACTIVATE:
        if (wparam == WA_CLICKACTIVE) {
            DWORD message_pos = GetMessagePos();
            POINT point = { GET_X_LPARAM(message_pos), GET_Y_LPARAM(message_pos) };
            ScreenToClient(frame_hwnd, &point);
            Split *split = split_get_from_point(root_split, &point);
            if (split) {
                focused_pane = split_get_pane(split);
            }
        }
        return 0;
      case WM_SETFOCUS:
        pane_set_focused(focused_pane, true);
        return 0;
      case WM_KILLFOCUS:
        return 0;
      case WM_ENTERSIZEMOVE:
        resizing = true;
        return 0;
      case WM_SIZING: {
        RECT *rect = (RECT *)lparam;
        RECT client_rect;
        get_frame_client_rect(rect, &client_rect);
        Pane *pane = split_get_pane(root_split);
        if (pane) {
            WinGuiSession *wgs = pane_get_active_session(pane);
            if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_EITHER) {
              wgs->resize_either.alt_pressed = is_alt_pressed();
            }
            snap_frame_rect_to_possible_term(pane, rect, &client_rect, wparam);
        }
        split_layout_rect = client_rect;
        split_plan_layout(root_split, &client_rect);
        split_update_sizetips(root_split);
        return TRUE;
      }
      case WM_EXITSIZEMOVE:
        resizing = false;
        split_hide_sizetips(root_split);
        resize_backends(root_split);
        return 0;
      case WM_SYSCOMMAND:
        switch (wparam & ~0xF) {
          case SC_MOUSEMENU:
            show_mouseptr(pane_get_active_session(focused_pane), true);
            break;
          case SC_KEYMENU:
            show_mouseptr(pane_get_active_session(focused_pane), true);
            if (lparam == 0) {
                PostMessage(hwnd, WM_CHAR, ' ', 0);
            }
            break;
        }
        break;
      case WM_NETEVENT:
      case WM_DONE_WITH_SOCKET:
        winselgui_response(message, wparam, lparam);
        return 0;
      case WM_PALETTECHANGED:
      case WM_QUERYNEWPALETTE:
      case WM_INPUTLANGCHANGE:
      case WM_SYSCOLORCHANGE:
        message_term_hwnds(root_split, message, wparam, lparam);
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void register_frame_class() {
    WNDCLASSW wndclass;

    wndclass.style = 0;
    wndclass.lpfnWndProc = frame_proc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hinst;
    wndclass.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_MAINICON));
    wndclass.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW));
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);

    RegisterClassW(&wndclass);
}

static void create_frame_window() {
    wchar_t *uappname = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
    frame_hwnd = CreateWindowExW(0, uappname, uappname,
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT,
      NULL, NULL, hinst, NULL);
    sfree(uappname);
}

static bool create_conf(const char *saved_session, Conf **conf, const char **session_name) {
    *conf = conf_new();
    conf_set_int(*conf, CONF_logtype, LGTYP_NONE);
    do_defaults(saved_session, *conf);
    if (conf_launchable(*conf)) {
        *session_name = dupstr(saved_session);
        return true;
    }
    int config_result = do_config_pp(frame_hwnd, *conf, session_name);
    if (!config_result) {
        conf_free(*conf);
        return false;
    }
    return true;
}

static void term_palette_init_fix_callback(void *ctx)
{
    Terminal *term = (Terminal *)ctx;
    win_palette_set(term->win, 0, 0, term->palette);
}

static void term_palette_init_fix(Terminal *term)
{
    if (!term->win_palette_pending)
    {
        queue_toplevel_callback(term_palette_init_fix_callback, term);
    }
}

static WinGuiSession *create_session(Conf *conf, const char *session_name) {
    WinGuiSession *wgs = (WinGuiSession *)smalloc(sizeof(WinGuiSession));

    memset(wgs, 0, sizeof(*wgs));
    wgs->conf = conf;

    wgs->caret_x = -1;
    wgs->caret_y = -1;
    wgs->specials = NULL;
    wgs->specials_menu = NULL;
    wgs->n_specials = 0;
    wgs->tried_pal = false;
    wgs->colorref_modifier = 0;
    wgs->send_raw_mouse = false;
    wgs->wheel_accumulator = 0;
    wgs->pointer_indicates_raw_mouse = false;
    wgs->busy_status = BUSY_NOT;
    wgs->compose_state = 0;
    wgs->termwin.vt = &windows_termwin_vt;
    wgs->wintw_hdc = NULL;
    wgs->trust_icon = INVALID_HANDLE_VALUE,
    wgs->eventlogstuff.ninitial = 0;
    wgs->eventlogstuff.ncircular = 0;
    wgs->eventlogstuff.circular_first = 0;
    wgs->seat.vt = &win_seat_vt;
    wgs->logpolicy.vt = &win_gui_logpolicy_vt;
    wgs->need_backend_resize = false;
    wgs->ignore_clip = false;
    wgs->pending_surrogate = 0;
    wgs->alt_numberpad_accumulator = 0;
    wgs->compose_char = 0;
    wgs->compose_keycode = 0;

    wgs->window_name = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);
    wgs->icon_name = dup_mb_to_wc(DEFAULT_CODEPAGE, appname);

    memset(&wgs->ucsdata, 0, sizeof(wgs->ucsdata));
    conf_cache_data(wgs);
    init_fonts(wgs, 0, 0);
    init_palette(wgs);

    wgs->term_palette_init = true;
    Terminal *term = term_init(conf, &wgs->ucsdata, &wgs->termwin);
    term->ldisc = NULL; // missing from term_init
    term->basic_erase_char.attr |= ATTR_ERASE;
    term->erase_char.attr |= ATTR_ERASE;
    term_palette_init_fix(term);
    wgs->term = term;
    setup_clipboards(term, conf);
    wgs->logctx = log_init(&wgs->logpolicy, conf);
    term_provide_logctx(term, wgs->logctx);
    init_term_dimensions(wgs);

    char *bits;
    int size = (wgs->font_width + 15) / 16 * 2 * wgs->font_height;
    bits = snewn(size, char);
    memset(bits, 0, size);
    wgs->caretbm = CreateBitmap(wgs->font_width, wgs->font_height, 1, 1, bits);
    sfree(bits);

    wgs->session_id = session_counter++;
    wgs->session_name = session_name;
    wgs->remote_closed = true;
    wgs->delete_session = false;

    wgs->cursor_visible = true;
    wgs->cursor_forced_visible = false;

    return wgs;
}

static void destroy_session(WinGuiSession *wgs) {
    DeleteObject(wgs->caretbm);

    sfree(wgs->find.pattern);

    log_free(wgs->logctx);
    term_free(wgs->term);

    sfree(wgs->logpal);
    if (wgs->pal)
        DeleteObject(wgs->pal);
    deinit_fonts(wgs);

    if (conf_get_int(wgs->conf, CONF_protocol) == PROT_SSH) {
        random_save_seed();
    }
    conf_free(wgs->conf);

    sfree((char *)wgs->session_name);
    sfree(wgs);
}

static void destroy_sessions(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        for (int i=0; i<pane_get_session_count(pane); i++) {
            WinGuiSession *wgs = (WinGuiSession *)pane_get_session(pane, i);
            if (wgs->backend) {
                stop_backend(wgs);
            }
            if (wgs->remote_closed) {
                delete_callbacks_for_context(wgs);
            }
            destroy_session(wgs);
        }
    } else {
        destroy_sessions(split_get_first(split));
        destroy_sessions(split_get_second(split));
    }
}

static void set_title_from_session(WinGuiSession *wgs) {
    if (conf_get_bool(wgs->conf, CONF_win_name_always) || !IsIconic(frame_hwnd))
        SetWindowTextW(frame_hwnd, wgs->window_name);
}

static void set_icon_title_from_session(WinGuiSession *wgs) {
    if (!conf_get_bool(wgs->conf, CONF_win_name_always) && IsIconic(frame_hwnd))
        SetWindowTextW(frame_hwnd, wgs->icon_name);
}

static void realize_palette(WinGuiSession *wgs) {
    Conf *conf = wgs->conf;
    bool got_new_palette = false;

    if (!wgs->tried_pal && conf_get_bool(conf, CONF_try_palette)) {
        HDC hdc = GetDC(wgs->term_hwnd);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            wgs->pal = CreatePalette(wgs->logpal);
            if (wgs->pal) {
                SelectPalette(hdc, wgs->pal, false);
                RealizePalette(hdc);
                SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);

                /* Convert all RGB() values in colours[] into PALETTERGB(),
                 * and ensure we stick to that later */
                wgs->colorref_modifier = PALETTERGB(0, 0, 0) ^ RGB(0, 0, 0);
                for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
                    wgs->colours[i] ^= wgs->colorref_modifier;

                /* Inhibit the SetPaletteEntries call below */
                got_new_palette = true;
            }
        }
        ReleaseDC(wgs->term_hwnd, hdc);
        wgs->tried_pal = true;
    }

    if (wgs->pal && !got_new_palette) {
        /* We already had a palette, so replace the changed colours in the
         * existing one. */
        SetPaletteEntries(wgs->pal, 0, wgs->logpal->palNumEntries, wgs->logpal->palPalEntry);

        HDC hdc = make_hdc(wgs);
        UnrealizeObject(wgs->pal);
        RealizePalette(hdc);
        free_hdc(wgs, hdc);
    }
}

static void activate_session(Pane *pane, int index) {
    WinGuiSession *wgs = pane_get_active_session(pane);
    if (wgs && wgs->term->has_focus) {
        term_set_focus(wgs->term, false);
    }
    pane_clear_active_session(pane);
    set_term_hwnd_style(pane, pane_get_session(pane, index));
    pane_select_session(pane, index);

    wgs = pane_get_active_session(pane);
    if (focused_pane == pane && !wgs->term->has_focus && GetForegroundWindow() == frame_hwnd) {
        term_set_focus(wgs->term, true);
        DestroyCaret();
        wgs->caret_x = wgs->caret_y = -1;
        CreateCaret(wgs->term_hwnd, wgs->caretbm, wgs->font_width, wgs->font_height);
        ShowCaret(wgs->term_hwnd);
    }
    wgs->find.update_finddlg_pending = true;
    realize_palette(wgs);

    if (conf_get_int(wgs->conf, CONF_resize_action) == RESIZE_EITHER) {
        wgs->resize_either.activate = true;
    }
    RECT term_rect;
    pane_get_term_rect(pane, &term_rect);
    resize_term_dimensions(wgs, &term_rect);
    InvalidateRect(pane_get_term_hwnd(pane), NULL, TRUE);

    if (wgs->caret_x < 0 || wgs->caret_y < 0) {
        win_set_cursor_pos(&wgs->termwin, wgs->term->curs.x, wgs->term->curs.y);
    }
    set_title_from_session(wgs);
    set_icon_title_from_session(wgs);
    term_update_sbar(wgs->term);
    update_mouse_pointer(wgs);
    reseteventlog(&wgs->eventlogstuff);
    update_finddlg(wgs);
}

static char *create_session_title(int id, const char *session_name) {
    return dupprintf("%d. %s", id, session_name);
}

static WinGuiSession *add_stale_session(Pane *pane, Conf *conf, const char *session_name, int index) {
    if (!session_name) {
        session_name = dupstr(conf_get_str(conf, CONF_host));
    }
    char *title = create_session_title(session_counter, session_name);
    WinGuiSession *wgs = create_session(conf, session_name);
    pane_insert_session(pane, index, title, conf_get_int(conf, CONF_protocol), wgs);
    session_set_pane(wgs, pane);
    sfree(title);
    return wgs;
}

static void add_session(Pane *pane, Conf *conf, const char *session_name, int index) {
    WinGuiSession *wgs = add_stale_session(pane, conf, session_name, index);
    activate_session(pane, index);
    start_backend(wgs);
}

static int delete_session(Pane *pane, int index) {
    WinGuiSession *wgs = pane_get_session(pane, index);
    int new_index = pane_delete_session(pane, index);
    destroy_session(wgs);
    if (new_index >= 0) {
      activate_session(pane, new_index);
    }
    if (pane_get_session_count(pane) == 0) {
        Split *split = split_find_parent(root_split, pane);
        if (split) {
            Pane *deleted_pane = split_merge(split);
            if (focused_pane == deleted_pane) {
                change_focused_pane(split_find_pane(split));
            }
            pane_destroy(deleted_pane);
            check_root_pane_merged();
        } else {
            SetFocus(NULL);
            DestroyWindow(frame_hwnd);
        }
    }
    return new_index;
}

static void close_session(Pane *pane, int index) {
    WinGuiSession *wgs = pane_get_session(pane, index);
    if (!wgs->remote_closed && conf_get_bool(wgs->conf, CONF_warn_on_close)) {
        if (pane_get_active_session_index(pane) != index) {
            activate_session(pane, index);
        }
        show_mouseptr(pane_get_active_session(focused_pane), true);
        char *title, *msg, *additional = NULL;
        title = dupprintf("%s Session Close Confirmation", appname);
        if (wgs->backend && wgs->backend->vt->close_warn_text) {
            additional = wgs->backend->vt->close_warn_text(wgs->backend);
        }
        msg = dupprintf("Are you sure you want to close this session?%s%s",
                        additional ? "\n" : "",
                        additional ? additional : "");
        int ret = MessageBox(frame_hwnd, msg, title, MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1);
        sfree(title);
        sfree(msg);
        sfree(additional);
        if (ret != IDOK) {
            return;
        }
    }
    if (wgs->backend) {
        stop_backend(wgs);
    }
    if (wgs->remote_closed) {
        delete_callbacks_for_context(wgs);
    }
    delete_session(pane, index);
}

static void show_finddlg(WinGuiSession *wgs) {
    const int default_pattern_buffer_len = 16;
    if (!wgs->find.pattern) {
        wgs->find.pattern = snewn(default_pattern_buffer_len, wchar_t);
        wgs->find.pattern_buffer_len = default_pattern_buffer_len;
        wgs->find.pattern_len = 0;
        wgs->find.pattern[0] = 0;
    }
    pane_show_finddlg(focused_pane, wgs->find.pattern, true, wgs->find.ignore_case, wgs->find.whole_word);
}

static void update_finddlg(WinGuiSession *wgs) {
    if (wgs->find.pattern) {
        pane_show_finddlg(focused_pane, wgs->find.pattern, false, wgs->find.ignore_case, wgs->find.whole_word);
        if (wgs->find.pattern_len > 1) {
            find_match_mask_alloc(&find_match_mask, wgs->term->rows, wgs->term->cols);
            find_display(wgs->term, wgs->find.pattern, wgs->find.pattern_len, wgs->find.ignore_case, wgs->find.whole_word, &find_match_mask);
        } else {
            find_match_mask_free(&find_match_mask);
        }
    } else {
        find_match_mask_free(&find_match_mask);
        pane_hide_finddlg(focused_pane);
    }
    wgs->find.update_finddlg_pending = false;
}

static void update_find_match_mask(WinGuiSession *wgs)
{
    bool dirty = find_match_mask.dirty;
    find_match_mask_alloc(&find_match_mask, wgs->term->rows, wgs->term->cols);
    find_display(wgs->term, wgs->find.pattern, wgs->find.pattern_len, wgs->find.ignore_case, wgs->find.whole_word, &find_match_mask);
    if (find_match_mask.dirty || dirty) {
        term_invalidate(wgs->term);
    }
}

static void drop_find_match_mask(WinGuiSession *wgs) {
    bool dirty = find_match_mask.dirty;
    find_match_mask_free(&find_match_mask);
    if (dirty) {
        term_invalidate(wgs->term);
    }
}

static void update_find_pattern(WinGuiSession *wgs, int l) {
    int buffer_len = l+1;
    if (wgs->find.pattern_buffer_len < buffer_len) {
        sfree(wgs->find.pattern);
        wgs->find.pattern = snewn(buffer_len, wchar_t);
        wgs->find.pattern_buffer_len = buffer_len;
    }
    wgs->find.pattern_len = pane_get_finddlg_text(wgs->pane, wgs->find.pattern, wgs->find.pattern_buffer_len);
    assert(wgs->find.pattern_len == l);
}

static void scroll_to_row(WinGuiSession *wgs, int row) {
    term_scroll(wgs->term, 0, row);
    find_match_mask_clear(&find_match_mask);
    find_display(wgs->term, wgs->find.pattern, wgs->find.pattern_len, wgs->find.ignore_case, wgs->find.whole_word, &find_match_mask);
    term_update(wgs->term);
}

static void handle_finddlg_notify(NMHDR *hdr) {
    Pane *pane = pane_get_from_finddlg_hwnd(hdr->hwndFrom);
    WinGuiSession *wgs_active = pane_get_active_session(pane);
    switch (hdr->code) {
      case FINDDLG_EDIT_CHANGED: {
        int l = pane_get_finddlg_text(pane, NULL, 0);
        update_find_pattern(wgs_active, l);
        if (l > 1) {
            update_find_match_mask(wgs_active);
        } else {
            drop_find_match_mask(wgs_active);
        }
        break;
      }
      case FINDDLG_IGNORE_CASE: {
        wgs_active->find.ignore_case = pane_get_finddlg_ignore_case(pane);
        if (find_match_mask.cells) {
            update_find_match_mask(wgs_active);
        }
        break;
      }
      case FINDDLG_WHOLE_WORD: {
        wgs_active->find.whole_word = pane_get_finddlg_whole_word(pane);
        if (find_match_mask.cells) {
            update_find_match_mask(wgs_active);
        }
        break;
      }
      case FINDDLG_EDIT_ENTER: {
        if (wgs_active->find.pattern_len < 2) {
            if (wgs_active->find.pattern_len > 0) {
                update_find_match_mask(wgs_active);
            }
            break;
        } // if pattern_len >= 2, no break, fall through to FINDDLG_UP
      }
      case FINDDLG_UP: {
        if (find_match_mask.cells) {
            int row;
            assert(wgs_active->find.pattern_len > 0);
            if (find_above_display(wgs_active->term, wgs_active->find.pattern, wgs_active->find.pattern_len, wgs_active->find.ignore_case, wgs_active->find.whole_word, &row)) {
                row -= wgs_active->term->rows/2;
                scroll_to_row(wgs_active, row);
            }
        }
        break;
      }
      case FINDDLG_DOWN: {
        if (find_match_mask.cells) {
            int row;
            assert(wgs_active->find.pattern_len > 0);
            if (find_below_display(wgs_active->term, wgs_active->find.pattern, wgs_active->find.pattern_len, wgs_active->find.ignore_case, wgs_active->find.whole_word, &row)) {
                row -= wgs_active->term->rows/2;
                scroll_to_row(wgs_active, row);
            }
        }
        break;
      }
      case FINDDLG_CLOSE: {
        sfree(wgs_active->find.pattern);
        wgs_active->find.pattern = NULL;
        wgs_active->find.pattern_buffer_len = 0;
        wgs_active->find.pattern_len = 0;
        wgs_active->find.ignore_case = false;
        wgs_active->find.whole_word = false;
        drop_find_match_mask(wgs_active);
        break;
      }
    }
}

static void handle_tab_bar_notify(TabBarNotify *notify) {
    Pane *pane = pane_get_from_tab_bar_hwnd(notify->hdr.hwndFrom);
    switch (notify->hdr.code) {
      case TCN_SELCHANGE:
        activate_session(pane, pane_get_active_session_index(pane));
        break;
      case TCN_TABEXCHANGE:
        pane_sessions_exchanged(pane, notify->tab_origin, pane_get_active_session_index(pane));
        break;
      case TCN_TABDELETE:
        close_session(pane, notify->tab_origin);
        break;
      case NM_CLICK:
        change_focused_pane(pane_get_from_tab_bar_hwnd(notify->hdr.hwndFrom));
        break;
      case NM_RCLICK: {
        POINT cursorpos;
        Pane *pane = pane_get_from_tab_bar_hwnd(notify->hdr.hwndFrom);
        change_focused_pane(pane);
        GetCursorPos(&cursorpos);
        TrackPopupMenu(popup_menus[SYSMENU].menu,
                       TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       cursorpos.x, cursorpos.y,
                       0, pane_get_term_hwnd(pane), NULL);
        break;
      }
      case TCN_OUTSIDE_DRAG: {
        POINT point = notify->point;
        ClientToScreen(notify->hdr.hwndFrom, &point);
        ScreenToClient(frame_hwnd, &point);
        Split *split = split_get_from_point(root_split, &point);
        if (!split) {
            split_marker_hide();
            break;
        }
        bool self_split = split_get_pane(split) == pane;
        RECT rect;
        SplitType type = split_get_possible_split_rect(split, self_split, &point, &rect);
        if (self_split && (type == SPLIT_TYPE_PANE || pane_get_session_count(pane) == 1)) {
            split_marker_hide();
            break;
        }
        split_marker_show(&rect);
        break;
      }
      case TCN_OUTSIDE_CANCEL:
        split_marker_hide();
        break;
      case TCN_OUTSIDE_RELEASE:
      {
        split_marker_hide();
        POINT point = notify->point;
        ClientToScreen(notify->hdr.hwndFrom, &point);
        ScreenToClient(frame_hwnd, &point);
        Split *target_split = split_get_from_point(root_split, &point);
        if (!target_split) {
            break;
        }
        Pane *source_pane = pane;
        bool self_split = split_get_pane(target_split) == source_pane;
        SplitPart part;
        SplitType type = split_get_possible_split(target_split, self_split, &point, &part);
        if (self_split && (type == SPLIT_TYPE_PANE || pane_get_session_count(source_pane) == 1)) {
            break;
        }
        int source_index = pane_get_active_session_index(pane);
        bool source_active = pane_get_active_session_index(source_pane) == source_index;
        Pane *root_pane = split_get_pane(root_split);
        if (type != SPLIT_TYPE_PANE) {
            if (source_active) {
                pane_clear_active_session(source_pane);
            }
            split_split(target_split, type, part);
            target_split = (part == SPLIT_PART_FIRST ? split_get_first(target_split) : split_get_second(target_split));
        }
        Pane *target_pane = split_get_pane(target_split);
        int target_index = pane_import_session(target_pane, source_pane, source_index);
        session_set_pane(pane_get_session(target_pane, target_index), target_pane);
        int source_selected_index = pane_delete_session(source_pane, source_index);
        if (source_selected_index >= 0) {
            activate_session(source_pane, source_selected_index);
        }
        if (source_active || target_index == 0) {
            activate_session(target_pane, target_index);
        }
        if (focused_pane == source_pane) {
            change_focused_pane(target_pane);
        }
        if (pane_get_session_count(source_pane) == 0) {
            Pane *deleted_pane = split_merge(split_find_parent(root_split, source_pane));
            assert(deleted_pane == source_pane);
            pane_destroy(deleted_pane);
            check_root_pane_merged();
        } else {
            if (root_pane && self_split) {
                root_pane_splitted(part == SPLIT_PART_FIRST ? split_get_second(root_split) : split_get_first(root_split));
            }
        }
        break;
      }
    }
}

static void handle_wm_initmenu(WinGuiSession *wgs_active, HMENU menu) {
    /*
     * Destroy the Restart Session menu item. (This will return
     * failure if it's already absent, as it will be the very first
     * time we call this function. We ignore that, because as long
     * as the menu item ends up not being there, we don't care
     * whether it was us who removed it or not!)
     */
    DeleteMenu(menu, IDM_RESTART, MF_BYCOMMAND);
    if (wgs_active->remote_closed) {
        /*
         * Show the Restart Session menu item. Do a precautionary
         * delete first to ensure we never end up with more than one.
         */
        InsertMenu(menu, IDM_DUPSESS, MF_BYCOMMAND | MF_ENABLED,
                   IDM_RESTART, "&Restart Session");
    }

    for (int position = GetMenuItemCount(menu)-1; position >= 0; position--) {
        if (GetMenuItemID(menu, position) == IDM_SPECIALSEP) {
            DeleteMenu(menu, position, MF_BYPOSITION);
            RemoveMenu(menu, position-1, MF_BYPOSITION);
            break;
        }
    }
    if (wgs_active->specials_menu) {
        InsertMenu(menu, IDM_SHOWLOG,
                   MF_BYCOMMAND | MF_POPUP | MF_ENABLED,
                   (UINT_PTR) wgs_active->specials_menu, "S&pecial Command");
        InsertMenu(menu, IDM_SHOWLOG,
                   MF_BYCOMMAND | MF_SEPARATOR, IDM_SPECIALSEP, 0);
    }

    DeleteMenu(menu, IDM_DUPSESS_SFTP, MF_BYCOMMAND);
    if (conf_get_int(wgs_active->conf, CONF_protocol) == PROT_SSH) {
        InsertMenu(menu, IDM_DUPSESS_NEW, MF_BYCOMMAND | MF_ENABLED,
                   IDM_DUPSESS_SFTP, "Duplicate as SFTP");
    }
}

static void message_term_hwnds(Split *split, UINT message, WPARAM wparam, LPARAM lparam) {
    Pane *pane = split_get_pane(split);
    if (pane) {
      SendMessage(pane_get_term_hwnd(pane), message, wparam, lparam);
      return;
    }
    message_term_hwnds(split_get_first(split), message, wparam, lparam);
    message_term_hwnds(split_get_second(split), message, wparam, lparam);
}

static void handle_wm_mouse_move(WPARAM wparam, LPARAM lparam) {
    /*
     * Windows seems to like to occasionally send MOUSEMOVE
     * events even if the mouse hasn't moved. Don't unhide
     * the mouse pointer in this case.
     */
    if (last_mousemove != WM_MOUSEMOVE ||
        wparam != last_wm_mousemove_wParam ||
        lparam != last_wm_mousemove_lParam) {
        show_mouseptr(pane_get_active_session(focused_pane), true);
        last_mousemove = WM_MOUSEMOVE;
        last_wm_mousemove_wParam = wparam;
        last_wm_mousemove_lParam = lparam;
    }
    /*
     * Add the mouse position and message time to the random
     * number noise.
     */
    noise_ultralight(NOISE_SOURCE_MOUSEPOS, lparam);
}

static void handle_wm_ncmouse_move(WPARAM wparam, LPARAM lparam) {
    if (last_mousemove != WM_NCMOUSEMOVE ||
        wparam != last_wm_ncmousemove_wParam ||
        lparam != last_wm_ncmousemove_lParam) {
        show_mouseptr(pane_get_active_session(focused_pane), true);
        last_mousemove = WM_NCMOUSEMOVE;
        last_wm_ncmousemove_wParam = wparam;
        last_wm_ncmousemove_lParam = lparam;
    }
    noise_ultralight(NOISE_SOURCE_MOUSEPOS, lparam);
}

static void terms_notify_minimised(Split *split, bool minimised) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        WinGuiSession *wgs = pane_get_active_session(pane);
        term_notify_minimised(wgs->term, minimised);
        return;
    }
    terms_notify_minimised(split_get_first(split), minimised);
    terms_notify_minimised(split_get_second(split), minimised);
}

static void handle_minimised_change() {
    WinGuiSession *wgs = pane_get_active_session(focused_pane);
    if (is_minimized) {
        SetWindowTextW(frame_hwnd,
                       conf_get_bool(wgs->conf, CONF_win_name_always) ?
                       wgs->window_name : wgs->icon_name);
    } else {
        SetWindowTextW(frame_hwnd, wgs->window_name);
    }
    terms_notify_minimised(root_split, is_minimized);
}
