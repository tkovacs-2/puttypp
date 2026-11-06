
typedef struct StubTabItem {
    char text[16];
    int iImage;
    DWORD dwState;
    char extra[16];
} StubTabItem;


typedef struct StubWindow {
    struct StubWindow *parent;
    struct StubWindow *first_child;
    struct StubWindow *next_sibling;
    struct StubWindow *prev_sibling;
    struct StubWindow *z_prev; /* GW_HWNDPREV chain */
    struct StubWindow *owner;

    WCHAR class_name[64];
    WCHAR window_text[512];
    WNDPROC wndproc;
    LONG_PTR user_data;
    LONG_PTR dlg_user;
    LONG style;
    LONG exstyle;
    RECT rect;
    RECT rect_restore;
    RECT client_rect;
    INT_PTR ctrl_id;
    HFONT font;
    bool enabled;
    int size_state;
    bool defer_winposchanged;

    bool is_dialog;

    StubTabItem *tab_items;
    int tab_count;
    int tab_capacity;
    int tab_cur_sel;
    int tab_item_extra;
    LPARAM tab_imagelist;
    int tab_pad_x, tab_pad_y;

    SCROLLINFO scroll_vert;

    BYTE layered_alpha;

    UINT button_check;
} StubWindow;

static LRESULT CALLBACK button_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static StubWindow *focus_wnd;
static StubWindow *capture_wnd;
static StubWindow *active_wnd;
static WNDCLASSW registered_classes[16] =
    {{0, button_proc, 0, 0, NULL, NULL, NULL, NULL, NULL, L"BUTTON"}};
static int registered_class_count = 1;

static StubWindow *invalidated_wnds[16];
static int invalidated_wnd_count = 0;

static StubWindow *wnd_from_hwnd(HWND hwnd) {
    return (StubWindow *)hwnd;
}

static HWND hwnd_from_wnd(StubWindow *w) {
    return (HWND)w;
}

static void link_child(StubWindow *parent, StubWindow *child) {
    child->parent = parent;
    child->next_sibling = parent->first_child;
    if (parent->first_child) {
        parent->first_child->prev_sibling = child;
    }
    parent->first_child = child;
    child->z_prev = parent;
}

static void unlink_child(StubWindow *child) {
    if (child->parent) {
        if (child->parent->first_child == child) {
            child->parent->first_child = child->next_sibling;
        }
        if (child->prev_sibling) {
            child->prev_sibling->next_sibling = child->next_sibling;
        }
        if (child->next_sibling) {
            child->next_sibling->prev_sibling = child->prev_sibling;
        }
    }
    child->parent = NULL;
    child->prev_sibling = child->next_sibling = NULL;
}

static StubWindow *alloc_window(void) {
    StubWindow *w = malloc(sizeof(StubWindow));
    memset(w, 0, sizeof(*w));
    w->enabled = true;
    w->tab_cur_sel = -1;
    w->tab_item_extra = sizeof(TCITEMHEADER);
    w->scroll_vert.cbSize = sizeof(SCROLLINFO);
    w->layered_alpha = 255;
    return w;
}

static const WNDCLASSW *find_class(LPCWSTR name) {
    for (int i = 0; i < registered_class_count; i++) {
        if (wcscmp(registered_classes[i].lpszClassName, name) == 0) {
            return &registered_classes[i];
        }
    }
    return NULL;
}

static void adjust_client_from_window(StubWindow *w) {
    int border = (w->style & (WS_BORDER | WS_THICKFRAME) ? 5 : 0);
    int caption = (w->style & WS_CAPTION) ? 30 : 0;
    int vscroll = (w->style & WS_VSCROLL) ? 10 : 0;
    int hscroll = (w->style & WS_HSCROLL) ? 10 : 0;
    int edge = (w->exstyle & WS_EX_CLIENTEDGE) ? 2 : 0;
    w->client_rect.left = w->rect.left + border + edge;
    w->client_rect.top = w->rect.top + caption + border + edge;
    w->client_rect.right = w->rect.right - border - vscroll - edge;
    w->client_rect.bottom = w->rect.bottom - border - hscroll - edge;
    if (w->client_rect.right < w->client_rect.left) {
        w->client_rect.right = w->client_rect.left;
    }
    if (w->client_rect.bottom < w->client_rect.top) {
        w->client_rect.bottom = w->client_rect.top;
    }
}

static void set_window_rect(StubWindow *w, int x, int y, int width, int height) {
    assert(width >= 0 && height >= 0);
    w->rect.left = x;
    w->rect.top = y;
    w->rect.right = x + width;
    w->rect.bottom = y + height;
    adjust_client_from_window(w);
}

static void tab_ensure(StubWindow *w, int count) {
    if (count <= w->tab_capacity) {
        return;
    }
    int cap = w->tab_capacity ? w->tab_capacity * 2 : 4;
    while (cap < count) {
        cap *= 2;
    }
    w->tab_items = realloc(w->tab_items, cap * sizeof(StubTabItem));
    memset(w->tab_items + w->tab_capacity, 0,
        (cap - w->tab_capacity) * sizeof(StubTabItem));
    w->tab_capacity = cap;
}

static LRESULT send_message(StubWindow *w, UINT msg, WPARAM wp, LPARAM lp) {
    return w->wndproc(hwnd_from_wnd(w), msg, wp, lp);
}

static bool is_window_visible(StubWindow *w) {
    if (!(w->style & WS_VISIBLE)) {
        return false;
    }
    if (w->parent) {
        return is_window_visible(w->parent);
    }
    return true;
}

static void set_window_visible(StubWindow *w, bool visible) {
    w->style = (w->style & ~WS_VISIBLE) | (visible ? WS_VISIBLE : 0);
}

static void set_active_window(StubWindow *w) {
    if (w == active_wnd) {
        return;
    }
    if (w->style & WS_CHILD) {
        return;
    }
    if (!is_window_visible(w)) {
        return;
    }
    StubWindow *old = active_wnd;
    if (old) {
        send_message(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), (LPARAM)hwnd_from_wnd(old));
    }
    active_wnd = w;
    send_message(w, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), (LPARAM)old);
}

static void send_child_windowposchanged(StubWindow *w) {
    for (StubWindow *c = w->first_child; c; c = c->next_sibling) {
        if (is_window_visible(c)) {
            send_child_windowposchanged(c);
        }
    }
    WINDOWPOS pos = {0};
    pos.hwnd = hwnd_from_wnd(w);
    pos.hwndInsertAfter = NULL;
    pos.flags = SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER;
    if (w->defer_winposchanged) {
        pos.x = w->rect.left;
        pos.y = w->rect.top;
        pos.cx = w->rect.right - w->rect.left;
        pos.cy = w->rect.bottom - w->rect.top;
    } else {
        pos.flags |= SWP_NOMOVE | SWP_NOSIZE;
    }
    send_message(w, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
    w->defer_winposchanged = false;
}

static void set_window_pos(StubWindow *wnd, WINDOWPOS *winpos) {
    WINDOWPOS pos = *winpos;
    bool move = !(pos.flags & SWP_NOMOVE);
    bool size = !(pos.flags & SWP_NOSIZE);
    bool show = (pos.flags & SWP_SHOWWINDOW);
    bool hide = (pos.flags & SWP_HIDEWINDOW);
    bool activate = !(pos.flags & SWP_NOACTIVATE);
    int x = pos.x;
    int y = pos.y;
    int w = pos.cx;
    int h = pos.cy;
    if (move && wnd->rect.left == x && wnd->rect.top == y) {
        if (!wnd->defer_winposchanged) {
            pos.flags |= SWP_NOMOVE;
            move = false;
        }
    }
    if (size && wnd->rect.right == x + w && wnd->rect.bottom == y + h) {
        if (!wnd->defer_winposchanged) {
            pos.flags |= SWP_NOSIZE;
            size = false;
        }
    }
    if (move || size) {
        if (!move) {
            x = wnd->rect.left;
            y = wnd->rect.top;
        }
        if (!size) {
            w = wnd->rect.right - wnd->rect.left;
            h = wnd->rect.bottom - wnd->rect.top;
        }
        set_window_rect(wnd, x, y, w, h);
    } else if (pos.flags & SWP_FRAMECHANGED) {
        adjust_client_from_window(wnd);
        pos.flags &= ~SWP_NOSIZE;
        size = true;
    }
    bool sent = false;
    if (show && !(wnd->style & WS_VISIBLE)) {
        set_window_visible(wnd, true);
        if (is_window_visible(wnd)) {
            for (StubWindow *c = wnd->first_child; c; c = c->next_sibling) {
                if (is_window_visible(c)) {
                    send_child_windowposchanged(c);
                }
            }
            if (wnd->defer_winposchanged) {

                pos.x = wnd->rect.left;
                pos.y = wnd->rect.top;
                pos.cx = wnd->rect.right - wnd->rect.left;
                pos.cy = wnd->rect.bottom - wnd->rect.top;
                pos.flags &= ~SWP_NOSIZE;
                send_message(wnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
                wnd->defer_winposchanged = false;
            } else {
                send_message(wnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
            }
            InvalidateRect(hwnd_from_wnd(wnd), NULL, FALSE);
            sent = true;
        }
    } else if (hide && is_window_visible(wnd)) {
        set_window_visible(wnd, false);
        send_message(wnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
        sent = true;
    }
    if (activate) {
        set_active_window(wnd);
    }
    if (!sent && !wnd->defer_winposchanged && (size || move)) {
        send_message(wnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
    }
}

static bool is_parent_of_child(StubWindow *w, StubWindow *c) {
    if (w == c) {
        return true;
    }
    if (c->parent == NULL) {
        return false;
    }
    return is_parent_of_child(w, c->parent);
}

static LRESULT CALLBACK tab_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    switch (msg) {
      case TCM_GETITEMCOUNT:
        return w->tab_count;
      case TCM_GETCURSEL:
        return w->tab_cur_sel;
      case TCM_SETCURSEL:
        w->tab_cur_sel = (int)wp;
        return 0;
      case TCM_SETITEMEXTRA:
        w->tab_item_extra = (int)wp;
        return TRUE;
      case TCM_SETPADDING:
        w->tab_pad_x = LOWORD(lp);
        w->tab_pad_y = HIWORD(lp);
        return 0;
      case TCM_SETIMAGELIST:
        w->tab_imagelist = lp;
        return 0;
      case TCM_INSERTITEM: {
        int index = (int)wp;
        TCITEM *item = (TCITEM *)lp;
        if (index < 0 || index > w->tab_count) {
            index = w->tab_count;
        }
        tab_ensure(w, w->tab_count + 1);
        if (index < w->tab_count) {
            memmove(&w->tab_items[index + 1], &w->tab_items[index],
                (w->tab_count - index) * sizeof(StubTabItem));
        }
        memset(&w->tab_items[index], 0, sizeof(StubTabItem));
        if (item && (item->mask & TCIF_TEXT)) {
            strncpy(w->tab_items[index].text, item->pszText, sizeof(w->tab_items[index].text) - 1);
            w->tab_items[index].text[sizeof(w->tab_items[index].text) - 1] = '\0';
        }
        if (item && (item->mask & TCIF_IMAGE)) {
            w->tab_items[index].iImage = item->iImage;
        }
        if (item && (item->mask & TCIF_STATE)) {
            w->tab_items[index].dwState = item->dwState;
        }
        w->tab_count++;
        if (w->tab_cur_sel < 0) {
            w->tab_cur_sel = 0;
        }
        return index;
      }
      case TCM_DELETEITEM: {
        int index = (int)wp;
        if (index < 0 || index >= w->tab_count) {
            return FALSE;
        }
        memmove(&w->tab_items[index], &w->tab_items[index + 1],
            (w->tab_count - index - 1) * sizeof(StubTabItem));
        w->tab_count--;
        if (w->tab_cur_sel >= w->tab_count) {
            w->tab_cur_sel = w->tab_count - 1;
        }
        return TRUE;
      }
      case TCM_GETITEM: {
        int index = (int)wp;
        TCITEM *item = (TCITEM *)lp;
        if (index < 0 || index >= w->tab_count || !item) {
            return FALSE;
        }
        StubTabItem *src = &w->tab_items[index];
        if (item->mask & TCIF_TEXT) {
            if (item->pszText && item->cchTextMax > 0) {
                strncpy(item->pszText, src->text, item->cchTextMax - 1);
                item->pszText[item->cchTextMax - 1] = L'\0';
            }
        }
        if (item->mask & TCIF_IMAGE) {
            item->iImage = src->iImage;
        }
        if (item->mask & TCIF_STATE) {
            item->dwState = src->dwState;
        }
        if (w->tab_item_extra > (int)sizeof(TCITEMHEADER)) {
            size_t extra = w->tab_item_extra - sizeof(TCITEMHEADER);
            memcpy((char *)item + sizeof(TCITEMHEADER), src->extra,
                extra < sizeof(src->extra) ? extra : sizeof(src->extra));
        }
        return TRUE;
      }
      case TCM_SETITEM: {
        int index = (int)wp;
        TCITEM *item = (TCITEM *)lp;
        if (index < 0 || index >= w->tab_count || !item) {
            return FALSE;
        }
        StubTabItem *dst = &w->tab_items[index];
        if (item->mask & TCIF_TEXT) {
            strcpy(dst->text, item->pszText);
        }
        if (item->mask & TCIF_IMAGE) {
            dst->iImage = item->iImage;
        }
        if (item->mask & TCIF_STATE) {
            dst->dwState = item->dwState;
        }
        if (w->tab_item_extra > (int)sizeof(TCITEMHEADER)) {
            size_t extra = w->tab_item_extra - sizeof(TCITEMHEADER);
            memcpy(dst->extra, (char *)item + sizeof(TCITEMHEADER),
                extra < sizeof(dst->extra) ? extra : sizeof(dst->extra));
        }
        return TRUE;
      }
      case TCM_GETITEMRECT: {
        int index = (int)wp;
        RECT *r = (RECT *)lp;
        if (!r || index < 0 || index >= w->tab_count) {
            return FALSE;
        }
        int tab_w = 80;
        r->left = index * tab_w;
        r->top = 0;
        r->right = (index + 1) * tab_w;
        r->bottom = 24;
        return TRUE;
      }
      case TCM_ADJUSTRECT: {
        RECT *r = (RECT *)lp;
        if (!r) {
            return 0;
        }
        if (wp) {
            r->top -= 24;
        } else {
            r->top += 24;
        }
        return 0;
      }
      case TCM_HITTEST:
        return -1;
    }
    return 0;
}

static LRESULT CALLBACK button_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    switch (msg) {
      case BM_SETCHECK:
        w->button_check = (UINT)wp;
        return 0;
      case BM_GETCHECK:
        return w->button_check;
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Window class registration and creation                                       */
/* -------------------------------------------------------------------------- */

WINUSERAPI ATOM WINAPI RegisterClassW(const WNDCLASSW *wc) {
    assert(registered_class_count < (sizeof(registered_classes) / sizeof(registered_classes[0])));
    registered_classes[registered_class_count] = *wc;
    registered_classes[registered_class_count].lpszClassName = wc->lpszClassName;
    registered_class_count++;
    return (ATOM)registered_class_count;
}

static HWND create_window_ex(DWORD exstyle, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    const WNDCLASSW *reg = find_class(cls);
    StubWindow *wnd = alloc_window();

    wnd->wndproc = reg->lpfnWndProc;
    wcsncpy(wnd->class_name, cls ? cls : L"", 63);
    wcsncpy(wnd->window_text, title ? title : L"", 511);

    wnd->style = (LONG)style;
    wnd->exstyle = (LONG)exstyle;
    wnd->ctrl_id = menu ? (INT_PTR)menu : 0;
    wnd->enabled = true;

    bool is_child = (style & (WS_CHILD)) != 0;
    bool is_overlapped = (style & (WS_POPUP|WS_CHILD)) == 0;
    if (x == CW_USEDEFAULT) {
        x = 0;
    }
    if (y == CW_USEDEFAULT) {
        y = 0;
    }
    if (w == CW_USEDEFAULT) {
        w = (is_overlapped ? monitor_info.screen_width/2 : 0);
    }
    if (h == CW_USEDEFAULT) {
        h = (is_overlapped ? monitor_info.screen_height/2 : 0);
    }
    set_window_rect(wnd, x, y, w, h);
    wnd->rect_restore = wnd->rect;

    StubWindow *p = parent ? wnd_from_hwnd(parent) : NULL;
    if (p || is_child) {
        link_child(p, wnd);
    }
    HWND hwnd = hwnd_from_wnd(wnd);

    CREATESTRUCTW cs;
    memset(&cs, 0, sizeof(cs));
    cs.lpCreateParams = param;
    cs.hInstance = inst;
    cs.hwndParent = parent;
    cs.style = style;
    cs.dwExStyle = exstyle;
    cs.x = x;
    cs.y = y;
    cs.cx = w;
    cs.cy = h;
    cs.lpszName = title;
    cs.lpszClass = cls;
    send_message(wnd, WM_CREATE, 0, (LPARAM)&cs);

    if (is_window_visible(wnd)) {
        WINDOWPOS pos;
        pos.hwnd = hwnd;
        pos.hwndInsertAfter = HWND_TOP;
        pos.x = x;
        pos.y = y;
        pos.cx = w;
        pos.cy = h;
        pos.flags = SWP_SHOWWINDOW;
        set_window_pos(wnd, &pos);
    } else {
        wnd->defer_winposchanged = true;
    }
    return hwnd;
}

WINUSERAPI HWND WINAPI CreateWindowExW(DWORD exstyle, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    return create_window_ex(exstyle, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

WINUSERAPI BOOL WINAPI DestroyWindow(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (focus_wnd && is_parent_of_child(w, focus_wnd)) {
        StubWindow *old = focus_wnd;
        focus_wnd = NULL;
        send_message(old, WM_KILLFOCUS, 0, 0);
    }
    if (active_wnd && is_parent_of_child(w, active_wnd)) {
        StubWindow *old = active_wnd;
        active_wnd = NULL;
        send_message(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    }
    if (capture_wnd && is_parent_of_child(w, capture_wnd)) {
        StubWindow *old = capture_wnd;
        capture_wnd = NULL;
        send_message(old, WM_CAPTURECHANGED, 0, 0);
    }
    send_message(w, WM_DESTROY, 0, 0);
    while (w->first_child) {
        DestroyWindow(hwnd_from_wnd(w->first_child));
    }
    unlink_child(w);
    free(w->tab_items);
    free(w);
    return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Window geometry, state, focus                                              */
/* -------------------------------------------------------------------------- */

WINUSERAPI BOOL WINAPI GetClientRect(HWND hwnd, LPRECT r) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    *r = w->client_rect;
    OffsetRect(r, -w->client_rect.left, -w->client_rect.top);
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT r) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    *r = w->rect;
    if (w->style & WS_CHILD) {
        int x = 0;
        int y = 0;
        do {
            w = w->parent;
            x += w->client_rect.left;
            y += w->client_rect.top;
        } while (w->style & WS_CHILD);
        OffsetRect(r, x, y);
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    WINDOWPOS pos;
    pos.hwnd = hwnd;
    pos.hwndInsertAfter = after;
    pos.x = x;
    pos.y = y;
    pos.cx = cx;
    pos.cy = cy;
    pos.flags = flags;
    set_window_pos(wnd_from_hwnd(hwnd), &pos);
    return TRUE;
}

WINUSERAPI LONG_PTR WINAPI SetWindowLongPtr(HWND hwnd, int idx, LONG_PTR val) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (w->is_dialog && idx == DWLP_USER) {
        LONG_PTR old = w->dlg_user;
        w->dlg_user = val;
        return old;
    }
    switch (idx) {
      case GWLP_USERDATA: {
        LONG_PTR old = w->user_data;
        w->user_data = val;
        return old;
      }
      case GWLP_WNDPROC: {
        WNDPROC old = w->wndproc;
        w->wndproc = (WNDPROC)val;
        return (LONG_PTR)old;
      }
      case GWL_STYLE: {
        LONG old = w->style;
        w->style = (LONG)val;
        adjust_client_from_window(w);
        return old;
      }
      case GWL_EXSTYLE: {
        LONG old = w->exstyle;
        w->exstyle = (LONG)val;
        return old;
      }
    }
    return 0;
}

WINUSERAPI LONG_PTR WINAPI GetWindowLongPtr(HWND hwnd, int idx) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w) {
        return 0;
    }
    if (w->is_dialog && idx == DWLP_USER) {
        return w->dlg_user;
    }
    switch (idx) {
      case GWLP_USERDATA: return w->user_data;
      case GWLP_WNDPROC: return (LONG_PTR)w->wndproc;
      case GWL_STYLE: return w->style;
      case GWL_EXSTYLE: return w->exstyle;
    }
    return 0;
}

WINUSERAPI BOOL WINAPI SetWindowTextW(HWND hwnd, LPCWSTR text) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w) {
        return FALSE;
    }
    wcsncpy(w->window_text, text ? text : L"", 511);
    w->window_text[511] = L'\0';
    return TRUE;
}

WINUSERAPI int WINAPI GetWindowTextW(HWND hwnd, LPWSTR buf, int max) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w || !buf || max <= 0) {
        return 0;
    }
    wcsncpy(buf, w->window_text, max - 1);
    buf[max - 1] = L'\0';
    return (int)wcslen(buf);
}

WINUSERAPI int WINAPI GetWindowTextLengthW(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    return w ? (int)wcslen(w->window_text) : 0;
}

WINUSERAPI HWND WINAPI GetParent(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    return w && w->parent ? hwnd_from_wnd(w->parent) : NULL;
}

WINUSERAPI HWND WINAPI GetDlgItem(HWND dlg, int id) {
    StubWindow *d = wnd_from_hwnd(dlg);
    if (!d) {
        return NULL;
    }
    for (StubWindow *c = d->first_child; c; c = c->next_sibling) {
        if (c->ctrl_id == id) {
            return hwnd_from_wnd(c);
        }
    }
    return NULL;
}

WINUSERAPI HWND WINAPI GetWindow(HWND hwnd, UINT cmd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w) {
        return NULL;
    }
    switch (cmd) {
      case GW_OWNER:
        return hwnd_from_wnd(w->owner);
      case GW_HWNDPREV:
        return w->z_prev ? hwnd_from_wnd(w->z_prev) : NULL;
      case GW_CHILD:
        return w->first_child ? hwnd_from_wnd(w->first_child) : NULL;
    }
    return NULL;
}

WINUSERAPI HWND WINAPI SetFocus(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (focus_wnd == w) {
        return hwnd;
    }
    StubWindow *old = focus_wnd;
    if (old) {
        send_message(old, WM_KILLFOCUS, 0, 0);
    }
    focus_wnd = w;
    if (w) {
        send_message(w, WM_SETFOCUS, 0, 0);
    }
    return hwnd_from_wnd(old);
}

WINUSERAPI HWND WINAPI GetFocus(void) {
    return hwnd_from_wnd(focus_wnd);
}

WINUSERAPI HWND WINAPI GetActiveWindow(void) {
    return hwnd_from_wnd(active_wnd);
}

WINUSERAPI BOOL WINAPI IsChild(HWND parent, HWND child) {
    StubWindow *c = wnd_from_hwnd(child);
    while (c) {
        if (c->parent && hwnd_from_wnd(c->parent) == parent) {
            return TRUE;
        }
        c = c->parent;
    }
    return FALSE;
}

WINUSERAPI BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT pt) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w || !pt) {
        return FALSE;
    }
    pt->x += w->rect.left;
    pt->y += w->rect.top;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT pt) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w || !pt) {
        return FALSE;
    }
    pt->x -= w->rect.left;
    pt->y -= w->rect.top;
    return TRUE;
}

WINUSERAPI int WINAPI MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT count) {
    for (UINT i = 0; i < count; i++) {
        if (from) {
            ClientToScreen(from, &pts[i]);
        }
        if (to) {
            ScreenToClient(to, &pts[i]);
        }
    }
    return 0;
}

WINUSERAPI BOOL WINAPI ShowWindow(HWND hwnd, int cmd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    bool old_visible = is_window_visible(w);

    int flags = SWP_NOZORDER | SWP_NOSIZE | SWP_NOMOVE;
    switch (cmd) {
      case SW_HIDE:
        flags |= SWP_HIDEWINDOW;
        break;
      case SW_SHOWMINNOACTIVE:
        w->style |= WS_MINIMIZE;
        flags |= (SWP_SHOWWINDOW | SWP_NOACTIVATE);
        break;
      case SW_SHOWMINIMIZED:
        w->style |= WS_MINIMIZE;
        flags |= SWP_SHOWWINDOW;
        break;
      case SW_MINIMIZE:
        w->style |= WS_MINIMIZE;
        break;
      case SW_MAXIMIZE:
        if (!(w->style & WS_MAXIMIZE)) {
            w->style |= WS_MAXIMIZE;
            set_window_rect(w, 0, 0, monitor_info.screen_width, monitor_info.screen_height);
            w->rect_restore = w->rect;
            flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        break;
      case SW_SHOWNA:
        flags |= (SWP_SHOWWINDOW | SWP_NOACTIVATE);
        break;
      case SW_SHOW:
        flags |= SWP_SHOWWINDOW;
        break;
      case SW_SHOWNOACTIVATE:
        flags |= SWP_NOACTIVATE;
      case SW_SHOWNORMAL:
        if (w->style & WS_MAXIMIZE) {
            set_window_rect(w, w->rect_restore.left, w->rect_restore.top, w->rect_restore.right - w->rect_restore.left, w->rect_restore.bottom - w->rect_restore.top);
            flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
        flags |= SWP_SHOWWINDOW;
        break;
      default:
        assert(false);
    }
    WINDOWPOS pos;
    pos.hwnd = hwnd;
    pos.hwndInsertAfter = NULL;
    pos.x = w->rect.left;
    pos.y = w->rect.top;
    pos.cx = w->rect.right - w->rect.left;
    pos.cy = w->rect.bottom - w->rect.top;
    pos.flags = flags;
    set_window_pos(w, &pos);

    return old_visible ? FALSE : TRUE;
}

WINUSERAPI BOOL WINAPI IsIconic(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    return w->size_state == SIZE_MINIMIZED;
}

WINUSERAPI BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *, BOOL) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    for (int i = 0; i < invalidated_wnd_count; i++) {
        if (invalidated_wnds[i] == w) {
            return TRUE;
        }
    }
    assert(invalidated_wnd_count < sizeof(invalidated_wnds) / sizeof(invalidated_wnds[0]));
    invalidated_wnds[invalidated_wnd_count++] = w;
    for (StubWindow *c = w->first_child; c; c = c->next_sibling) {
        InvalidateRect(hwnd_from_wnd(c), NULL, FALSE);
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w) {
        return FALSE;
    }
    if (flags & LWA_ALPHA) {
        w->layered_alpha = alpha;
    }
    return TRUE;
}

WINUSERAPI LRESULT WINAPI SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return send_message(wnd_from_hwnd(hwnd), msg, wp, lp);
}

WINUSERAPI LRESULT WINAPI SendMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return send_message(wnd_from_hwnd(hwnd), msg, wp, lp);
}

WINUSERAPI LRESULT WINAPI DefWindowProcW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (msg == WM_SETFONT) {
        w->font = (HFONT)wp;
        return 0;
    }
    if (msg == WM_GETFONT) {
        return (LRESULT)w->font;
    }
    if (msg == WM_ACTIVATE) {
        if (LOWORD(wp) != WA_INACTIVE) {
            SetFocus(hwnd);
        }
        return 0;
    }
    if (msg == WM_WINDOWPOSCHANGED) {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!(pos->flags & SWP_NOMOVE)) {
            send_message(w, WM_MOVE, 0, MAKELPARAM(w->rect.left, w->rect.top));
        }
        if (!(pos->flags & SWP_NOSIZE)) {
            send_message(w, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w->client_rect.right - w->client_rect.left, w->client_rect.bottom - w->client_rect.top));
        }
    }
    return 0;
}

WINUSERAPI LRESULT WINAPI CallWindowProc(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return proc(hwnd, msg, wp, lp);
}

WINUSERAPI int WINAPI SetScrollInfo(HWND hwnd, int bar, LPCSCROLLINFO si, BOOL redraw) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w || bar != SB_VERT || !si) {
        return 0;
    }
    int old = w->scroll_vert.nPos;
    w->scroll_vert = *si;
    w->scroll_vert.cbSize = sizeof(SCROLLINFO);
    if (redraw) {
        InvalidateRect(hwnd, NULL, FALSE);
    }
    return old;
}

WINUSERAPI BOOL WINAPI GetScrollInfo(HWND hwnd, int bar, LPSCROLLINFO si) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    if (!w || bar != SB_VERT || !si) {
        return FALSE;
    }
    *si = w->scroll_vert;
    return TRUE;
}

WINUSERAPI HWND WINAPI SetCapture(HWND hwnd) {
    HWND old = hwnd_from_wnd(capture_wnd);
    capture_wnd = wnd_from_hwnd(hwnd);
    return old;
}

WINUSERAPI BOOL WINAPI ReleaseCapture(void) {
    capture_wnd = NULL;
    return TRUE;
}

WINUSERAPI HWND WINAPI GetCapture(void) {
    return hwnd_from_wnd(capture_wnd);
}

WINUSERAPI UINT_PTR WINAPI SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc) {
    return id;
}

WINUSERAPI BOOL WINAPI KillTimer(HWND hwnd, UINT_PTR id) {
    return TRUE;
}

WINUSERAPI HWND WINAPI FindWindowExW(HWND, HWND, LPCWSTR, LPCWSTR) {
    return NULL;
}

WINUSERAPI BOOL WINAPI IsWindowVisible(HWND hwnd) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    return is_window_visible(w);
}

WINUSERAPI BOOL WINAPI EnableWindow(HWND hwnd, BOOL enable) {
    StubWindow *w = wnd_from_hwnd(hwnd);
    w->enabled = enable;
    return TRUE;
}

void stubwinapi_update_windows() {
    for (int i = 0; i < invalidated_wnd_count; i++) {
        StubWindow *w = invalidated_wnds[i];
        if (!is_window_visible(w) || (w->style & WS_MINIMIZE)) {
            continue;
        }
        send_message(w, WM_PAINT, 0, 0);
    }
    invalidated_wnd_count = 0;
}
