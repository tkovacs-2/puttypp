
typedef struct StubMenu {
    struct StubMenuItem {
        UINT flags;
        UINT_PTR id;
        struct StubMenu *submenu;
    } *items;
    int item_count;
    int item_capacity;
} StubMenu;

WINUSERAPI HMENU WINAPI CreatePopupMenu(void) {
    StubMenu *m = calloc(1, sizeof(*m));
    return (HMENU)m;
}

WINUSERAPI BOOL WINAPI AppendMenu(HMENU menu, UINT flags, UINT_PTR id, LPCSTR text) {
    StubMenu *m = (StubMenu *)menu;
    if (m->item_count >= m->item_capacity) {
        m->item_capacity = m->item_capacity ? m->item_capacity * 2 : 8;
        m->items = realloc(m->items, m->item_capacity * sizeof(*m->items));
    }
    m->items[m->item_count].flags = flags;
    m->items[m->item_count].id = id;
    m->item_count++;
    return TRUE;
}

WINUSERAPI BOOL WINAPI InsertMenu(HMENU menu, UINT pos, UINT flags, UINT_PTR id, LPCSTR text) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI DeleteMenu(HMENU menu, UINT pos, UINT flags) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI DestroyMenu(HMENU menu) {
    StubMenu *m = (StubMenu *)menu;
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].submenu) {
            DestroyMenu((HMENU)m->items[i].submenu);
        }
    }
    free(m->items);
    free(m);
    return TRUE;
}

WINUSERAPI DWORD WINAPI CheckMenuItem(HMENU menu, UINT id, UINT check) {
    StubMenu *m = (StubMenu *)menu;
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].id == id) {
            if (check & MF_CHECKED) {
                m->items[i].flags |= MF_CHECKED;
            } else {
                m->items[i].flags &= ~MF_CHECKED;
            }
            return m->items[i].flags;
        }
    }
    return (DWORD)-1;
}

WINUSERAPI int WINAPI GetMenuItemCount(HMENU menu) {
    StubMenu *m = (StubMenu *)menu;
    return m ? m->item_count : 0;
}

WINUSERAPI UINT WINAPI GetMenuItemID(HMENU menu, int pos) {
    StubMenu *m = (StubMenu *)menu;
    assert(pos >= 0 && pos < m->item_count);
    return (UINT)m->items[pos].id;
}

WINUSERAPI BOOL WINAPI RemoveMenu(HMENU menu, UINT pos, UINT flags) {
    StubMenu *m = (StubMenu *)menu;
    int index = (flags & MF_BYPOSITION) ? (int)pos : -1;
    if (index < 0) {
        for (int i = 0; i < m->item_count; i++) {
            if (m->items[i].id == pos) {
                index = i;
                break;
            }
        }
    }
    if (index < 0 || index >= m->item_count) {
        return FALSE;
    }
    memmove(&m->items[index], &m->items[index + 1], (m->item_count - index - 1) * sizeof(*m->items));
    m->item_count--;
    return TRUE;
}

WINUSERAPI BOOL WINAPI TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT *) {
    return TRUE;
}
