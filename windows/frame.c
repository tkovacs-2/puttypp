
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

static WinGuiFrontend *create_frontend(Conf *conf, const char *session_name) {
    WinGuiFrontend *wgf = (WinGuiFrontend *)smalloc(sizeof(WinGuiFrontend));

    memset(wgf, 0, sizeof(*wgf));
    wgf->conf = conf;

    wgf->caret_x = -1;
    wgf->caret_y = -1;
    wgf->specials = NULL;
    wgf->specials_menu = NULL;
    wgf->n_specials = 0;
    wgf->tried_pal = false;
    wgf->colorref_modifier = 0;
    wgf->send_raw_mouse = false;
    wgf->wheel_accumulator = 0;
    wgf->pointer_indicates_raw_mouse = false;
    wgf->busy_status = BUSY_NOT;
    wgf->compose_state = 0;
    wgf->wintw.vt = &windows_termwin_vt;
    wgf->wintw_hdc = NULL;
    wgf->trust_icon = INVALID_HANDLE_VALUE,
    wgf->eventlogstuff.ninitial = 0;
    wgf->eventlogstuff.ncircular = 0;
    wgf->eventlogstuff.circular_first = 0;
    wgf->seat.vt = &win_seat_vt;
    wgf->logpolicy.vt = &win_gui_logpolicy_vt;
    wgf->need_backend_resize = false;
    wgf->request_resize.first_time = 1;
    wgf->wnd_proc.ignore_clip = false;
    wgf->syschar.pending_surrogate = 0;
    wgf->translate_key.alt_sum = 0;
    wgf->translate_key.compose_char = 0;
    wgf->translate_key.compose_keycode = 0;

    memset(&wgf->ucsdata, 0, sizeof(wgf->ucsdata));
    conf_cache_data(wgf);
    init_fonts(wgf, 0,0);
    init_palette(wgf);

    Terminal *term = term_init(conf, &wgf->ucsdata, &wgf->wintw);
    wgf->term = term;
    setup_clipboards(term, conf);
    wgf->logctx = log_init(&wgf->logpolicy, conf);
    term_provide_logctx(term, wgf->logctx);
    term_size(term, conf_get_int(conf, CONF_height),
              conf_get_int(conf, CONF_width),
              conf_get_int(conf, CONF_savelines));

    char *bits;
    int size = (wgf->font_width + 15) / 16 * 2 * wgf->font_height;
    bits = snewn(size, char);
    memset(bits, 0, size);
    wgf->caretbm = CreateBitmap(wgf->font_width, wgf->font_height, 1, 1, bits);
    sfree(bits);

    wgf->session_id = session_counter++;
    wgf->session_name = session_name;
    wgf->remote_closed = true;

    wgf->si.total = wgf->term->rows;
    wgf->si.page = wgf->term->rows;
    wgf->si.start = 0;

    wgf->cursor_visible = true;
    wgf->cursor_forced_visible = false;

    return wgf;
}

static void destroy_frontend(WinGuiFrontend *wgf) {
    DeleteObject(wgf->caretbm);

    log_free(wgf->logctx);
    term_free(wgf->term);

    sfree(wgf->logpal);
    if (wgf->pal)
        DeleteObject(wgf->pal);
    deinit_fonts(wgf);

    if (conf_get_int(wgf->conf, CONF_protocol) == PROT_SSH) {
        random_save_seed();
    }
    conf_free(wgf->conf);

    sfree((char *)wgf->session_name);
    sfree(wgf);
}

static void set_title_from_session(WinGuiFrontend *wgf) {
    if (conf_get_bool(wgf->conf, CONF_win_name_always) || !IsIconic(frame_hwnd))
        SetWindowText(frame_hwnd, wgf->window_name);
}

static void set_icon_title_from_session(WinGuiFrontend *wgf) {
    if (!conf_get_bool(wgf->conf, CONF_win_name_always) && IsIconic(frame_hwnd))
        SetWindowText(frame_hwnd, wgf->icon_name);
}

static void set_scrollbar_from_session(WinGuiFrontend *wgf, bool redraw) {
    if (!conf_get_bool(wgf->conf, is_full_screen() ?
                       CONF_scrollbar_in_fullscreen : CONF_scrollbar))
        return;

    SCROLLINFO si;
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = wgf->si.total - 1;
    si.nPage = wgf->si.page;
    si.nPos = wgf->si.start;
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

static void realize_palette(WinGuiFrontend *wgf) {
    Conf *conf = wgf->conf;
    bool got_new_palette = false;

    if (!wgf->tried_pal && conf_get_bool(conf, CONF_try_palette)) {
        HDC hdc = GetDC(term_hwnd);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            wgf->pal = CreatePalette(wgf->logpal);
            if (wgf->pal) {
                SelectPalette(hdc, wgf->pal, false);
                RealizePalette(hdc);
                SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);

                /* Convert all RGB() values in colours[] into PALETTERGB(),
                 * and ensure we stick to that later */
                wgf->colorref_modifier = PALETTERGB(0, 0, 0) ^ RGB(0, 0, 0);
                for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
                    wgf->colours[i] ^= wgf->colorref_modifier;

                /* Inhibit the SetPaletteEntries call below */
                got_new_palette = true;
            }
        }
        ReleaseDC(term_hwnd, hdc);
        wgf->tried_pal = true;
    }

    if (wgf->pal && !got_new_palette) {
        /* We already had a palette, so replace the changed colours in the
         * existing one. */
        SetPaletteEntries(wgf->pal, 0, wgf->logpal->palNumEntries, wgf->logpal->palPalEntry);

        HDC hdc = make_hdc(wgf);
        UnrealizeObject(wgf->pal);
        RealizePalette(hdc);
        free_hdc(term_hwnd, hdc);
    }
}

static void activate_session(WinGuiFrontend *wgf) {
    tab_bar_select_tab(wgf->tab_index);
    wgf_active = wgf;
    realize_palette(wgf);
    int resize_action = conf_get_int(wgf->conf, CONF_resize_action);
    bool was_zoomed = wgf->resize_either.was_zoomed;
    if (IsZoomed(frame_hwnd)) {
        if (!was_zoomed) {
            wgf->resize_either.was_zoomed = true;
            wgf->resize_either.font_width = wgf->font_width;
            wgf->resize_either.font_height = wgf->font_height;
        }
        if (resize_action == RESIZE_DISABLED) {
            ShowWindow(frame_hwnd, SW_RESTORE);
            force_normal(frame_hwnd);
            reset_window(wgf, -1);
        } else {
            if (resize_action == RESIZE_EITHER && !was_zoomed) {
                WINDOWPLACEMENT wp;
                wp.length = sizeof(WINDOWPLACEMENT);
                GetWindowPlacement(frame_hwnd, &wp);
                int width = wp.rcNormalPosition.right-wp.rcNormalPosition.left-extra_width+tab_bar_get_extra_width();
                int height = wp.rcNormalPosition.bottom-wp.rcNormalPosition.top-extra_height+tab_bar_get_extra_height();
                wm_size_resize_term(wgf, MAKELPARAM(width, height), false);
            }
            reset_window(wgf, 0);
            InvalidateRect(term_hwnd, NULL, true);
        }
    } else {
        wgf->resize_either.was_zoomed = false;
        if (resize_action == RESIZE_DISABLED) {
            reset_window(wgf, 1);
        } else if (resize_action == RESIZE_FONT) {
            reset_window(wgf, 0);
            InvalidateRect(term_hwnd, NULL, true);
        } else {
            if (resize_action == RESIZE_EITHER && was_zoomed) {
                deinit_fonts(wgf);
                init_fonts(wgf, wgf->resize_either.font_width, wgf->resize_either.font_height);
            }
            RECT r;
            GetClientRect(frame_hwnd, &r);
            wm_size_resize_term(wgf, MAKELPARAM(r.right-r.left, r.bottom-r.top), true);
            reset_window(wgf, 1);
        }
    }
    set_frame_style(wgf->conf);
    set_title_from_session(wgf);
    set_icon_title_from_session(wgf);
    set_scrollbar_from_session(wgf, true);
    update_mouse_pointer(wgf);
    reseteventlog(&wgf->eventlogstuff);
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
    WinGuiFrontend *wgf = create_frontend(conf, session_name);
    pointer_array_insert(index, wgf);
    activate_session(wgf);
    start_backend(wgf);
}

static void delete_session(WinGuiFrontend *wgf) {
    int deleted_index = wgf->tab_index;
    int index = wgf_active->tab_index;
    tab_bar_remove_tab(deleted_index);
    pointer_array_remove(deleted_index);
    if (pointer_array_size() == 0) {
        SetFocus(NULL);
    }
    destroy_frontend(wgf);
    if (pointer_array_size() == 0) {
        wgf_active = NULL;
        DestroyWindow(frame_hwnd);
    } else if (index == deleted_index) {
        if (index == pointer_array_size()) {
            index--;
        }
        activate_session((WinGuiFrontend *)pointer_array_get(index));
    }
}

static void handle_wm_notify(LPARAM lParam) {
    struct TBHDR *nmhdr = (struct TBHDR *)lParam;
    int index = tab_bar_get_current_tab();
    switch (nmhdr->_hdr.code) {
      case TCN_SELCHANGE: {
        activate_session((WinGuiFrontend *)pointer_array_get(index));
        break;
      }
      case TCN_TABEXCHANGE: {
        pointer_array_exchange(nmhdr->_tabOrigin, index);
        break;
      }
      case TCN_TABDELETE: {
        WinGuiFrontend *wgf = (WinGuiFrontend *)pointer_array_get(nmhdr->_tabOrigin);
        if (!wgf->remote_closed && conf_get_bool(wgf->conf, CONF_warn_on_close)) {
            if (index != nmhdr->_tabOrigin) {
                index = nmhdr->_tabOrigin;
                activate_session(wgf);
            }
            show_mouseptr(wgf, true);
            char *title, *msg, *additional = NULL;
            title = dupprintf("%s Session Close Confirmation", appname);
            if (wgf->backend && wgf->backend->vt->close_warn_text) {
                additional = wgf->backend->vt->close_warn_text(wgf->backend);
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
        if (wgf->backend) {
            stop_backend(wgf);
        }
        if (wgf->remote_closed) {
            delete_callbacks_for_context(wgf);
        }
        delete_session(wgf);
        break;
      }
      case NM_RCLICK:
      {
        POINT cursorpos;

        show_mouseptr(wgf_active, true);
        GetCursorPos(&cursorpos);
        TrackPopupMenu(popup_menus[SYSMENU].menu,
                       TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                       cursorpos.x, cursorpos.y,
                       0, frame_hwnd, NULL);
        break;
      }
    }
}

static void handle_wm_initmenu() {
    int resize_action = conf_get_int(wgf_active->conf, CONF_resize_action);
    for (int i = 0; i < lenof(popup_menus); i++) {
        EnableMenuItem(popup_menus[i].menu, IDM_FULLSCREEN, MF_BYCOMMAND |
                       (resize_action == RESIZE_DISABLED ? MF_GRAYED : MF_ENABLED));
        /*
         * Destroy the Restart Session menu item. (This will return
         * failure if it's already absent, as it will be the very first
         * time we call this function. We ignore that, because as long
         * as the menu item ends up not being there, we don't care
         * whether it was us who removed it or not!)
         */
        DeleteMenu(popup_menus[i].menu, IDM_RESTART, MF_BYCOMMAND);
        if (wgf_active->remote_closed) {
            /*
             * Show the Restart Session menu item. Do a precautionary
             * delete first to ensure we never end up with more than one.
             */
            InsertMenu(popup_menus[i].menu, IDM_DUPSESS, MF_BYCOMMAND | MF_ENABLED,
                       IDM_RESTART, "&Restart Session");
        }
    }
}
