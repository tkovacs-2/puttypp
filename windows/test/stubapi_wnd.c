
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
    RECT window_rect;
    RECT window_rect_restored;
    RECT window_rect_prev;
    RECT client_rect;
    INT_PTR ctrl_id;
    HFONT font;
    bool visible;
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

static LRESULT CALLBACK stub_button_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static HWND stub_focus_hwnd;
static HWND stub_capture_hwnd;
static HWND stub_active_hwnd;
static WNDCLASSW stub_registered_classes[8] =
    {{0, stub_button_proc, 0, 0, NULL, NULL, NULL, NULL, NULL, L"BUTTON"}};
static int stub_registered_class_count = 1;

static WINDOWPLACEMENT stub_placement;
static int stub_show_cmd = SW_SHOWNORMAL;

static StubWindow *stub_wnd_from_hwnd(HWND hwnd) {
    return (StubWindow *)hwnd;
}

static HWND stub_hwnd_from_wnd(StubWindow *w) {
    return (HWND)w;
}

static void stub_link_child(StubWindow *parent, StubWindow *child) {
    child->parent = parent;
    child->next_sibling = parent->first_child;
    if (parent->first_child) {
        parent->first_child->prev_sibling = child;
    }
    parent->first_child = child;
    child->z_prev = parent;
}

static void stub_unlink_child(StubWindow *child) {
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

static StubWindow *stub_alloc_window(void) {
    StubWindow *w = calloc(1, sizeof(*w));
    w->enabled = true;
    w->tab_cur_sel = -1;
    w->tab_item_extra = sizeof(TCITEMHEADER);
    w->scroll_vert.cbSize = sizeof(SCROLLINFO);
    w->layered_alpha = 255;
    return w;
}

static const WNDCLASSW *stub_find_class(LPCWSTR name) {
    for (int i = 0; i < stub_registered_class_count; i++) {
        if (wcscmp(stub_registered_classes[i].lpszClassName, name) == 0) {
            return &stub_registered_classes[i];
        }
    }
    return NULL;
}

static void stub_adjust_client_from_window(StubWindow *w) {
    int border = (w->style & (WS_BORDER | WS_THICKFRAME) ? 5 : 0);
    int caption = (w->style & WS_CAPTION) ? 30 : 0;
    w->client_rect.left = w->window_rect.left + border;
    w->client_rect.top = w->window_rect.top + caption + border;
    w->client_rect.right = w->window_rect.right - border;
    w->client_rect.bottom = w->window_rect.bottom - border;
    if (w->client_rect.right < w->client_rect.left) {
        w->client_rect.right = w->client_rect.left;
    }
    if (w->client_rect.bottom < w->client_rect.top) {
        w->client_rect.bottom = w->client_rect.top;
    }
}

static void stub_set_window_rect(StubWindow *w, int x, int y, int width, int height) {
    assert(width >= 0 && height >= 0);
    w->window_rect_prev = w->window_rect;
    w->window_rect.left = x;
    w->window_rect.top = y;
    w->window_rect.right = x + width;
    w->window_rect.bottom = y + height;
    stub_adjust_client_from_window(w);
}

static void stub_tab_ensure(StubWindow *w, int count) {
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

static LRESULT stub_send_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    return w->wndproc(hwnd, msg, wp, lp);
}

static void stub_set_active_window(StubWindow *w) {
    HWND hwnd = stub_hwnd_from_wnd(w);
    if (hwnd == stub_active_hwnd) {
        return;
    }
    if (!w->visible) {
        return;
    }
    if (w->style & WS_CHILD) {
        stub_set_active_window(w->parent);
        return;
    }
    HWND old = stub_active_hwnd;
    if (old) {
        stub_send_message(old, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), (LPARAM)hwnd);
    }
    stub_active_hwnd = hwnd;
    stub_send_message(hwnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), (LPARAM)old);
}

static LRESULT CALLBACK stub_tab_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
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
        stub_tab_ensure(w, w->tab_count + 1);
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

static LRESULT CALLBACK stub_button_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
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
    assert(stub_registered_class_count < (int)(sizeof(stub_registered_classes) / sizeof(stub_registered_classes[0])));
    stub_registered_classes[stub_registered_class_count] = *wc;
    stub_registered_classes[stub_registered_class_count].lpszClassName = wc->lpszClassName;
    stub_registered_class_count++;
    return (ATOM)stub_registered_class_count;
}

static HWND stub_create_window_ex(DWORD exstyle, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    const WNDCLASSW *reg = stub_find_class(cls);
    StubWindow *wnd = stub_alloc_window();

    wnd->wndproc = reg->lpfnWndProc;
    wcsncpy(wnd->class_name, cls ? cls : L"", 63);
    wcsncpy(wnd->window_text, title ? title : L"", 511);

    wnd->style = (LONG)style;
    wnd->exstyle = (LONG)exstyle;
    wnd->ctrl_id = menu ? (INT_PTR)menu : 0;
    wnd->visible = (style & WS_VISIBLE);
    wnd->enabled = true;

    bool is_child = (style & (WS_CHILD)) != 0;
    bool is_overlapped = (style & (WS_POPUP|WS_CHILD)) != 0;
    if (x == CW_USEDEFAULT) {
        x = 0;
    }
    if (y == CW_USEDEFAULT) {
        y = 0;
    }
    if (w == CW_USEDEFAULT) {
        w = (is_overlapped ? stub_monitor_info.screen_width/2 : 0);
    }
    if (h == CW_USEDEFAULT) {
        h = (is_overlapped ? stub_monitor_info.screen_height/2 : 0);
    }
    stub_set_window_rect(wnd, x, y, w, h);
    StubWindow *par = parent ? stub_wnd_from_hwnd(parent) : NULL;
    if (par) {
        stub_link_child(par, wnd);
    }
    HWND hwnd = stub_hwnd_from_wnd(wnd);

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

    if (wnd->visible) {
        WINDOWPOS pos;
        pos.hwnd = hwnd;
        pos.hwndInsertAfter = HWND_TOP;
        pos.x = x;
        pos.y = y;
        pos.cx = w;
        pos.cy = h;
        pos.flags = SWP_SHOWWINDOW;
        stub_send_message(hwnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
        if (!is_child) {
            stub_set_active_window(wnd);
        }
    } else {
        wnd->defer_winposchanged = true;
    }
    return hwnd;
}

WINUSERAPI HWND WINAPI CreateWindowExW(DWORD exstyle, LPCWSTR cls, LPCWSTR title, DWORD style,
    int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    return stub_create_window_ex(exstyle, cls, title, style, x, y, w, h, parent, menu, inst, param);
}

WINUSERAPI BOOL WINAPI DestroyWindow(HWND hwnd) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (stub_focus_hwnd == hwnd) {
        stub_focus_hwnd = NULL;
        stub_send_message(hwnd, WM_KILLFOCUS, 0, 0);
    }
    if (stub_active_hwnd == hwnd) {
        stub_active_hwnd = NULL;
        stub_send_message(hwnd, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    }
    if (stub_capture_hwnd == hwnd) {
        stub_capture_hwnd = NULL;
        stub_send_message(hwnd, WM_CAPTURECHANGED, 0, 0);
    }
    stub_send_message(hwnd, WM_DESTROY, 0, 0);
    while (w->first_child) {
        DestroyWindow(stub_hwnd_from_wnd(w->first_child));
    }
    free(w->tab_items);
    stub_unlink_child(w);
    free(w);
    return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Window geometry, state, focus                                              */
/* -------------------------------------------------------------------------- */

WINUSERAPI BOOL WINAPI GetClientRect(HWND hwnd, LPRECT r) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !r) {
        return FALSE;
    }
    *r = w->client_rect;
    OffsetRect(r, -w->client_rect.left, -w->client_rect.top);
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetWindowRect(HWND hwnd, LPRECT r) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !r) {
        return FALSE;
    }
    *r = w->window_rect;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w) {
        return FALSE;
    }
    if (!(flags & SWP_NOMOVE)) {
        if (x != 0 || y != 0 || !(flags & SWP_NOSIZE)) {
            int width = w->window_rect.right - w->window_rect.left;
            int height = w->window_rect.bottom - w->window_rect.top;
            if (!(flags & SWP_NOSIZE)) {
                width = cx;
                height = cy;
            }
            stub_set_window_rect(w, x, y, width, height);
        }
    } else if (!(flags & SWP_NOSIZE)) {
        stub_set_window_rect(w, w->window_rect.left, w->window_rect.top, cx, cy);
    }
    if (flags & SWP_SHOWWINDOW) {
        w->visible = true;
    }
    if (after == HWND_TOP) {
    } else if (after && after != HWND_TOPMOST && after != HWND_NOTOPMOST) {
    }
    return TRUE;
}

WINUSERAPI LONG_PTR WINAPI SetWindowLongPtr(HWND hwnd, int idx, LONG_PTR val) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w) {
        return 0;
    }
    if (w->is_dialog && idx == DWLP_USER) {
        LONG_PTR old = w->dlg_user;
        w->dlg_user = val;
        return old;
    }
    switch (idx) {
      case GWLP_USERDATA:
        { LONG_PTR old = w->user_data; w->user_data = val; return old; }
      case GWLP_WNDPROC:
        { WNDPROC old = w->wndproc; w->wndproc = (WNDPROC)val; return (LONG_PTR)old; }
      case GWL_STYLE:
        { LONG old = w->style; w->style = (LONG)val; stub_adjust_client_from_window(w); return old; }
      case GWL_EXSTYLE:
        { LONG old = w->exstyle; w->exstyle = (LONG)val; return old; }
    }
    return 0;
}

WINUSERAPI LONG_PTR WINAPI GetWindowLongPtr(HWND hwnd, int idx) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
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
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w) {
        return FALSE;
    }
    wcsncpy(w->window_text, text ? text : L"", 511);
    w->window_text[511] = L'\0';
    return TRUE;
}

WINUSERAPI int WINAPI GetWindowTextW(HWND hwnd, LPWSTR buf, int max) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !buf || max <= 0) {
        return 0;
    }
    wcsncpy(buf, w->window_text, max - 1);
    buf[max - 1] = L'\0';
    return (int)wcslen(buf);
}

WINUSERAPI int WINAPI GetWindowTextLengthW(HWND hwnd) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    return w ? (int)wcslen(w->window_text) : 0;
}

WINUSERAPI HWND WINAPI GetParent(HWND hwnd) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    return w && w->parent ? stub_hwnd_from_wnd(w->parent) : NULL;
}

WINUSERAPI HWND WINAPI GetDlgItem(HWND dlg, int id) {
    StubWindow *d = stub_wnd_from_hwnd(dlg);
    if (!d) {
        return NULL;
    }
    for (StubWindow *c = d->first_child; c; c = c->next_sibling) {
        if (c->ctrl_id == id) {
            return stub_hwnd_from_wnd(c);
        }
    }
    return NULL;
}

WINUSERAPI HWND WINAPI GetWindow(HWND hwnd, UINT cmd) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w) {
        return NULL;
    }
    switch (cmd) {
      case GW_OWNER:
        return stub_hwnd_from_wnd(w->owner);
      case GW_HWNDPREV:
        return w->z_prev ? stub_hwnd_from_wnd(w->z_prev) : NULL;
      case GW_CHILD:
        return w->first_child ? stub_hwnd_from_wnd(w->first_child) : NULL;
    }
    return NULL;
}

WINUSERAPI HWND WINAPI SetFocus(HWND hwnd) {
    if (stub_focus_hwnd == hwnd) {
        return hwnd;
    }
    HWND old = stub_focus_hwnd;
    if (old) {
        stub_send_message(old, WM_KILLFOCUS, 0, 0);
    }
    stub_focus_hwnd = hwnd;
    stub_send_message(hwnd, WM_SETFOCUS, 0, 0);
    return old;
}

WINUSERAPI HWND WINAPI GetFocus(void) {
    return stub_focus_hwnd;
}

WINUSERAPI HWND WINAPI GetActiveWindow(void) {
    return stub_active_hwnd;
}

WINUSERAPI BOOL WINAPI IsChild(HWND parent, HWND child) {
    StubWindow *c = stub_wnd_from_hwnd(child);
    while (c) {
        if (c->parent && stub_hwnd_from_wnd(c->parent) == parent) {
            return TRUE;
        }
        c = c->parent;
    }
    return FALSE;
}

WINUSERAPI BOOL WINAPI ClientToScreen(HWND hwnd, LPPOINT pt) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !pt) {
        return FALSE;
    }
    pt->x += w->window_rect.left;
    pt->y += w->window_rect.top;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ScreenToClient(HWND hwnd, LPPOINT pt) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !pt) {
        return FALSE;
    }
    pt->x -= w->window_rect.left;
    pt->y -= w->window_rect.top;
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
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    bool activate = true;
    bool window_rect_changed = false;
    bool old_visible = w->visible;
    int old_size_state = w->size_state;

    switch (cmd) {
      case SW_HIDE:
        activate = false;
        w->visible = false;
        break;
      case SW_SHOWMINNOACTIVE:
        activate = false;
      case SW_SHOWMINIMIZED:
      case SW_MINIMIZE:
        w->size_state = SIZE_MINIMIZED;
        w->visible = false;
        break;
      case SW_MAXIMIZE:
        w->size_state = SIZE_MAXIMIZED;
        w->visible = true;
        break;
      case SW_SHOWNA:
        activate = false;
      case SW_SHOW:
        w->visible = true;
        break;
      case SW_SHOWNOACTIVATE:
        activate = false;
      case SW_SHOWNORMAL:
        w->size_state = SIZE_RESTORED;
        w->visible = true;
        break;
      default:
        assert(false);
    }
    if (old_size_state == SIZE_RESTORED && w->size_state != SIZE_MAXIMIZED) {
        w->window_rect_restored = w->window_rect;
        stub_set_window_rect(w, 0, 0, stub_monitor_info.screen_width, stub_monitor_info.screen_height);
    }
    if (old_size_state == SIZE_MAXIMIZED && w->size_state != SIZE_RESTORED) {
        stub_set_window_rect(w, w->window_rect_restored.left, w->window_rect_restored.top, w->window_rect_restored.right - w->window_rect_restored.left, w->window_rect_restored.bottom - w->window_rect_restored.top);
    }
    if (w->defer_winposchanged || window_rect_changed) {
        WINDOWPOS pos;
        pos.hwnd = hwnd;
        pos.hwndInsertAfter = NULL;
        pos.x = w->window_rect.left;
        pos.y = w->window_rect.top;
        pos.cx = w->window_rect.right - w->window_rect.left;
        pos.cy = w->window_rect.bottom - w->window_rect.top;
        pos.flags = SWP_NOZORDER;
        stub_send_message(hwnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&pos);
    }
    if (activate) {
        stub_set_active_window(w);
    }
    w->defer_winposchanged = false;
    return old_visible ? FALSE : TRUE;
}

WINUSERAPI BOOL WINAPI IsIconic(HWND hwnd) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    return w->size_state == SIZE_MINIMIZED;
}

WINUSERAPI BOOL WINAPI InvalidateRect(HWND hwnd, const RECT *r, BOOL erase) {
    (void)r;
    (void)erase;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *wp) {
    if (!wp) {
        return FALSE;
    }
    stub_placement = *wp;
    stub_show_cmd = wp->showCmd;
    return SetWindowPos(hwnd, NULL, wp->rcNormalPosition.left, wp->rcNormalPosition.top,
        wp->rcNormalPosition.right - wp->rcNormalPosition.left,
        wp->rcNormalPosition.bottom - wp->rcNormalPosition.top, SWP_NOZORDER);
}

WINUSERAPI BOOL WINAPI GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *wp) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || !wp) {
        return FALSE;
    }
    *wp = stub_placement;
    wp->rcNormalPosition = w->window_rect;
    wp->showCmd = stub_show_cmd;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w) {
        return FALSE;
    }
    if (flags & LWA_ALPHA) {
        w->layered_alpha = alpha;
    }
    return TRUE;
}

WINUSERAPI LRESULT WINAPI SendMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return stub_send_message(hwnd, msg, wp, lp);
}

WINUSERAPI LRESULT WINAPI SendMessageW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return stub_send_message(hwnd, msg, wp, lp);
}

WINUSERAPI LRESULT WINAPI DefWindowProcW(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
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
        if (!(pos->flags & SWP_NOMOVE) &&
            (w->window_rect_prev.left != w->window_rect.left ||
            w->window_rect_prev.top != w->window_rect.top)) {
            stub_send_message(hwnd, WM_MOVE, 0, MAKELPARAM(w->window_rect.left, w->window_rect.top));
        }
        if (!(pos->flags & SWP_NOSIZE) &&
            (w->window_rect_prev.right - w->window_rect_prev.left != w->window_rect.right - w->window_rect.left ||
            w->window_rect_prev.bottom - w->window_rect_prev.top != w->window_rect.bottom - w->window_rect.top)) {
            stub_send_message(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w->window_rect.right - w->window_rect.left, w->window_rect.bottom - w->window_rect.top));
        }
    }
    return 0;
}

WINUSERAPI LRESULT WINAPI CallWindowProc(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return proc(hwnd, msg, wp, lp);
}

WINUSERAPI int WINAPI SetScrollInfo(HWND hwnd, int bar, LPCSCROLLINFO si, BOOL redraw) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
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
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    if (!w || bar != SB_VERT || !si) {
        return FALSE;
    }
    *si = w->scroll_vert;
    return TRUE;
}

WINUSERAPI HWND WINAPI SetCapture(HWND hwnd) {
    HWND old = stub_capture_hwnd;
    stub_capture_hwnd = hwnd;
    return old;
}

WINUSERAPI BOOL WINAPI ReleaseCapture(void) {
    stub_capture_hwnd = NULL;
    return TRUE;
}

WINUSERAPI HWND WINAPI GetCapture(void) {
    return stub_capture_hwnd;
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
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    return w->visible;
}

WINUSERAPI BOOL WINAPI EnableWindow(HWND hwnd, BOOL enable) {
    StubWindow *w = stub_wnd_from_hwnd(hwnd);
    w->enabled = enable;
    return TRUE;
}
