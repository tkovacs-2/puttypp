
typedef struct StubCursor {
    LPCSTR name;
} StubCursor;

typedef struct StubIcon {
    LPCSTR name;
    bool shared;
} StubIcon;

typedef struct ResourceBitmap {
    LPCSTR name;
    int cx;
    int cy;
} ResourceBitmap;

static StubCursor system_cursors[] = {
    {IDC_ARROW},
    {IDC_IBEAM},
    {IDC_WAIT},
    {IDC_CROSS},
    {IDC_SIZEALL},
    {IDC_SIZENESW},
    {IDC_SIZENWSE}
};

static int resource_icon_count = 0;
static StubIcon resource_icons[32];

static int resource_bitmap_count = 0;
static ResourceBitmap resource_bitmaps[32];

WINUSERAPI HCURSOR WINAPI LoadCursor(HINSTANCE inst, LPCSTR name) {
    for (int i = 0; i < ARRAYSIZE(system_cursors); i++) {
        if (system_cursors[i].name == name) {
            return (HCURSOR)&system_cursors[i];
        }
    }
    return NULL;
}

WINUSERAPI HICON WINAPI LoadIcon(HINSTANCE inst, LPCSTR name) {
    for (int i = 0; i < resource_icon_count; i++) {
        if (resource_icons[i].name == name) {
            return (HICON)&resource_icons[i];
        }
    }
    return NULL;
}

WINUSERAPI HBITMAP WINAPI LoadBitmap(HINSTANCE hinst, LPCSTR name) {
    for (int i = 0; i < resource_bitmap_count; i++) {
        if (resource_bitmaps[i].name == name) {
            return CreateBitmap(resource_bitmaps[i].cx, resource_bitmaps[i].cy, 1, 1, NULL);
        }
    }
    return NULL;
}

WINUSERAPI HANDLE WINAPI LoadImage(HINSTANCE inst, LPCSTR name, UINT type, int cx, int cy, UINT flags) {
    switch (type) {
      case IMAGE_CURSOR:
        assert(!(flags & LR_SHARED));
        return (HANDLE)LoadCursor(inst, name);
      case IMAGE_ICON:
        if (flags & LR_SHARED) {
            return (HANDLE)LoadIcon(inst, name);
        } else {
            for (int i = 0; i < resource_icon_count; i++) {
                if (resource_icons[i].name == name) {
                    StubIcon *icon = calloc(1, sizeof(*icon));
                    icon->name = resource_icons[i].name;
                    return (HICON)icon;
                }
            }
        }
        break;
      case IMAGE_BITMAP:
        for (int i = 0; i < resource_bitmap_count; i++) {
            if (resource_bitmaps[i].name == name) {
                return CreateBitmap(cx, cy, 1, 1, NULL);
            }
        }
        break;
    }
    return NULL;
}

WINUSERAPI BOOL WINAPI DestroyIcon(HICON icon) {
    StubIcon *i = (StubIcon *)icon;
    assert(!i->shared);
    free(i);
    return TRUE;
}

void stub_set_resource_icon(int id) {
    resource_icons[resource_icon_count].name = MAKEINTRESOURCE(id);
    resource_icon_count++;
}

void stub_set_resource_bitmap(int id, int cx, int cy) {
    resource_bitmaps[resource_bitmap_count].name = MAKEINTRESOURCE(id);
    resource_bitmaps[resource_bitmap_count].cx = cx;
    resource_bitmaps[resource_bitmap_count].cy = cy;
    resource_bitmap_count++;
}
