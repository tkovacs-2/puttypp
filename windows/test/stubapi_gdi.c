typedef enum StubGdiType {
    STUB_GDI_FONT,
    STUB_GDI_BRUSH,
    STUB_GDI_PEN,
    STUB_GDI_BITMAP,
    STUB_GDI_PALETTE
} StubGdiType;

typedef struct StubGdiObject {
    StubGdiType type;
    bool stock;
    int selected;
    union {
        BITMAP bitmap;
        LOGFONTW font;
    } u;
} StubGdiObject;

static StubGdiObject *stub_create_gdi_object(StubGdiType type) {
    StubGdiObject *o = calloc(1, sizeof(*o));
    o->type = type;
    o->stock = false;
    return o;
}

static StubGdiObject STUB_STOCK_WHITE_BRUSH = {STUB_GDI_BRUSH, true, 0};
static StubGdiObject STUB_STOCK_GRAY_BRUSH = {STUB_GDI_BRUSH, true, 0};
static StubGdiObject STUB_STOCK_DKGRAY_BRUSH = {STUB_GDI_BRUSH, true, 0};
static StubGdiObject STUB_STOCK_BLACK_PEN = {STUB_GDI_PEN, true, 0};
static StubGdiObject STUB_STOCK_WHITE_PEN = {STUB_GDI_PEN, true, 0};
static StubGdiObject STUB_STOCK_DEFAULT_PALETTE = {STUB_GDI_PALETTE, true, 0};
static StubGdiObject STUB_STOCK_SYSTEM_FONT = {STUB_GDI_FONT, true, 0, {{16, 8}}};
static StubGdiObject STUB_STOCK_BITMAP = {STUB_GDI_BITMAP, true, 0, {{1, 1}}};

typedef struct StubDc {
    HWND hwnd;
    StubGdiObject *selected_font;
    StubGdiObject *selected_brush;
    StubGdiObject *selected_pen;
    StubGdiObject *selected_palette;
    StubGdiObject *selected_bitmap;
} StubDc;

static StubDc *stub_alloc_dc(HWND hwnd) {
    StubDc *dc = calloc(1, sizeof(StubDc));
    dc->hwnd = hwnd;
    dc->selected_font = &STUB_STOCK_SYSTEM_FONT;
    dc->selected_brush = &STUB_STOCK_WHITE_BRUSH;
    dc->selected_pen = &STUB_STOCK_BLACK_PEN;
    dc->selected_palette = &STUB_STOCK_DEFAULT_PALETTE;
    return dc;
}

static void stub_free_dc(HDC hdc) {
    StubDc *dc = (StubDc *)hdc;
    free(dc);
}

WINUSERAPI HDC WINAPI GetDC(HWND hwnd) {
    return (HDC)stub_alloc_dc(hwnd);
}

WINUSERAPI int WINAPI ReleaseDC(HWND, HDC hdc) {
    StubDc *dc = (StubDc *)hdc;
    if (!dc->selected_font->stock) {
        dc->selected_font->selected--;
    }
    if (!dc->selected_brush->stock) {
        dc->selected_brush->selected--;
    }
    if (!dc->selected_pen->stock) {
        dc->selected_pen->selected--;
    }
    if (!dc->selected_palette->stock) {
        dc->selected_palette->selected--;
    }
    assert(dc->selected_bitmap == NULL);
    stub_free_dc(hdc);
    return 1;
}

WINUSERAPI HDC WINAPI BeginPaint(HWND hwnd, PAINTSTRUCT *ps) {
    memset(ps, 0, sizeof(*ps));
    GetClientRect(hwnd, &ps->rcPaint);
    ps->hdc = (HDC)stub_alloc_dc(hwnd);
    return ps->hdc;
}

WINUSERAPI BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *ps) {
    stub_free_dc(ps->hdc);
    return TRUE;
}

WINUSERAPI HDC WINAPI CreateCompatibleDC(HDC) {
    StubDc *dc = stub_alloc_dc(NULL);
    dc->selected_bitmap = &STUB_STOCK_BITMAP;
    return (HDC)dc;
}

WINUSERAPI BOOL WINAPI DeleteDC(HDC hdc) {
    assert(((StubDc *)hdc)->selected_bitmap->stock);
    stub_free_dc(hdc);
    return TRUE;
}

WINUSERAPI HGDIOBJ WINAPI GetStockObject(int obj) {
    switch (obj) {
      case WHITE_BRUSH:
        return &STUB_STOCK_WHITE_BRUSH;
      case GRAY_BRUSH:
        return &STUB_STOCK_GRAY_BRUSH;
      case DKGRAY_BRUSH:
        return &STUB_STOCK_DKGRAY_BRUSH;
      case BLACK_PEN:
        return &STUB_STOCK_BLACK_PEN;
      case WHITE_PEN:
        return &STUB_STOCK_WHITE_PEN;
      case DEFAULT_PALETTE:
        return &STUB_STOCK_DEFAULT_PALETTE;
      default:
        assert(false);
    }
    return NULL;
}

WINUSERAPI HGDIOBJ WINAPI SelectObject(HDC hdc, HGDIOBJ obj) {
    StubDc *dc = (StubDc *)hdc;
    StubGdiObject *new = (StubGdiObject *)obj;
    StubGdiObject *old = NULL;
    switch (new->type) {
      case STUB_GDI_FONT:
        old = dc->selected_font;
        dc->selected_font = new;
        break;
      case STUB_GDI_BRUSH:
        old = dc->selected_brush;
        dc->selected_brush = new;
        break;
      case STUB_GDI_PEN:
        old = dc->selected_pen;
        dc->selected_pen = new;
        break;
      case STUB_GDI_PALETTE:
        old = dc->selected_palette;
        dc->selected_palette = new;
        break;
      case STUB_GDI_BITMAP:
        old = dc->selected_bitmap;
        dc->selected_bitmap = new;
        break;
      default:
        assert(false);
    }
    if (old != new) {
        if (!old->stock) {
            old->selected--;
        }
        if (!new->stock) {
            new->selected++;
        }
    }
    return old;
}

WINUSERAPI BOOL WINAPI DeleteObject(HGDIOBJ obj) {
    StubGdiObject *o = (StubGdiObject *)obj;
    if (o->stock) {
        return TRUE;
    }
    assert(o->selected == 0);
    free(o);
    return TRUE;
}

WINUSERAPI HBRUSH WINAPI CreateSolidBrush(COLORREF color) {
    return (HBRUSH)stub_create_gdi_object(STUB_GDI_BRUSH);
}

WINUSERAPI HFONT WINAPI CreateFontW(int h, int w, int, int, int weight, DWORD, DWORD, DWORD,
                                    DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR face) {
    StubGdiObject *o = stub_create_gdi_object(STUB_GDI_FONT);
    o->u.font.lfHeight = h;
    o->u.font.lfWidth = w;
    o->u.font.lfWeight = weight;
    wcsncpy(o->u.font.lfFaceName, face, LF_FACESIZE - 1);
    return (HFONT)o;
}

WINUSERAPI HFONT WINAPI CreateFontIndirectW(const LOGFONTW *lf) {
    return CreateFontW(lf->lfHeight, lf->lfWidth, lf->lfEscapement, lf->lfOrientation,
        lf->lfWeight, lf->lfItalic, lf->lfUnderline, lf->lfStrikeOut, lf->lfCharSet,
        lf->lfOutPrecision, lf->lfClipPrecision, lf->lfQuality, lf->lfPitchAndFamily,
        lf->lfFaceName);
}

WINUSERAPI HBITMAP WINAPI CreateBitmap(int w, int h, UINT, UINT, const void *) {
    StubGdiObject *o = stub_create_gdi_object(STUB_GDI_BITMAP);
    o->u.bitmap.bmWidth = w;
    o->u.bitmap.bmHeight = h;
    return (HBITMAP)o;
}

WINUSERAPI HBITMAP WINAPI CreateCompatibleBitmap(HDC, int w, int h) {
    return CreateBitmap(w, h, 0, 0, NULL);
}

WINUSERAPI LONG WINAPI SetBitmapBits(HBITMAP bm, DWORD bytes, const void *) {
    StubGdiObject *o = (StubGdiObject *)bm;
    assert(o->type == STUB_GDI_BITMAP);
    return bytes;
}

WINUSERAPI HPALETTE WINAPI CreatePalette(const LOGPALETTE *) {
    return (HPALETTE)stub_create_gdi_object(STUB_GDI_PALETTE);
}

WINUSERAPI UINT WINAPI RealizePalette(HDC) {
    return 0;
}
WINUSERAPI HPALETTE WINAPI SelectPalette(HDC hdc, HPALETTE pal, BOOL) {
    return (HPALETTE)SelectObject(hdc, (HGDIOBJ)pal);
}

WINUSERAPI UINT WINAPI SetPaletteEntries(HPALETTE, UINT, UINT, const PALETTEENTRY *) {
    return 0;
}

WINUSERAPI BOOL WINAPI UnrealizeObject(HGDIOBJ) {
    return TRUE;
}

WINUSERAPI int WINAPI GetDeviceCaps(HDC, int idx) {
    switch (idx) {
      case LOGPIXELSX:
      case LOGPIXELSY:
        return 96;
      case RASTERCAPS:
        return RC_PALETTE;
      default:
        assert(false);
    }
}

WINUSERAPI int WINAPI GetObjectW(HGDIOBJ obj, int sz, void *out) {
    StubGdiObject *o = (StubGdiObject *)obj;
    if (o->type == STUB_GDI_BITMAP && sz == sizeof(BITMAP)) {
        memcpy(out, &o->u.bitmap, sizeof(BITMAP));
        return sz;
    }
    if (o->type == STUB_GDI_FONT && sz == sizeof(LOGFONTW)) {
        memcpy(out, &o->u.font, sizeof(LOGFONTW));
        return sz;
    }
    return 0;
}

WINUSERAPI BOOL WINAPI GetTextMetricsW(HDC hdc, LPTEXTMETRICW tm) {
    StubDc *dc = (StubDc *)hdc;
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = dc->selected_font->u.font.lfHeight;
    tm->tmAveCharWidth = dc->selected_font->u.font.lfWidth;
    tm->tmDescent = 2;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetTextExtentPoint32W(HDC hdc, LPCWSTR, int len, LPSIZE sz) {
    StubDc *dc = (StubDc *)hdc;
    sz->cx = dc->selected_font->u.font.lfWidth * len;
    sz->cy = dc->selected_font->u.font.lfHeight;
    return TRUE;
}

WINUSERAPI COLORREF WINAPI SetBkColor(HDC, COLORREF) {
    return CLR_INVALID;
}

WINUSERAPI COLORREF WINAPI SetTextColor(HDC, COLORREF) {
    return CLR_INVALID;
}

WINUSERAPI int WINAPI SetBkMode(HDC, int) {
    return OPAQUE;
}

WINUSERAPI BOOL WINAPI ExtTextOutW(HDC, int, int, UINT, const RECT *, LPCWSTR, UINT, const INT *) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI TextOutW(HDC, int, int, LPCWSTR, int) {
    return TRUE;
}

WINUSERAPI int WINAPI DrawText(HDC, LPCSTR, int, LPRECT r, UINT) {
    return r->bottom - r->top;
}

WINUSERAPI int WINAPI FillRect(HDC, const RECT *, HBRUSH) {
    return 1;
}

WINUSERAPI int WINAPI FrameRect(HDC, const RECT *, HBRUSH) {
    return 1;
}

WINUSERAPI BOOL WINAPI Rectangle(HDC, int, int, int, int) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI DrawEdge(HDC, LPRECT, UINT, UINT) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) {
    return TRUE;
}

WINUSERAPI int WINAPI SaveDC(HDC) {
     return 1;
}

WINUSERAPI BOOL WINAPI RestoreDC(HDC, int) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) {
    return TRUE;
}

WINUSERAPI BOOL WINAPI UpdateColors(HDC) {
    return TRUE;
}
