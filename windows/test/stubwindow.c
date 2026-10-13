
typedef struct FindMatchMask {
    void *cells;
    bool dirty;
} FindMatchMask;

static void find_match_mask_alloc(FindMatchMask *, int, int) {
}

static void find_match_mask_free(FindMatchMask *) {
}

static void find_match_mask_clear(FindMatchMask *) {
}

static void find_display(Terminal *, const wchar_t *, int, bool, bool, FindMatchMask *) {
}

static bool find_above_display(Terminal *, const wchar_t *, int, bool, bool, int *) {
    return false;
}

static bool find_below_display(Terminal *, const wchar_t *, int, bool, bool, int *) {
    return false;
}

#define OSC4_NCOLOURS 262

/* OSC 4 palette indices: 0-15 ANSI, 16-255 xterm cube/greys, 256-261 PuTTY. */
#define OSC4_COLOUR_black         0
#define OSC4_COLOUR_red           1
#define OSC4_COLOUR_green         2
#define OSC4_COLOUR_yellow        3
#define OSC4_COLOUR_blue          4
#define OSC4_COLOUR_magenta       5
#define OSC4_COLOUR_cyan          6
#define OSC4_COLOUR_white         7
#define OSC4_COLOUR_black_bold    8
#define OSC4_COLOUR_red_bold      9
#define OSC4_COLOUR_green_bold    10
#define OSC4_COLOUR_yellow_bold   11
#define OSC4_COLOUR_blue_bold     12
#define OSC4_COLOUR_magenta_bold  13
#define OSC4_COLOUR_cyan_bold     14
#define OSC4_COLOUR_white_bold    15
#define OSC4_COLOUR_fg            256
#define OSC4_COLOUR_fg_bold       257
#define OSC4_COLOUR_bg            258
#define OSC4_COLOUR_bg_bold       259
#define OSC4_COLOUR_cursor_fg     260
#define OSC4_COLOUR_cursor_bg     261

#define CONF_COLORS_DEFAULT_PUTTY { \
    {187, 187, 187}, \
    {255, 255, 255}, \
    {0, 0, 0}, \
    {85, 85, 85}, \
    {0, 0, 0}, \
    {0, 255, 0}, \
    {0, 0, 0}, \
    {85, 85, 85}, \
    {187, 0, 0}, \
    {255, 85, 85}, \
    {0, 187, 0}, \
    {85, 255, 85}, \
    {187, 187, 0}, \
    {255, 255, 85}, \
    {0, 0, 187}, \
    {85, 85, 255}, \
    {187, 0, 187}, \
    {255, 85, 255}, \
    {0, 187, 187}, \
    {85, 255, 255}, \
    {187, 187, 187}, \
    {255, 255, 255} \
}

#define CONF_COLORS_XTERM_BW { \
    {0, 0, 0}, \
    {0, 0, 0}, \
    {255, 255, 255}, \
    {229, 229, 229}, \
    {255, 255, 255}, \
    {0, 0, 0}, \
    {0, 0, 0}, \
    {127, 127, 127}, \
    {205, 0, 0}, \
    {255, 0, 0}, \
    {0, 205, 0}, \
    {0, 255, 0}, \
    {205, 205, 0}, \
    {255, 255, 0}, \
    {0, 0, 238}, \
    {92, 92, 255}, \
    {205, 0, 205}, \
    {255, 0, 255}, \
    {0, 205, 205}, \
    {0, 255, 255}, \
    {229, 229, 229}, \
    {255, 255, 255} \
}

/* Dark blue bg, reddish fg; ANSI 0-7 = DEFAULT_PUTTY normals +120 on R; bolds unchanged. */
#define CONF_COLORS_RED_ON_BLUE { \
    {220, 90, 90}, \
    {255, 140, 140}, \
    {0, 0, 100}, \
    {40, 40, 140}, \
    {0, 0, 0}, \
    {255, 140, 0}, \
    {120, 0, 0}, \
    {85, 85, 85}, \
    {255, 0, 0}, \
    {255, 85, 85}, \
    {120, 187, 0}, \
    {85, 255, 85}, \
    {255, 187, 0}, \
    {255, 255, 85}, \
    {120, 0, 187}, \
    {85, 85, 255}, \
    {255, 0, 187}, \
    {255, 85, 255}, \
    {120, 187, 187}, \
    {85, 255, 255}, \
    {255, 187, 187}, \
    {255, 255, 255} \
}

static const int conf_colour_to_osc4[CONF_NCOLOURS] = {
    OSC4_COLOUR_fg, OSC4_COLOUR_fg_bold,
    OSC4_COLOUR_bg, OSC4_COLOUR_bg_bold,
    OSC4_COLOUR_cursor_fg, OSC4_COLOUR_cursor_bg,
    OSC4_COLOUR_black, OSC4_COLOUR_black_bold,
    OSC4_COLOUR_red, OSC4_COLOUR_red_bold,
    OSC4_COLOUR_green, OSC4_COLOUR_green_bold,
    OSC4_COLOUR_yellow, OSC4_COLOUR_yellow_bold,
    OSC4_COLOUR_blue, OSC4_COLOUR_blue_bold,
    OSC4_COLOUR_magenta, OSC4_COLOUR_magenta_bold,
    OSC4_COLOUR_cyan, OSC4_COLOUR_cyan_bold,
    OSC4_COLOUR_white, OSC4_COLOUR_white_bold,
};

static WinGuiSession *term_remote_wgs = NULL;
static TermRemoteResult term_remote_result;
static bool visualize_caret = false;

#include "win-gui-seat.h"
#include "putty-rc.h"

static void set_palette_entry(WinGuiSession *wgs, unsigned index,
                              unsigned char r, unsigned char g, unsigned char b) {
    wgs->colours[index] = RGB(r, g, b) ^ wgs->colorref_modifier;
    PALETTEENTRY *pe = &wgs->logpal->palPalEntry[index];
    pe->peRed = r;
    pe->peGreen = g;
    pe->peBlue = b;
    pe->peFlags = PC_NOCOLLAPSE;
}

static HDC make_hdc(WinGuiSession *wgs);
static void free_hdc(WinGuiSession *wgs, HDC hdc);

static void set_palette(WinGuiSession *wgs) {
    for (unsigned i = 0; i < 216; i++) {
        int r = i / 36, g = (i / 6) % 6, b = i % 6;
        unsigned char rr = r ? (unsigned char)(r * 40 + 55) : 0;
        unsigned char gg = g ? (unsigned char)(g * 40 + 55) : 0;
        unsigned char bb = b ? (unsigned char)(b * 40 + 55) : 0;
        set_palette_entry(wgs, i + 16, rr, gg, bb);
    }
    for (unsigned i = 0; i < 24; i++) {
        unsigned char shade = (unsigned char)(i * 10 + 8);
        set_palette_entry(wgs, i + 232, shade, shade, shade);
    }
    for (int i = 0; i < CONF_NCOLOURS; i++) {
        unsigned char r = conf_get_int_int(wgs->conf, CONF_colours, i*3+0);
        unsigned char g = conf_get_int_int(wgs->conf, CONF_colours, i*3+1);
        unsigned char b = conf_get_int_int(wgs->conf, CONF_colours, i*3+2);
        set_palette_entry(wgs, conf_colour_to_osc4[i], r, g, b);
    }

    bool got_new_palette = false;
    bool is_active = (wgs == pane_get_active_session(wgs->pane));

    if (!wgs->tried_pal && conf_get_bool(wgs->conf, CONF_try_palette)) {
        HDC hdc = GetDC(wgs->term_hwnd);
        if (GetDeviceCaps(hdc, RASTERCAPS) & RC_PALETTE) {
            wgs->pal = CreatePalette(wgs->logpal);
            if (wgs->pal) {
                if (is_active) {
                    SelectPalette(hdc, wgs->pal, !wgs->term->has_focus);
                    RealizePalette(hdc);
                    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);
                }
                wgs->colorref_modifier = PALETTERGB(0, 0, 0) ^ RGB(0, 0, 0);
                for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
                    wgs->colours[i] ^= wgs->colorref_modifier;
                got_new_palette = true;
            }
        }
        ReleaseDC(wgs->term_hwnd, hdc);
        wgs->tried_pal = true;
    }
    if (wgs->pal && !got_new_palette) {
        SetPaletteEntries(wgs->pal, 0, wgs->logpal->palNumEntries, wgs->logpal->palPalEntry);
        if (is_active) {
            HDC hdc = make_hdc(wgs);
            UnrealizeObject(wgs->pal);
            RealizePalette(hdc);
            free_hdc(wgs, hdc);
        }
    }
}

static void paint_term_glyph(HDC hdc, WinGuiSession *wgs, int term_left, int term_top,
    int col, int row, wchar_t ch, int fg_index, int bg_index) {
    RECT cell;
    cell.left = term_left + col * wgs->font_width;
    cell.top = term_top + row * wgs->font_height;
    cell.right = cell.left + wgs->font_width;
    cell.bottom = cell.top + wgs->font_height;

    SetBkColor(hdc, wgs->colours[bg_index]);
    SetTextColor(hdc, wgs->colours[fg_index]);
    SetBkMode(hdc, TRANSPARENT);
    ExtTextOutW(hdc, cell.left, cell.top, ETO_OPAQUE | ETO_CLIPPED, &cell, &ch, 1, NULL);
}

static void paint_term_color_names(HDC hdc, WinGuiSession *wgs, int term_left, int term_top) {
    static const wchar_t *const names[] = {
        L"Black", L"Red", L"Green", L"Yellow", L"Blue", L"Magenta", L"Cyan", L"White",
    };
    static const wchar_t bold[] = L"Bold";
    const int name_cols = 7;
    const int bold_cols = 4;
    const int start_col = 1;
    const int start_row = 1;
    int max_cols = wgs->term->cols - 2;
    int max_rows = wgs->term->rows - 2;

    if (max_cols <= 0 || max_rows <= 0) {
        return;
    }

    HGDIOBJ old_font = SelectObject(hdc, wgs->fonts[0]);

    for (int i = 0; i < 8 && i < max_rows; i++) {
        const wchar_t *name = names[i];
        size_t name_len = wcslen(name);
        int fg = OSC4_COLOUR_black + i;
        int fg_bold = fg + (OSC4_COLOUR_black_bold - OSC4_COLOUR_black);
        int row = start_row + i;
        int col = start_col;

        int name_limit = min(name_cols, max_cols);
        for (int p = 0; p < name_limit; p++, col++) {
            wchar_t ch = (p < (int)name_len) ? name[p] : L' ';
            paint_term_glyph(hdc, wgs, term_left, term_top, col, row, ch, fg, OSC4_COLOUR_bg);
        }

        if (max_cols <= name_cols) {
            continue;
        }

        int bold_limit = min(bold_cols, max_cols - name_cols);
        for (int p = 0; p < bold_limit; p++, col++) {
            paint_term_glyph(hdc, wgs, term_left, term_top, col, row, bold[p], fg_bold, OSC4_COLOUR_bg_bold);
        }
    }

    SelectObject(hdc, old_font);
}

static bool is_wgs_valid(WinGuiSession *wgs, Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        int count = pane_get_session_count(pane);
        for (int i = 0; i < count; i++) {
            if (wgs == pane_get_session(pane, i)) {
                return true;
            }
        }
    } else {
        if (is_wgs_valid(wgs, split_get_first(split)) || is_wgs_valid(wgs, split_get_second(split))) {
            return true;
        }
    }
    return false;
}

void term_remote_action(WinGuiSession *wgs, TermRemoteResult *result) {
    switch (result->action) {
      case TERM_REMOTE_RESIZE:
        request_term_resize(wgs, result->param.resize.width, result->param.resize.height);
        break;
      case TERM_REMOTE_MAXIMISE:
        set_term_maximised(wgs, true);
        break;
      case TERM_REMOTE_RESTORE:
        set_term_maximised(wgs, false);
        break;
      case TERM_REMOTE_MINIMISE:
        set_term_minimised(wgs, true);
        break;
      case TERM_REMOTE_MOVE:
        move_term(wgs, result->param.move.x, result->param.move.y);
        break;
      case TERM_REMOTE_Z_TOP:
        set_term_zorder(wgs, true);
        break;
      case TERM_REMOTE_Z_BOTTOM:
        set_term_zorder(wgs, false);
        break;
    }
}

static void update_caret_bitmap(Split *split) {
    Pane *pane = split_get_pane(split);
    if (pane) {
        int count = pane_get_session_count(pane);
        for (int i = 0; i < count; i++) {
            WinGuiSession *wgs = pane_get_session(pane, i);
            int size = (wgs->font_width + 15) / 16 * 2 * wgs->font_height;
            char *bits = snewn(size, char);
            memset(bits, (visualize_caret ? 0x55 : 0), size);
            SetBitmapBits(wgs->caretbm, size, bits);
            sfree(bits);
        }
    } else {
        update_caret_bitmap(split_get_first(split));
        update_caret_bitmap(split_get_second(split));
    }
}

static const char *const appname = "PuTTY++";
static struct PopupMenu popup_menus[1] = {NULL};

static UINT last_mousemove = 0;
static LPARAM last_wm_mousemove_lParam;
static LPARAM last_wm_ncmousemove_lParam;

#define IDM_FULLSCREEN  0x0180
#define IDM_ALWAYSONTOP  0x0190
#define IDM_VISUALIZECARET  0x01A0
#define IDM_RESTART 0x01B0
#define IDM_SPECIALSEP 0x01D0
#define IDM_SHOWLOG 0x01E0
#define IDM_DUPSESS_SFTP 0x01F0
#define IDM_DUPSESS 0x0180
#define IDM_DUPSESS_NEW 0x0180

#define NOISE_SOURCE_MOUSEPOS 0
#define DEFAULT_CODEPAGE 0
#define WM_NETEVENT WM_APP + 1
#define WM_DONE_WITH_SOCKET WM_APP + 2
#define WM_TAB_CYCLE WM_APP + 6
#define WM_TAB_MENU WM_APP + 7
#define LGTYP_NONE 0
#define TIMER_TERM_REMOTE 1

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static struct {} windows_termwin_vt;
static struct {} win_seat_vt;
static struct {} win_gui_logpolicy_vt;

static FindMatchMask find_match_mask;

#define DECL_WINDOWS_FUNCTION(linkage, rettype, name, params)   \
    typedef rettype (WINAPI *t_##name) params;                  \
    linkage t_##name p_##name

#define GET_WINDOWS_FUNCTION_NO_TYPECHECK(module, name) \
    (p_##name = module ?                                \
    (t_##name) GetProcAddress(module, #name) : NULL)

enum MONITOR_DPI_TYPE { MDT_EFFECTIVE_DPI, MDT_ANGULAR_DPI, MDT_RAW_DPI, MDT_DEFAULT };
DECL_WINDOWS_FUNCTION(static, BOOL, GetMonitorInfoA, (HMONITOR, LPMONITORINFO));
DECL_WINDOWS_FUNCTION(static, HMONITOR, MonitorFromWindow, (HWND, DWORD));
DECL_WINDOWS_FUNCTION(static, HRESULT, GetDpiForMonitor, (HMONITOR hmonitor, enum MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY));
DECL_WINDOWS_FUNCTION(static, BOOL, SystemParametersInfoForDpi, (UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi));

static void init_winfuncs() {
    HMODULE user32_module = GetModuleHandle("user32.dll");
    HMODULE shcore_module = GetModuleHandle("shcore.dll");
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetMonitorInfoA);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, MonitorFromWindow);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(shcore_module, GetDpiForMonitor);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, SystemParametersInfoForDpi);
}

void init_window_dpi_info(HWND hwnd, POINT *dpi_info) {
    if (dpi_info->x == 0 || dpi_info->y == 0) {
        if (p_GetDpiForMonitor && p_MonitorFromWindow) {
            UINT dpiX, dpiY;
            HMONITOR currentMonitor = p_MonitorFromWindow(
                hwnd, MONITOR_DEFAULTTOPRIMARY);
            if (p_GetDpiForMonitor(currentMonitor, MDT_EFFECTIVE_DPI,
                &dpiX, &dpiY) == S_OK) {
                dpi_info->x = (int)dpiX;
                dpi_info->y = (int)dpiY;
            }
        }

        /* Fall back to system DPI */
        if (dpi_info->x == 0 || dpi_info->y == 0) {
            HDC hdc = GetDC(hwnd);
            dpi_info->x = GetDeviceCaps(hdc, LOGPIXELSX);
            dpi_info->y = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(hwnd, hdc);
        }
    }
}

static bool is_alt_pressed(void) {
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
        return false;
    if (keystate[VK_MENU] & 0x80)
        return true;
    if (keystate[VK_RMENU] & 0x80)
        return true;
    return false;
}

static void init_fonts(WinGuiSession *wgf, int width, int height) {
    assert((width == 0 && height == 0) || (width > 0 && height > 0));
    if (height == 0) {
        height = -MulDiv(wgf->conf->fontspec.height, dpi_info.y, 72);
        width = 0;
    }
    wgf->fonts[0] = CreateFontW(height, width, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
        L"Lucida Console");

    if (width == 0) {
        HDC hdc = GetDC(frame_hwnd);
        HGDIOBJ oldfont = SelectObject(hdc, wgf->fonts[0]);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        SelectObject(hdc, oldfont);
        ReleaseDC(NULL, hdc);
        wgf->font_width = tm.tmAveCharWidth;
        wgf->font_height = tm.tmHeight;
        wgf->font_dpi = dpi_info.y;
    } else {
        wgf->font_width = width;
        wgf->font_height = height;
        wgf->font_dpi = 0;
    }
}

static void deinit_fonts(WinGuiSession *wgf) {
    DeleteObject(wgf->fonts[0]);
    wgf->fonts[0] = NULL;
    wgf->font_width = 0;
    wgf->font_height = 0;
}

static bool get_monitor_info(MONITORINFO *mi) {
    if (p_GetMonitorInfoA && p_MonitorFromWindow) {
        HMONITOR mon;
        mon = p_MonitorFromWindow(frame_hwnd, MONITOR_DEFAULTTONEAREST);
        mi->cbSize = sizeof(*mi);
        p_GetMonitorInfoA(mon, mi);
        return true;
    }
    return false;
}

static bool get_fullscreen_rect(RECT * ss) {
    MONITORINFO mi;
    if (get_monitor_info(&mi)) {
        /* structure copy */
        *ss = mi.rcMonitor;
        return true;
    }
    return GetClientRect(GetDesktopWindow(), ss);
}

static bool get_workingarea_rect(RECT *ss) {
    MONITORINFO mi;
    if (get_monitor_info(&mi)) {
        /* structure copy */
        *ss = mi.rcWork;
        return true;
    }
    return GetClientRect(GetDesktopWindow(), ss);
}

static void check_menu_item(UINT item, bool check) {
    CheckMenuItem(popup_menus[0].menu, item, check ? MF_CHECKED : MF_UNCHECKED);
}

static void sys_cursor_update(WinGuiSession *wgs) {
    if (!wgs->term->has_focus || wgs->caret_x < 0 || wgs->caret_y < 0) {
        return;
    }
    SetCaretPos(wgs->caret_x, wgs->caret_y);
}

static void win_set_cursor_pos(TermWin *win, int x, int y) {
    WinGuiSession *wgs = ((WinGuiSession *)(((char *)(win)) - offsetof(WinGuiSession, termwin)));

    if (!wgs->term->has_focus) {
        return;
    }
    int cx = x * wgs->font_width + wgs->offset_width;
    int cy = y * wgs->font_height + wgs->offset_height;
    if (cx == wgs->caret_x && cy == wgs->caret_y) {
        return;
    }
    wgs->caret_x = cx;
    wgs->caret_y = cy;
    sys_cursor_update(wgs);
}

static void do_defaults(const char *session, Conf *conf) {
    static const char *const saved_sessions[] = {
        "resize_term",
        "resize_font",
        "disabled",
        "resize_either",
    };
    static const Conf saved_session_confs[] = {
        {RESIZE_TERM, 80, 24, false, false, {12, ""}, 1, false, false, false, PROT_TELNET, "a", CONF_COLORS_DEFAULT_PUTTY},
        {RESIZE_FONT, 80, 24, true, false, {12, ""}, 1, false, false, false, PROT_SSH, "b", CONF_COLORS_DEFAULT_PUTTY},
        {RESIZE_DISABLED, 80, 24, true, true, {12, ""}, 1, false, false, false, PROT_TELNET, "c", CONF_COLORS_XTERM_BW},
        {RESIZE_EITHER, 80, 24, true, true, {12, ""}, 1, false, false, false, PROT_TELNET, "d", CONF_COLORS_RED_ON_BLUE}
    };
    static const Conf default_conf = {RESIZE_TERM, 80, 24, false, false, {12, ""}, 1, false, false, false, PROT_SSH, "", CONF_COLORS_DEFAULT_PUTTY};

    if (session) {
        for (int i = 0; i < sizeof(saved_sessions) / sizeof(saved_sessions[0]); i++) {
            if (strcmp(session, saved_sessions[i]) == 0) {
                *conf = saved_session_confs[i];
                return;
            }
        }
    }
    *conf = default_conf;
}

bool do_config_pp(HWND hwnd, Conf *conf, const char **session_name) {
    assert(session_name && *session_name == NULL);
    ConfDlgParam param = {conf, session_name, false};
    return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_RECONF), hwnd, ConfDlgProc, (LPARAM)&param) == true;
}

bool do_reconfig_pp(HWND hwnd, Conf *conf, const char **session_name, int protcfginfo) {
    assert(session_name && *session_name);
    ConfDlgParam param = {conf, session_name, true};
    return DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_RECONF), hwnd, ConfDlgProc, (LPARAM)&param) == true;
}

static void init_palette(WinGuiSession *wgs) {
    wgs->pal = NULL;
    wgs->tried_pal = false;
    wgs->logpal = (LOGPALETTE *)malloc(
        sizeof(LOGPALETTE) + (OSC4_NCOLOURS - 1) * sizeof(PALETTEENTRY));
    wgs->logpal->palVersion = 0x300;
    wgs->logpal->palNumEntries = OSC4_NCOLOURS;
    for (unsigned i = 0; i < OSC4_NCOLOURS; i++) {
        PALETTEENTRY *out = &wgs->logpal->palPalEntry[i];
        out->peFlags = PC_NOCOLLAPSE;
        out->peRed = 0;
        out->peGreen = 0;
        out->peBlue = 0;
    }
}

static HDC make_hdc(WinGuiSession *wgs) {
    assert(wgs->term_hwnd);
    HDC hdc = GetDC(wgs->term_hwnd);
    if (wgs->pal) {
        SelectPalette(hdc, wgs->pal, !wgs->term->has_focus);
    }
    return hdc;
}

static void free_hdc(WinGuiSession *wgs, HDC hdc) {
    assert(wgs->term_hwnd);
    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);
    ReleaseDC(wgs->term_hwnd, hdc);
}

static void show_mouseptr(WinGuiSession *wgs, bool show) {
    static bool cursor_visible = true;
    if (!conf_get_bool(wgs->conf, CONF_hide_mouseptr))
    show = true;
    wgs->cursor_visible = show;
    if (cursor_visible && !show)
    ShowCursor(false);
    else if (!cursor_visible && show)
    ShowCursor(true);
    cursor_visible = show;
}

static void update_mouse_pointer(WinGuiSession *wgs) {
    show_mouseptr(wgs, wgs->cursor_visible);
}

void refresh_find_match_mask(WinGuiSession *wgs) {
}

static void winselgui_response(UINT, WPARAM, LPARAM) {
}

static void set_input_locale(HKL) {
}

static void conf_cache_data(WinGuiSession *) {
}

static void setup_clipboards(Terminal *, Conf *) {
}

static LogContext *log_init(LogPolicy *, Conf *) {
    return NULL;
}

static void log_free(LogContext *) {
}

static void random_save_seed(void) {
}

static void start_backend(WinGuiSession *) {
}

static void stop_backend(WinGuiSession *) {
}

static void delete_callbacks_for_context(void *) {
}

static void noise_ultralight(int, LPARAM) {
}

void reseteventlog(eventlog_stuff *) {
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
      case WM_CREATE:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA *)lParam)->lpCreateParams);
        return 0;
      case WM_SIZE: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        WinGuiSession *wgs = pane_get_active_session(pane);
        handle_term_hwnd_size(wgs, wParam);
        return 0;
      }
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc;
        RECT rect;

        HideCaret(hwnd);
        hdc = BeginPaint(hwnd, &ps);

        GetClientRect(hwnd, &rect);
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        WinGuiSession *wgs = pane_get_active_session(pane);
        int term_left = rect.left + wgs->offset_width;
        int term_top = rect.top + wgs->offset_height;
        int term_width = wgs->font_width * wgs->term->cols;
        int term_height = wgs->font_height * wgs->term->rows;
        int conf_cols = conf_get_int(wgs->conf, CONF_width);
        int conf_rows = conf_get_int(wgs->conf, CONF_height);
        int conf_width = wgs->font_width * conf_cols;
        int conf_height = wgs->font_height * conf_rows;
        bool focus = GetFocus() == hwnd;

        HBRUSH bg_brush = CreateSolidBrush(wgs->colours[OSC4_COLOUR_bg]);
        FillRect(hdc, &rect, bg_brush);
        DeleteObject(bg_brush);

        HBRUSH conf_brush = CreateSolidBrush(wgs->colours[focus ? OSC4_COLOUR_blue_bold
            : OSC4_COLOUR_blue]);
        if ((conf_cols == wgs->term->cols && conf_rows > wgs->term->rows) || (conf_cols != wgs->term->cols)) {
            RECT conf_right_rect = {
                term_left + conf_width - wgs->font_width,
                term_top + (conf_cols == wgs->term->cols ? term_height : 0),
                term_left + conf_width,
                term_top + conf_height
            };
            FrameRect(hdc, &conf_right_rect, conf_brush);
        }
        if ((conf_rows == wgs->term->rows && conf_cols > wgs->term->cols) || (conf_rows != wgs->term->rows)) {
            RECT conf_bottom_rect = {
                term_left + (conf_rows == wgs->term->rows ? term_width : 0),
                term_top + conf_height - wgs->font_height,
                term_left + conf_width,
                term_top + conf_height
            };
            FrameRect(hdc, &conf_bottom_rect, conf_brush);
        }
        if (conf_cols > wgs->term->cols) {
            RECT conf_top_rect = {
                term_left + term_width,
                term_top,
                term_left + conf_width,
                term_top + wgs->font_height
            };
            FrameRect(hdc, &conf_top_rect, conf_brush);
        }
        if (conf_rows > wgs->term->rows) {
            RECT conf_left_rect = {
                term_left,
                term_top + term_height,
                term_left + wgs->font_width,
                term_top + conf_height
            };
            FrameRect(hdc, &conf_left_rect, conf_brush);
        }
        DeleteObject(conf_brush);

        RECT left_rect = {
            term_left,
            term_top,
            term_left + wgs->font_width,
            term_top + term_height
        };
        RECT right_rect = {
            term_left + term_width - wgs->font_width,
            term_top,
            term_left + term_width,
            term_top + term_height
        };
        RECT top_rect = {
            term_left,
            term_top,
            term_left + term_width,
            term_top + wgs->font_height
        };
        RECT bottom_rect = {
            term_left,
            term_top + term_height - wgs->font_height,
            term_left + term_width,
            term_top + term_height
        };
        HBRUSH term_brush = CreateSolidBrush(wgs->colours[focus ? OSC4_COLOUR_fg_bold
            : OSC4_COLOUR_fg]);
        FrameRect(hdc, &left_rect, term_brush);
        FrameRect(hdc, &right_rect, term_brush);
        FrameRect(hdc, &top_rect, term_brush);
        FrameRect(hdc, &bottom_rect, term_brush);
        DeleteObject(term_brush);

        RECT cursor_rect = {
            term_left + wgs->term->curs.x * wgs->font_width,
            term_top + wgs->term->curs.y * wgs->font_height,
            term_left + wgs->term->curs.x * wgs->font_width + wgs->font_width,
            term_top + wgs->term->curs.y * wgs->font_height + wgs->font_height
        };
        HBRUSH cursor_brush = CreateSolidBrush(wgs->colours[OSC4_COLOUR_cursor_bg]);
        if (wgs->term->has_focus) {
            FillRect(hdc, &cursor_rect, cursor_brush);
        } else {
            FrameRect(hdc, &cursor_rect, cursor_brush);
        }
        DeleteObject(cursor_brush);

        paint_term_color_names(hdc, wgs, term_left, term_top);

        EndPaint(hwnd, &ps);
        ShowCaret(hwnd);
        return 0;
      }
      case WM_KEYDOWN:
        if (wParam == 'P') {
            RECT ss;
            ss.left = ss.top = 0;
            ss.right = GetSystemMetrics(SM_CXSCREEN);
            ss.bottom = GetSystemMetrics(SM_CYSCREEN);
            wchar_t *text = wcsdup(L"typedef struct InitData\r\n{\r\n    const LPRECT screen_size;\r\n    const LPWSTR text;\r\n}\r\n");
            size_t text_length = 0;
            show_paste_confirm(&ss, &text, &text_length);
            free(text);
        } else if (wParam == 'F') {
            WinGuiSession *wgs = pane_get_active_session(focused_pane);
            show_finddlg(wgs);
        } else if (wParam == 'E') {
            WinGuiSession *wgs = pane_get_active_session(focused_pane);
            TermRemoteResult result;
            if (do_term_remote(frame_hwnd, &result)) {
                if (result.delayed) {
                    term_remote_wgs = wgs;
                    term_remote_result = result;
                    SetTimer(hwnd, TIMER_TERM_REMOTE, 5000, NULL);
                } else {
                    term_remote_action(wgs, &result);
                }
            }
        } else if (wParam == 'R') {
            WinGuiSession *wgs = pane_get_active_session(focused_pane);
            Conf *prev_conf = conf_copy(wgs->conf);
            const char *session_name = wgs->session_name;
            if (!do_reconfig_pp(frame_hwnd, wgs->conf, &session_name, 0)) {
                conf_free(prev_conf);
                return 0;
            }

            reconfig_session_title(wgs, session_name);

            bool set_palette_needed = false;
            if (conf_get_bool(wgs->conf, CONF_try_palette) != conf_get_bool(prev_conf, CONF_try_palette)) {
                reconfig_palette(wgs);
                set_palette_needed = true;
            }
            reconfig_term_dimensions(wgs, prev_conf);
            win_set_cursor_pos(&wgs->termwin, wgs->term->curs.x, wgs->term->curs.y);

            if (set_palette_needed) {
                set_palette(wgs);
            }
        } else if (wParam == 'S') {
            flip_full_screen();
        } else if (wParam == 'T') {
            flip_always_on_top();
        } else if (wParam == VK_APPS) {
            POINT cursorpos;
            pane_get_active_session_hotspot(pane_get_from_term_hwnd(hwnd), &cursorpos);
            TrackPopupMenu(popup_menus[0].menu,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                cursorpos.x, cursorpos.y,
                0, hwnd, NULL);
        } else if (wParam == 'N') {
            Conf *conf = NULL;
            const char *session_name = NULL;
            if (create_conf(NULL, &conf, &session_name)) {
                WinGuiSession *wgs = add_session(focused_pane, conf, session_name, pane_get_session_count(focused_pane));
                set_palette(wgs);
            }
        } else if (wParam == VK_TAB) {
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (shift && ctrl) {
                PostMessage(frame_hwnd, WM_TAB_CYCLE, 1, 0);
                return 0;
            }
            if (ctrl) {
                PostMessage(frame_hwnd, WM_TAB_CYCLE, 0, 0);
                return 0;
            }
        } else {
            if (pane_get_active_session_index(focused_pane) != 0) {
                pane_set_session_notified(focused_pane, 0);
            }
            show_mouseptr(pane_get_active_session(focused_pane), false);
        }
        return 0;
      case WM_SETFOCUS: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        WinGuiSession *wgs = pane_get_active_session(pane);
        handle_term_hwnd_setfocus(wgs);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
      }
      case WM_KILLFOCUS: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        WinGuiSession *wgs = pane_get_active_session(pane);
        handle_term_hwnd_killfocus(wgs, pane);
        if (wgs) {InvalidateRect(wgs->term_hwnd, NULL, TRUE);}
        return 0;
      }
      case WM_NCLBUTTONDOWN:
      case WM_NCRBUTTONDOWN: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        change_focused_pane(pane);
        show_mouseptr(pane_get_active_session(pane), true);
        break;
      }
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        change_focused_pane(pane);
        if (message == WM_RBUTTONDOWN && (wParam & MK_CONTROL)) {
            POINT cursorpos;
            show_mouseptr(pane_get_active_session(pane), true);
            GetCursorPos(&cursorpos);
            TrackPopupMenu(popup_menus[0].menu,
                TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                cursorpos.x, cursorpos.y,
                0, hwnd, NULL);
        } else {
            show_mouseptr(pane_get_active_session(pane), true);
        }
        return 0;
      }
      case WM_INITMENU: {
        Pane *pane = pane_get_from_term_hwnd(hwnd);
        WinGuiSession *wgs = pane_get_active_session(pane);
        handle_wm_initmenu(wgs, (HMENU)wParam);
        return 0;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDM_FULLSCREEN:
            flip_full_screen();
            break;
          case IDM_ALWAYSONTOP:
            flip_always_on_top();
            break;
          case IDM_VISUALIZECARET:
            visualize_caret = !visualize_caret;
            update_caret_bitmap(root_split);
            check_menu_item(IDM_VISUALIZECARET, visualize_caret);
            InvalidateRect(pane_get_term_hwnd(focused_pane), NULL, TRUE);
            break;
        }
        return 0;
      case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        MapWindowPoints(hwnd, frame_hwnd, &pt, 1);
        SendMessage(frame_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
        return 0;
      }
      case WM_NCMOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(frame_hwnd, &pt);
        SendMessage(frame_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
        break;
      }
      case WM_TIMER:
        if (wParam == TIMER_TERM_REMOTE) {
            if (is_wgs_valid(term_remote_wgs, root_split) && term_remote_wgs->term_hwnd == hwnd) {
                term_remote_action(term_remote_wgs, &term_remote_result);
                KillTimer(hwnd, 1);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static const WCHAR sessionClass[] = L"SessionClass";

void register_term_hwnd_class() {
    WNDCLASSW wndclass;

    wndclass.style = 0;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hinst;
    wndclass.hIcon = NULL;
    wndclass.hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_IBEAM));
    wndclass.hbrBackground = NULL;
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = sessionClass;
    RegisterClassW(&wndclass);
}

HWND create_term_hwnd(const RECT *rect, void *user_data) {
    return CreateWindowExW(0, sessionClass, NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
        frame_hwnd, NULL, hinst, user_data);
}
