
static int session_counter = 1;

static void register_frame_class() {
    WNDCLASSW wndclass;

    wndclass.style = 0;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hinst;
    wndclass.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_MAINICON));
    wndclass.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW));
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);

    RegisterClassW(&wndclass);
}

static HWND create_frame_window(Conf *conf, int guess_width, int guess_height) {
    int winmode = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
    int exwinmode = 0;
    const struct BackendVtable *vt =
        backend_vt_from_proto(be_default_protocol);
    bool resize_forbidden = false;
    if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
        resize_forbidden = true;
    if (!conf_get_bool(conf, CONF_scrollbar))
        winmode &= ~(WS_VSCROLL);
    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED ||
        resize_forbidden)
        winmode &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    if (conf_get_bool(conf, CONF_alwaysontop))
        exwinmode |= WS_EX_TOPMOST;
    if (conf_get_bool(conf, CONF_sunken_edge))
        exwinmode |= WS_EX_CLIENTEDGE;
    wchar_t *uappname = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);
    HWND hwnd = CreateWindowExW(
        exwinmode, uappname, uappname, winmode, CW_USEDEFAULT,
        CW_USEDEFAULT, guess_width, guess_height, NULL, NULL, hinst, NULL);
    sfree(uappname);
    return hwnd;
}

static void adjust_terminal_window(HWND frame_hwnd, HWND term_hwnd) {
    RECT r;
    GetClientRect(frame_hwnd, &r);
    SetWindowPos(term_hwnd, NULL, tab_bar_get_extra_width(), tab_bar_get_extra_height(),
                 r.right-r.left-tab_bar_get_extra_width(), r.bottom-r.top-tab_bar_get_extra_height(), SWP_NOZORDER);
}

static void adjust_extra_size() {
    extra_width += tab_bar_get_extra_width();
    extra_height += tab_bar_get_extra_height();
}

static void adjust_client_size(int *width, int *height) {
    *width -= tab_bar_get_extra_width();
    *height -= tab_bar_get_extra_height();
    if (*width < 0) {
      *width = 0;
    }
    if (*height < 0) {
      *height = 0;
    }
}

static bool create_conf(const char *saved_session, Conf **conf, const char **session_name) {
    *conf = conf_new();
    conf_set_int(*conf, CONF_logtype, LGTYP_NONE);
    do_defaults(saved_session, *conf);
    if (conf_launchable(*conf)) {
        *session_name = dupstr(saved_session);
        return true;
    }
    int config_result = do_config(frame_hwnd, *conf, session_name);
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

static WinGuiSession *create_frontend(Conf *conf, const char *session_name) {
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
    wgs->wintw.vt = &windows_termwin_vt;
    wgs->wintw_hdc = NULL;
    wgs->trust_icon = INVALID_HANDLE_VALUE,
    wgs->eventlogstuff.ninitial = 0;
    wgs->eventlogstuff.ncircular = 0;
    wgs->eventlogstuff.circular_first = 0;
    wgs->seat.vt = &win_seat_vt;
    wgs->logpolicy.vt = &win_gui_logpolicy_vt;
    wgs->need_backend_resize = false;
    wgs->wnd_proc.ignore_clip = false;
    wgs->syschar.pending_surrogate = 0;
    wgs->translate_key.alt_sum = 0;
    wgs->translate_key.compose_char = 0;
    wgs->translate_key.compose_keycode = 0;

    wgs->window_name = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);
    wgs->icon_name = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, appname);

    memset(&wgs->ucsdata, 0, sizeof(wgs->ucsdata));
    conf_cache_data(wgs);
    init_fonts(wgs, 0,0);
    init_palette(wgs);

    wgs->term_palette_init = true;
    Terminal *term = term_init(conf, &wgs->ucsdata, &wgs->wintw);
    term->ldisc = NULL; // missing from term_init
    term->basic_erase_char.attr |= ATTR_ERASE;
    term->erase_char.attr |= ATTR_ERASE;
    term_palette_init_fix(term);
    wgs->term = term;
    setup_clipboards(term, conf);
    wgs->logctx = log_init(&wgs->logpolicy, conf);
    term_provide_logctx(term, wgs->logctx);
    term_size(term, conf_get_int(conf, CONF_height),
              conf_get_int(conf, CONF_width),
              conf_get_int(conf, CONF_savelines));

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

static void destroy_frontend(WinGuiSession *wgs) {
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

static void set_title_from_session(WinGuiSession *wgs) {
    if (conf_get_bool(wgs->conf, CONF_win_name_always) || !IsIconic(frame_hwnd))
        SetWindowTextW(frame_hwnd, wgs->window_name);
}

static void set_icon_title_from_session(WinGuiSession *wgs) {
    if (!conf_get_bool(wgs->conf, CONF_win_name_always) && IsIconic(frame_hwnd))
        SetWindowTextW(frame_hwnd, wgs->icon_name);
}

/* Calling of wintw_set_scrollbar() is connected to drawing in term_update() in terminal.c
   This means if scroll info has changed till session is not active we don't get the
   about the new status.
   At activation we need to calculate it based on the Terminal data, but the
   update_sbar() function doing this is static.
   As a workaround the original handling is copied here. */
static void update_sbar(Terminal *term) {
    term->win_scrollbar_update_pending = false;
    int sblines = count234(term->scrollback);
    if (term->erase_to_scrollback &&
        term->alt_which && term->alt_screen) {
            sblines += term->alt_sblines;
    }
    wintw_set_scrollbar(term->win, sblines + term->rows,
                        sblines + term->disptop, term->rows);
}

static void set_scrollbar(int total, int start, int page, bool redraw) {
    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = page;
    si.nPos = start;
    SetScrollInfo(frame_hwnd, SB_VERT, &si, redraw);
}

static bool set_frame_style(Conf *conf) {
    HWND hwnd = frame_hwnd;
    HWND hwndInsertAfter = NULL;
    LONG nflg, flag = GetWindowLongPtr(hwnd, GWL_STYLE);
    LONG nexflag, exflag = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    nexflag = exflag;
    if (conf_get_bool(conf, CONF_alwaysontop) !=
        (exflag & WS_EX_TOPMOST)) {
      if (conf_get_bool(conf, CONF_alwaysontop)) {
        nexflag |= WS_EX_TOPMOST;
        hwndInsertAfter = HWND_TOPMOST;
      } else {
        nexflag &= ~(WS_EX_TOPMOST);
        hwndInsertAfter = HWND_NOTOPMOST;
      }
    }
    if (conf_get_bool(conf, CONF_sunken_edge))
        nexflag |= WS_EX_CLIENTEDGE;
    else
        nexflag &= ~(WS_EX_CLIENTEDGE);

    nflg = flag;
    if (conf_get_bool(conf, is_full_screen() ?
                      CONF_scrollbar_in_fullscreen :
                      CONF_scrollbar))
        nflg |= WS_VSCROLL;
    else
        nflg &= ~WS_VSCROLL;

    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED ||
        is_full_screen())
        nflg &= ~WS_THICKFRAME;
    else
        nflg |= WS_THICKFRAME;

    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED)
        nflg &= ~WS_MAXIMIZEBOX;
    else
        nflg |= WS_MAXIMIZEBOX;

    if (nflg != flag || nexflag != exflag) {
      if (nflg != flag)
          SetWindowLongPtr(hwnd, GWL_STYLE, nflg);
      if (nexflag != exflag)
          SetWindowLongPtr(hwnd, GWL_EXSTYLE, nexflag);

      SetWindowPos(hwnd, hwndInsertAfter, 0, 0, 0, 0,
                   SWP_NOACTIVATE | SWP_NOCOPYBITS |
                   SWP_NOMOVE | SWP_NOSIZE |
                   (hwndInsertAfter ? SWP_NOZORDER : 0) |
                   SWP_FRAMECHANGED);
      return true;
    }
    return false;
}

static void realize_palette(WinGuiSession *wgs) {
    Conf *conf = wgs->conf;
    bool got_new_palette = false;

    if (!wgs->tried_pal && conf_get_bool(conf, CONF_try_palette)) {
        HDC hdc = GetDC(term_hwnd);
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
        ReleaseDC(term_hwnd, hdc);
        wgs->tried_pal = true;
    }

    if (wgs->pal && !got_new_palette) {
        /* We already had a palette, so replace the changed colours in the
         * existing one. */
        SetPaletteEntries(wgs->pal, 0, wgs->logpal->palNumEntries, wgs->logpal->palPalEntry);

        HDC hdc = make_hdc(wgs);
        UnrealizeObject(wgs->pal);
        RealizePalette(hdc);
        free_hdc(term_hwnd, hdc);
    }
}

static void activate_session(WinGuiSession *wgs) {
    tab_bar_clear_tab_notified(wgs->tab_index);
    tab_bar_select_tab(wgs->tab_index);
    wgs_active = wgs;
    wgs->find.update_finddlg_pending = true;
    realize_palette(wgs);
    int resize_action = conf_get_int(wgs->conf, CONF_resize_action);
    bool was_zoomed = wgs->resize_either.was_zoomed;
    if (wgs->font_dpi != dpi_info.y) {
        deinit_fonts(wgs);
        init_fonts(wgs, 0, 0);
    }

    if (IsZoomed(frame_hwnd)) {
        if (!was_zoomed) {
            wgs->resize_either.was_zoomed = true;
            wgs->resize_either.font_width = wgs->font_width;
            wgs->resize_either.font_height = wgs->font_height;
        }
        if (resize_action == RESIZE_DISABLED) {
            ShowWindow(frame_hwnd, SW_RESTORE);
            force_normal(frame_hwnd);
            reset_window(wgs, -1);
        } else {
            if (resize_action == RESIZE_EITHER && !was_zoomed) {
                WINDOWPLACEMENT wp;
                wp.length = sizeof(WINDOWPLACEMENT);
                GetWindowPlacement(frame_hwnd, &wp);
                int width = wp.rcNormalPosition.right-wp.rcNormalPosition.left-extra_width+tab_bar_get_extra_width();
                int height = wp.rcNormalPosition.bottom-wp.rcNormalPosition.top-extra_height+tab_bar_get_extra_height();
                wm_size_resize_term(wgs, MAKELPARAM(width, height), false);
            }
            reset_window(wgs, 0);
            InvalidateRect(term_hwnd, NULL, true);
        }
    } else {
        wgs->resize_either.was_zoomed = false;
        if (resize_action == RESIZE_DISABLED) {
            reset_window(wgs, 1);
        } else if (resize_action == RESIZE_FONT) {
            reset_window(wgs, 0);
            InvalidateRect(term_hwnd, NULL, true);
        } else {
            if (resize_action == RESIZE_EITHER && was_zoomed) {
                deinit_fonts(wgs);
                init_fonts(wgs, wgs->resize_either.font_width, wgs->resize_either.font_height);
            }
            RECT r;
            GetClientRect(frame_hwnd, &r);
            wm_size_resize_term(wgs, MAKELPARAM(r.right-r.left, r.bottom-r.top), true);
            reset_window(wgs, 1);
        }
    }
    set_frame_style(wgs->conf);
    set_title_from_session(wgs);
    set_icon_title_from_session(wgs);
    update_sbar(wgs->term);
    update_mouse_pointer(wgs);
    reseteventlog(&wgs->eventlogstuff);
    update_finddlg(wgs);
}

static char *create_tab_title(int id, const char *session_name) {
    return dupprintf("%d. %s", id, session_name);
}

static void add_session_tab(int protocol, const char *session_name, int index) {
    char *tab_title = create_tab_title(session_counter, session_name);
    tab_bar_insert_tab(index, tab_title, protocol);
    sfree(tab_title);
}

static void add_session(Conf *conf, const char *session_name, int index) {
    if (!session_name) {
        session_name = dupstr(conf_get_str(conf, CONF_host));
    }
    add_session_tab(conf_get_int(conf, CONF_protocol), session_name, index);
    WinGuiSession *wgs = create_frontend(conf, session_name);
    pointer_array_insert(index, wgs);
    activate_session(wgs);
    start_backend(wgs);
}

static void delete_session(WinGuiSession *wgs) {
    int deleted_index = wgs->tab_index;
    int index = wgs_active->tab_index;
    if (pointer_array_size() > 1 && index == deleted_index) {
        if (index+1 == pointer_array_size()) {
            index--;
        } else {
            index++;
        }
        activate_session((WinGuiSession *)pointer_array_get(index));
    }
    tab_bar_remove_tab(deleted_index);
    pointer_array_remove(deleted_index);
    if (pointer_array_size() == 0) {
        SetFocus(NULL);
    }
    destroy_frontend(wgs);
    if (pointer_array_size() == 0) {
        wgs_active = NULL;
        DestroyWindow(frame_hwnd);
    }
}

static void show_finddlg(WinGuiSession *wgs) {
    const int default_pattern_buffer_len = 16;
    if (!wgs->find.pattern) {
        wgs->find.pattern = snewn(default_pattern_buffer_len, wchar_t);
        wgs->find.pattern_buffer_len = default_pattern_buffer_len;
        wgs->find.pattern_len = 0;
        wgs->find.pattern[0] = 0;
    }
    finddlg_create(wgs->find.pattern, true, wgs->find.ignore_case, wgs->find.whole_word);
}

static void update_finddlg(WinGuiSession *wgs) {
    if (wgs->find.pattern) {
        finddlg_create(wgs->find.pattern, false, wgs->find.ignore_case, wgs->find.whole_word);
        if (wgs->find.pattern_len > 1) {
            find_match_mask_alloc(&find_match_mask, wgs->term->rows, wgs->term->cols);
            find_display(wgs->term, wgs->find.pattern, wgs->find.pattern_len, wgs->find.ignore_case, wgs->find.whole_word, &find_match_mask);
        }
    } else {
        find_match_mask_free(&find_match_mask);
        finddlg_destroy();
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
    wgs->find.pattern_len = finddlg_get_text(wgs->find.pattern, wgs->find.pattern_buffer_len);
    assert(wgs->find.pattern_len == l);
}

static void scroll_to_row(WinGuiSession *wgs, int row) {
    term_scroll(wgs->term, 0, row);
    find_match_mask_clear(&find_match_mask);
    find_display(wgs->term, wgs->find.pattern, wgs->find.pattern_len, wgs->find.ignore_case, wgs->find.whole_word, &find_match_mask);
    term_update(wgs->term);
}

static void handle_finddlg_notify(LPARAM lParam) {
    switch (((NMHDR *)lParam)->code) {
      case FINDDLG_EDIT_CHANGED: {
        int l = finddlg_get_text(NULL, 0);
        update_find_pattern(wgs_active, l);
        if (l > 1) {
            update_find_match_mask(wgs_active);
        } else {
            drop_find_match_mask(wgs_active);
        }
        break;
      }
      case FINDDLG_IGNORE_CASE: {
        wgs_active->find.ignore_case = finddlg_get_ignore_case();
        if (find_match_mask.cells) {
            update_find_match_mask(wgs_active);
        }
        break;
      }
      case FINDDLG_WHOLE_WORD: {
        wgs_active->find.whole_word = finddlg_get_whole_word();
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
        finddlg_destroy();
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

static void handle_wm_notify(LPARAM lParam) {
    if (((NMHDR *)lParam)->idFrom == FINDDLG_NOTIFY_ID) {
        handle_finddlg_notify(lParam);
        return;
    }
    struct TBHDR *nmhdr = (struct TBHDR *)lParam;
    int index = tab_bar_get_current_tab();
    switch (nmhdr->_hdr.code) {
      case TCN_SELCHANGE: {
        activate_session((WinGuiSession *)pointer_array_get(index));
        break;
      }
      case TCN_TABEXCHANGE: {
        pointer_array_exchange(nmhdr->_tabOrigin, index);
        break;
      }
      case TCN_TABDELETE: {
        WinGuiSession *wgs = (WinGuiSession *)pointer_array_get(nmhdr->_tabOrigin);
        if (!wgs->remote_closed && conf_get_bool(wgs->conf, CONF_warn_on_close)) {
            if (index != nmhdr->_tabOrigin) {
                index = nmhdr->_tabOrigin;
                activate_session(wgs);
            }
            show_mouseptr(wgs, true);
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
                break;
            }
        }
        if (wgs->backend) {
            stop_backend(wgs);
        }
        if (wgs->remote_closed) {
            delete_callbacks_for_context(wgs);
        }
        delete_session(wgs);
        break;
      }
      case NM_RCLICK:
      {
        POINT cursorpos;

        show_mouseptr(wgs_active, true);
        GetCursorPos(&cursorpos);
        TrackPopupMenu(popup_menus[SYSMENU].menu,
                       TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       cursorpos.x, cursorpos.y,
                       0, frame_hwnd, NULL);
        break;
      }
    }
}

static void handle_wm_initmenu(WPARAM wParam) {
    HMENU menu = (HMENU)wParam;
    int resize_action = conf_get_int(wgs_active->conf, CONF_resize_action);
    EnableMenuItem(menu, IDM_FULLSCREEN, MF_BYCOMMAND |
                   (resize_action == RESIZE_DISABLED ? MF_GRAYED : MF_ENABLED));
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
