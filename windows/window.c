/*
 * window.c - the PuTTY(tel) main program, which runs a PuTTY terminal
 * emulator and backend in a window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#ifdef __WINE__
#define NO_MULTIMON                    /* winelib doesn't have this */
#endif

#ifndef NO_MULTIMON
#define COMPILE_MULTIMON_STUBS
#endif

#include "putty.h"
#include "terminal.h"
#include "storage.h"
#include "win_res.h"
#include "winsecur.h"
#include "winseat.h"
#include "tree234.h"

#ifndef NO_MULTIMON
#include <multimon.h>
#endif

#include <imm.h>
#include <commctrl.h>
#include <richedit.h>
#include <mmsystem.h>

/* From MSDN: In the WM_SYSCOMMAND message, the four low-order bits of
 * wParam are used by Windows, and should be masked off, so we shouldn't
 * attempt to store information in them. Hence all these identifiers have
 * the low 4 bits clear. Also, identifiers should < 0xF000. */

#define IDM_SHOWLOG   0x0010
#define IDM_NEWSESS   0x0020
#define IDM_DUPSESS   0x0030
#define IDM_RESTART   0x0040
#define IDM_RECONF    0x0050
#define IDM_CLRSB     0x0060
#define IDM_RESET     0x0070
#define IDM_DUPSESS_NEW 0x0080
#define IDM_CLOSESESS 0x0090
#define IDM_EXIT      0x00A0
#define IDM_HELP      0x0140
#define IDM_ABOUT     0x0150
#define IDM_SAVEDSESS 0x0160
#define IDM_COPYALL   0x0170
#define IDM_FULLSCREEN  0x0180
#define IDM_COPY      0x0190
#define IDM_PASTE     0x01A0
#define IDM_CONFIRM_PASTE 0x01B0
#define IDM_SPECIALSEP 0x0200

#define IDM_SPECIAL_MIN 0x0400
#define IDM_SPECIAL_MAX 0x0800

#define IDM_SAVED_MIN 0x1000
#define IDM_SAVED_MAX 0x5000
#define MENU_SAVED_STEP 16
/* Maximum number of sessions on saved-session submenu */
#define MENU_SAVED_MAX ((IDM_SAVED_MAX-IDM_SAVED_MIN) / MENU_SAVED_STEP)

#define WM_IGNORE_CLIP (WM_APP + 2)
#define WM_FULLSCR_ON_MAX (WM_APP + 3)
#define WM_GOT_CLIPDATA (WM_APP + 4)

/* Needed for Chinese support and apparently not always defined. */
#ifndef VK_PROCESSKEY
#define VK_PROCESSKEY 0xE5
#endif

/* Mouse wheel support. */
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A           /* not defined in earlier SDKs */
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

/* DPI awareness support */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#define WM_DPICHANGED_BEFOREPARENT 0x02E2
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#define WM_GETDPISCALEDSIZE 0x02E4
#endif

/* VK_PACKET, used to send Unicode characters in WM_KEYDOWNs */
#ifndef VK_PACKET
#define VK_PACKET 0xE7
#endif

typedef struct WinGuiFrontend WinGuiFrontend;

static Mouse_Button translate_button(Conf *conf, Mouse_Button button);
static void show_mouseptr(WinGuiFrontend *wgf, bool show);
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static int TranslateKey(WinGuiFrontend *, UINT message, WPARAM wParam, LPARAM lParam,
                        unsigned char *output);
static void init_palette(WinGuiFrontend *);
static void init_fonts(WinGuiFrontend *, int, int);
static void init_dpi_info(void);
static void another_font(WinGuiFrontend *, int);
static void deinit_fonts(WinGuiFrontend *);
static void set_input_locale(HKL);
static void update_savedsess_menu(void);
static void init_winfuncs(void);

static bool is_full_screen(void);
static void make_full_screen(void);
static void clear_full_screen(void);
static void flip_full_screen(void);
static void process_clipdata(HWND hwnd, HGLOBAL clipdata, bool unicode);
static void paste_clipdata(Terminal *term, WPARAM wParam, LPARAM lParam);
static void setup_clipboards(Terminal *, Conf *);

/* Window layout information */
static void reset_window(WinGuiFrontend *, int);
static int extra_width, extra_height;
static bool was_zoomed = false;

static void flash_window(int mode);
static void sys_cursor_update(WinGuiFrontend *);
static bool get_fullscreen_rect(RECT * ss);

static int kbd_codepage;

#define TIMING_TIMER_ID 1234
static long timing_next_time;

static struct {
    HMENU menu;
} popup_menus[2];
enum { SYSMENU, CTXMENU };
static HMENU savedsess_menu;

struct wm_netevent_params {
    /* Used to pass data to wm_netevent_callback */
    WPARAM wParam;
    LPARAM lParam;
};

static void conf_cache_data(WinGuiFrontend *);

static struct sesslist sesslist;       /* for saved-session menu */

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_UNDERLINE 2
#define FONT_BOLDUND 3
#define FONT_WIDE       0x04
#define FONT_HIGH       0x08
#define FONT_NARROW     0x10

#define FONT_OEM        0x20
#define FONT_OEMBOLD    0x21
#define FONT_OEMUND     0x22
#define FONT_OEMBOLDUND 0x23

#define FONT_MAXNO      0x40
#define FONT_SHIFT      5

enum MONITOR_DPI_TYPE { MDT_EFFECTIVE_DPI, MDT_ANGULAR_DPI, MDT_RAW_DPI, MDT_DEFAULT };
DECL_WINDOWS_FUNCTION(static, HRESULT, GetDpiForMonitor, (HMONITOR hmonitor, enum MONITOR_DPI_TYPE dpiType, UINT *dpiX, UINT *dpiY));
DECL_WINDOWS_FUNCTION(static, HRESULT, GetSystemMetricsForDpi, (int nIndex, UINT dpi));
DECL_WINDOWS_FUNCTION(static, HRESULT, AdjustWindowRectExForDpi, (LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi));
DECL_WINDOWS_FUNCTION(static, BOOL, SystemParametersInfoForDpi, (UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi));

POINT dpi_info;
static RECT dpi_changed_new_wnd_rect;

static int dbltime, lasttime, lastact;
static Mouse_Button lastbtn;

static UINT wm_mousewheel = WM_MOUSEWHEEL;

#define IS_HIGH_VARSEL(wch1, wch2) \
    ((wch1) == 0xDB40 && ((wch2) >= 0xDD00 && (wch2) <= 0xDDEF))
#define IS_LOW_VARSEL(wch) \
    (((wch) >= 0x180B && (wch) <= 0x180D) || /* MONGOLIAN FREE VARIATION SELECTOR */ \
     ((wch) >= 0xFE00 && (wch) <= 0xFE0F)) /* VARIATION SELECTOR 1-16 */

struct WinGuiFrontend {
    int caret_x;
    int caret_y;
    Ldisc *ldisc;
    Backend *backend;
    struct unicode_data ucsdata;
    const SessionSpecial *specials;
    HMENU specials_menu;
    int n_specials;
    Conf *conf;
    LogContext *logctx;
    Terminal *term;
    int cursor_type;
    int vtmode;
    HFONT fonts[FONT_MAXNO];
    LOGFONT lfont;
    bool fontflag[FONT_MAXNO];
    enum {
        BOLD_NONE, BOLD_SHADOW, BOLD_FONT
    } bold_font_mode;
    bool bold_colours;
    enum {
        UND_LINE, UND_FONT
    } und_mode;
    int descent;
    int font_strikethrough_y;
    COLORREF colours[OSC4_NCOLOURS];
    HPALETTE pal;
    LPLOGPALETTE logpal;
    bool tried_pal;
    COLORREF colorref_modifier;
    HBITMAP caretbm;
    bool send_raw_mouse;
    int wheel_accumulator;
    bool pointer_indicates_raw_mouse;
    BusyStatus busy_status;
    char *window_name;
    char *icon_name;
    int compose_state;
    TermWin wintw;
    HDC wintw_hdc;
    HICON trust_icon;
    eventlog_stuff eventlogstuff;
    Seat seat;
    LogPolicy logpolicy;
    int font_width, font_height;
    bool font_dualwidth, font_varpitch;
    int offset_width, offset_height;
    bool need_backend_resize;
    struct {
      int first_time;
      RECT ss;
    } request_resize;
    struct {
      RECT ss;
    } reset_window;
    struct {
      bool ignore_clip;
    } wnd_proc;
    struct {
      wchar_t pending_surrogate;
    } syschar;
    struct {
      int alt_sum;
      int compose_char;
      WPARAM compose_keycode;
    } translate_key;
    int session_id;
    const char *session_name;
    bool remote_closed;
    bool delete_session;
    int remote_exitcode;
    int tab_index;
    struct {
      int total;
      int start;
      int page;
    } si;
    bool cursor_visible;
    bool cursor_forced_visible;
    struct {
      bool was_zoomed;
      int font_width;
      int font_height;
    } resize_either;
    bool term_palette_init;
    int font_dpi;
};

HWND frame_hwnd = NULL;
static HWND term_hwnd = NULL;
static bool confirm_paste = true;

#include "frame.h"
#include "tabbar.h"
#include "pointerarray.h"
#include "pastedlg.h"

static bool wintw_setup_draw_ctx(TermWin *);
static void wintw_draw_text(TermWin *, int x, int y, wchar_t *text, int len,
                            unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_cursor(TermWin *, int x, int y, wchar_t *text, int len,
                              unsigned long attrs, int lattrs, truecolour tc);
static void wintw_draw_trust_sigil(TermWin *, int x, int y);
static int wintw_char_width(TermWin *, int uc);
static void wintw_free_draw_ctx(TermWin *);
static void wintw_set_cursor_pos(TermWin *, int x, int y);
static void wintw_set_raw_mouse_mode(TermWin *, bool enable);
static void wintw_set_raw_mouse_mode_pointer(TermWin *, bool enable);
static void wintw_set_scrollbar(TermWin *, int total, int start, int page);
static void wintw_bell(TermWin *, int mode);
static void wintw_clip_write(
    TermWin *, int clipboard, wchar_t *text, int *attrs,
    truecolour *colours, int len, bool must_deselect);
static void wintw_clip_request_paste(TermWin *, int clipboard);
static void wintw_refresh(TermWin *);
static void wintw_request_resize(TermWin *, int w, int h);
static void wintw_set_title(TermWin *, const char *title);
static void wintw_set_icon_title(TermWin *, const char *icontitle);
static void wintw_set_minimised(TermWin *, bool minimised);
static void wintw_set_maximised(TermWin *, bool maximised);
static void wintw_move(TermWin *, int x, int y);
static void wintw_set_zorder(TermWin *, bool top);
static void wintw_palette_set(TermWin *, unsigned, unsigned, const rgb *);
static void wintw_palette_get_overrides(TermWin *, Terminal *);

static const TermWinVtable windows_termwin_vt = {
    .setup_draw_ctx = wintw_setup_draw_ctx,
    .draw_text = wintw_draw_text,
    .draw_cursor = wintw_draw_cursor,
    .draw_trust_sigil = wintw_draw_trust_sigil,
    .char_width = wintw_char_width,
    .free_draw_ctx = wintw_free_draw_ctx,
    .set_cursor_pos = wintw_set_cursor_pos,
    .set_raw_mouse_mode = wintw_set_raw_mouse_mode,
    .set_raw_mouse_mode_pointer = wintw_set_raw_mouse_mode_pointer,
    .set_scrollbar = wintw_set_scrollbar,
    .bell = wintw_bell,
    .clip_write = wintw_clip_write,
    .clip_request_paste = wintw_clip_request_paste,
    .refresh = wintw_refresh,
    .request_resize = wintw_request_resize,
    .set_title = wintw_set_title,
    .set_icon_title = wintw_set_icon_title,
    .set_minimised = wintw_set_minimised,
    .set_maximised = wintw_set_maximised,
    .move = wintw_move,
    .set_zorder = wintw_set_zorder,
    .palette_set = wintw_palette_set,
    .palette_get_overrides = wintw_palette_get_overrides,
};

const bool share_can_be_downstream = true;
const bool share_can_be_upstream = true;

static bool win_seat_is_utf8(Seat *seat);
static char *win_seat_get_ttymode(Seat *seat, const char *mode);
static StripCtrlChars *win_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic);

static size_t win_seat_output(
    Seat *seat, bool is_stderr, const void *, size_t);
static bool win_seat_eof(Seat *seat);
static int win_seat_get_userpass_input(
    Seat *seat, prompts_t *p, bufchain *input);
static void win_seat_notify_remote_exit(Seat *seat);
static void win_seat_connection_fatal(Seat *seat, const char *msg);
static void win_seat_update_specials_menu(Seat *seat);
static void win_seat_set_busy_status(Seat *seat, BusyStatus status);
static bool win_seat_set_trust_status(Seat *seat, bool trusted);
static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y);
static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y);

static int win_seat_verify_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, const char *keydisp, char **fingerprints,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return dlg_verify_ssh_host_key(frame_hwnd, host, port, keytype, keystr, keydisp, fingerprints);
}

static int win_seat_confirm_weak_crypto_primitive(
    Seat *seat, const char *algtype, const char *algname,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return dlg_seat_confirm_weak_crypto_primitive(algtype, algname);
}

static int win_seat_confirm_weak_cached_hostkey(
    Seat *seat, const char *algname, const char *betteralgs,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return dlg_confirm_weak_cached_hostkey(algname, betteralgs);
}

static const SeatVtable win_seat_vt = {
    .output = win_seat_output,
    .eof = win_seat_eof,
    .get_userpass_input = win_seat_get_userpass_input,
    .notify_remote_exit = win_seat_notify_remote_exit,
    .connection_fatal = win_seat_connection_fatal,
    .update_specials_menu = win_seat_update_specials_menu,
    .get_ttymode = win_seat_get_ttymode,
    .set_busy_status = win_seat_set_busy_status,
    .verify_ssh_host_key = win_seat_verify_ssh_host_key,
    .confirm_weak_crypto_primitive = win_seat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = win_seat_confirm_weak_cached_hostkey,
    .is_utf8 = win_seat_is_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = win_seat_get_window_pixel_size,
    .stripctrl_new = win_seat_stripctrl_new,
    .set_trust_status = win_seat_set_trust_status,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = win_seat_get_cursor_position,
};

static void win_gui_eventlog(LogPolicy *lp, const char *string)
{
    WinGuiFrontend* wgf = container_of(lp, WinGuiFrontend, logpolicy);
    dlg_eventlog(&wgf->eventlogstuff, string);
}

static int win_gui_askappend(LogPolicy *lp, Filename *filename,
                             void (*callback)(void *ctx, int result),
                             void *ctx)
{
    return dlg_askappend(filename);
}

static void win_gui_logging_error(LogPolicy *lp, const char *event)
{
    WinGuiFrontend *wgf = container_of(lp, WinGuiFrontend, logpolicy);

    /* Send 'can't open log file' errors to the terminal window.
     * (Marked as stderr, although terminal.c won't care.) */
    seat_stderr_pl(&wgf->seat, ptrlen_from_asciz(event));
    seat_stderr_pl(&wgf->seat, PTRLEN_LITERAL("\r\n"));
}

const LogPolicyVtable win_gui_logpolicy_vt = {
    .eventlog = win_gui_eventlog,
    .askappend = win_gui_askappend,
    .logging_error = win_gui_logging_error,
    .verbose = null_lp_verbose_yes,
};

static WinGuiFrontend *wgf_active = NULL;

static void start_backend(WinGuiFrontend *wgf)
{
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    const struct BackendVtable *vt;
    char *realhost = NULL;

    /*
     * Select protocol. This is farmed out into a table in a
     * separate file to enable an ssh-free variant.
     */
    vt = backend_vt_from_proto(conf_get_int(conf, CONF_protocol));
    if (!vt) {
        char *str = dupprintf("%s Internal Error", appname);
        MessageBox(NULL, "Unsupported protocol number found",
                   str, MB_OK | MB_ICONEXCLAMATION);
        sfree(str);
        return;
    }

    seat_set_trust_status(&wgf->seat, true);
    char *error = backend_init(vt, &wgf->seat, &wgf->backend, wgf->logctx, conf,
                         conf_get_str(conf, CONF_host),
                         conf_get_int(conf, CONF_port),
                         &realhost,
                         conf_get_bool(conf, CONF_tcp_nodelay),
                         conf_get_bool(conf, CONF_tcp_keepalives));
    if (error) {
        char *str = dupprintf("%s Error", appname);
        char *msg = dupprintf("Unable to open connection to\n%s\n%s",
                              conf_dest(conf), error);
        sfree(error);
        MessageBox(NULL, msg, str, MB_ICONERROR | MB_OK);
        sfree(str);
        sfree(msg);
        if (wgf->backend) {
            backend_free(wgf->backend);
            wgf->backend = NULL;
        }
        if (realhost) {
            sfree(realhost);
        }
        return;
    }
    term_setup_window_titles(term, realhost);
    sfree(realhost);

    /*
     * Connect the terminal to the backend for resize purposes.
     */
    term_provide_backend(term, wgf->backend);

    /*
     * Set up a line discipline.
     */
    wgf->ldisc = ldisc_create(conf, term, wgf->backend, &wgf->seat);

    wgf->remote_closed = false;
}

static void stop_backend(WinGuiFrontend *wgf)
{
    Terminal *term = wgf->term;
    char *newtitle;

    newtitle = dupprintf("%s (inactive)", appname);
    win_set_icon_title(&wgf->wintw, newtitle);
    win_set_title(&wgf->wintw, newtitle);
    sfree(newtitle);

    if (wgf->ldisc) {
        ldisc_free(wgf->ldisc);
        wgf->ldisc = NULL;
    }
    if (wgf->backend) {
        backend_free(wgf->backend);
        wgf->backend = NULL;
        term_provide_backend(term, NULL);
        seat_update_specials_menu(&wgf->seat);
    }
}

static void frontend_set_tab_index(void *p, int index) {
    WinGuiFrontend *wgf = (WinGuiFrontend *)p;
    wgf->tab_index = index;
}

static bool is_session_deletable(WinGuiFrontend *wgf) {
    int close_on_exit = conf_get_int(wgf->conf, CONF_close_on_exit);
    return (close_on_exit == FORCE_ON ||
            (close_on_exit == AUTO && wgf->remote_exitcode != INT_MAX) ||
            wgf->delete_session);
}

static void remote_close_callback(void *context) {
    WinGuiFrontend *wgf = (WinGuiFrontend *)context;
    assert(wgf->remote_closed);

    stop_backend(wgf);
    if (is_session_deletable(wgf)) {
        delete_session(wgf);
    }
}

static void remote_close(WinGuiFrontend *wgf, int exitcode, const char *msg) {
    queue_toplevel_callback(remote_close_callback, wgf);
    wgf->remote_closed = true;
    wgf->remote_exitcode = exitcode;

    if (is_session_deletable(wgf)) {
        return;
    }
    tab_bar_set_tab_unusable(wgf->tab_index, true);

    term_data(wgf->term, true, "\r\n", 2);
    term_set_trust_status(wgf->term, true);
    term_data(wgf->term, true, "\033[31m", 5);
    const char *p0 = msg;
    const char *p = msg;
    while (*p) {
        if (*p == '\n' && p[1] != 0 && p != msg && p[-1] != '\r') {
            term_data(wgf->term, true, p0, p-p0);
            term_data(wgf->term, true, "\r\n", 2);
            p++;
            p0 = p;
        }
        p++;
    }
    term_data(wgf->term, true, p0, p-p0);
    term_data(wgf->term, true, "\033[0m", 4);
    term_set_trust_status(wgf->term, false);
}

static HFONT get_dpi_aware_tab_bar_font() {
    if (p_SystemParametersInfoForDpi) {
        struct {
            NONCLIENTMETRICSW ncm;
#if WINVER < 0x0600
            int iPaddedBorderWidth;
#endif
        } buffer;
        NONCLIENTMETRICSW *ncm = (NONCLIENTMETRICSW *)&buffer;
        ncm->cbSize = sizeof(buffer);
        if (p_SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ncm->cbSize, ncm, 0, dpi_info.y)) {
            return CreateFontIndirectW(&ncm->lfMenuFont);
        }
    } else {
        NONCLIENTMETRICS ncm;
        ncm.cbSize = sizeof(NONCLIENTMETRICS);
        if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
           return CreateFontIndirect(&ncm.lfMenuFont);
        }
    }
    return NULL;
}

static void check_menu_item(UINT item, UINT check)
{
    int i;
    for (i = 0; i < lenof(popup_menus); i++)
        CheckMenuItem(popup_menus[i].menu, item, check);
}

const unsigned cmdline_tooltype =
    TOOLTYPE_HOST_ARG |
    TOOLTYPE_PORT_ARG |
    TOOLTYPE_NO_VERBOSE_OPTION;

HINSTANCE hinst;

const char *term_class_name = "TermWindow";

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    HRESULT hr;
    int guess_width, guess_height;

    dll_hijacking_protection();

    hinst = inst;

    sk_init();

    init_common_controls();

    /* Set Explicit App User Model Id so that jump lists don't cause
       PuTTY to hang on to removable media. */

    set_explicit_app_user_model_id();

    /* Ensure a Maximize setting in Explorer doesn't maximise the
     * config box. */
    defuse_showwindow();

    init_winver();

    /*
     * If we're running a version of Windows that doesn't support
     * WM_MOUSEWHEEL, find out what message number we should be
     * using instead.
     */
    if (osMajorVersion < 4 ||
        (osMajorVersion == 4 && osPlatformId != VER_PLATFORM_WIN32_NT))
        wm_mousewheel = RegisterWindowMessage("MSWHEEL_ROLLMSG");

    init_help();

    init_winfuncs();

    Conf *conf = conf_new();
    const char *session_name = NULL;

    /*
     * Initialize COM.
     */
    hr = CoInitialize(NULL);
    if (hr != S_OK && hr != S_FALSE) {
        char *str = dupprintf("%s Fatal Error", appname);
        MessageBox(NULL, "Failed to initialize COM subsystem",
                   str, MB_OK | MB_ICONEXCLAMATION);
        sfree(str);
        return 1;
    }

    /*
     * Process the command line.
     */
    {
        char *p;
        bool special_launchable_argument = false;

        settings_set_default_protocol(be_default_protocol);
        /* Find the appropriate default port. */
        {
            const struct BackendVtable *vt =
                backend_vt_from_proto(be_default_protocol);
            settings_set_default_port(0); /* illegal */
            if (vt)
                settings_set_default_port(vt->default_port);
        }
        conf_set_int(conf, CONF_logtype, LGTYP_NONE);

        do_defaults(NULL, conf);

        p = cmdline;

        /*
         * Process a couple of command-line options which are more
         * easily dealt with before the line is broken up into words.
         * These are the old-fashioned but convenient @sessionname and
         * the internal-use-only &sharedmemoryhandle, plus the &R
         * prefix for -restrict-acl, all of which are used by PuTTYs
         * auto-launching each other via System-menu options.
         */
        while (*p && isspace(*p))
            p++;
        if (*p == '&' && p[1] == 'R' &&
            (!p[2] || p[2] == '@' || p[2] == '&')) {
            /* &R restrict-acl prefix */
            restrict_process_acl();
            p += 2;
        }

        if (*p == '@') {
            /*
             * An initial @ means that the whole of the rest of the
             * command line should be treated as the name of a saved
             * session, with _no quoting or escaping_. This makes it a
             * very convenient means of automated saved-session
             * launching, via IDM_SAVEDSESS or Windows 7 jump lists.
             */
            int i = strlen(p);
            while (i > 1 && isspace(p[i - 1]))
                i--;
            p[i] = '\0';
            do_defaults(p + 1, conf);
            if (conf_launchable(conf)) {
                session_name = dupstr(p + 1);
            } else {
                if (!do_config(NULL, conf, &session_name)) {
                    cleanup_exit(0);
                }
            }
            special_launchable_argument = true;
        } else if (*p == '&') {
            /*
             * An initial & means we've been given a command line
             * containing the hex value of a HANDLE for a file
             * mapping object, which we must then interpret as a
             * serialised Conf.
             */
            HANDLE filemap;
            void *cp;
            unsigned cpsize;
            if (sscanf(p + 1, "%p:%u:", &filemap, &cpsize) == 2 &&
                (cp = MapViewOfFile(filemap, FILE_MAP_READ,
                                    0, 0, cpsize)) != NULL) {
                BinarySource src[1];
                BinarySource_BARE_INIT(src, cp, cpsize);
                if (!conf_deserialise(conf, src))
                    modalfatalbox("Serialised configuration data was invalid");
                UnmapViewOfFile(cp);
                CloseHandle(filemap);
                session_name = dupstr(strchr(strchr(p+1, ':')+1,':')+1);
            } else if (!do_config(NULL, conf, &session_name)) {
                cleanup_exit(0);
            }
            special_launchable_argument = true;
        } else if (!*p) {
            /* Do-nothing case for an empty command line - or rather,
             * for a command line that's empty _after_ we strip off
             * the &R prefix. */
        } else {
            /*
             * Otherwise, break up the command line and deal with
             * it sensibly.
             */
            int argc, i;
            char **argv;

            split_into_argv(cmdline, &argc, &argv, NULL);

            for (i = 0; i < argc; i++) {
                char *p = argv[i];
                int ret;

                ret = cmdline_process_param(p, i+1<argc?argv[i+1]:NULL,
                                            1, conf);
                if (ret == -2) {
                    cmdline_error("option \"%s\" requires an argument", p);
                } else if (ret == 2) {
                    i++;               /* skip next argument */
                } else if (ret == 1) {
                    continue;          /* nothing further needs doing */
                } else if (!strcmp(p, "-cleanup")) {
                    /*
                     * `putty -cleanup'. Remove all registry
                     * entries associated with PuTTY, and also find
                     * and delete the random seed file.
                     */
                    char *s1, *s2;
                    s1 = dupprintf("This procedure will remove ALL Registry entries\n"
                                   "associated with %s, and will also remove\n"
                                   "the random seed file. (This only affects the\n"
                                   "currently logged-in user.)\n"
                                   "\n"
                                   "THIS PROCESS WILL DESTROY YOUR SAVED SESSIONS.\n"
                                   "Are you really sure you want to continue?",
                                   appname);
                    s2 = dupprintf("%s Warning", appname);
                    if (message_box(NULL, s1, s2,
                                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
                                    HELPCTXID(option_cleanup)) == IDYES) {
                        cleanup_all();
                    }
                    sfree(s1);
                    sfree(s2);
                    exit(0);
                } else if (!strcmp(p, "-pgpfp")) {
                    pgp_fingerprints_msgbox(NULL);
                    exit(1);
                } else if (*p != '-') {
                    cmdline_error("unexpected argument \"%s\"", p);
                } else {
                    cmdline_error("unknown option \"%s\"", p);
                }

                if (ret == 2 && !session_name && strcmp(p, "-load") == 0) {
                    session_name = dupstr(argv[i]);
                }
            }
        }

        cmdline_run_saved(conf);

        /*
         * Bring up the config dialog if the command line hasn't
         * (explicitly) specified a launchable configuration.
         */
        if (!(special_launchable_argument || cmdline_host_ok(conf))) {
            if (session_name) {
                sfree((char *)session_name);
                session_name = NULL;
            }
            if (!do_config(NULL, conf, &session_name))
                cleanup_exit(0);
        }

        prepare_session(conf);

        if (!session_name) {
            session_name = dupstr(conf_get_str(conf, CONF_host));
        }
    }

    if (!prev) {
        WNDCLASSW wndclass;

        register_frame_class();

        wndclass.style = 0;
        wndclass.lpfnWndProc = WndProc;
        wndclass.cbClsExtra = 0;
        wndclass.cbWndExtra = 0;
        wndclass.hInstance = inst;
        wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_MAINICON));
        wndclass.hCursor = LoadCursor(NULL, IDC_IBEAM);
        wndclass.hbrBackground = NULL;
        wndclass.lpszMenuName = NULL;
        wndclass.lpszClassName = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, term_class_name);

        RegisterClassW(&wndclass);
    }

    /*
     * Guess some defaults for the window size. This all gets
     * updated later, so we don't really care too much. However, we
     * do want the font width/height guesses to correspond to a
     * large font rather than a small one...
     */

    const int font_width = 10;
    const int font_height = 20;
    extra_width = 25;
    extra_height = 28;
    guess_width = extra_width + font_width * conf_get_int(conf, CONF_width);
    guess_height = extra_height + font_height*conf_get_int(conf, CONF_height);
    {
        RECT r;
        get_fullscreen_rect(&r);
        if (guess_width > r.right - r.left)
            guess_width = r.right - r.left;
        if (guess_height > r.bottom - r.top)
            guess_height = r.bottom - r.top;
    }

    {
        frame_hwnd = create_frame_window(conf, guess_width, guess_height);

        int winmode = WS_CHILD | WS_VISIBLE;
        int exwinmode = 0;
        wchar_t *uappname = dup_mb_to_wc(DEFAULT_CODEPAGE, 0, term_class_name);
        term_hwnd = CreateWindowExW(
            exwinmode, uappname, NULL, winmode, 0,
            0, guess_width, guess_height, frame_hwnd, NULL, inst, NULL);
        memset(&dpi_info, 0, sizeof(dpi_info));
        init_dpi_info();
        sfree(uappname);

        create_tab_bar();
        add_session_tab(conf_get_int(conf, CONF_protocol), session_name, 0);
        tab_bar_set_measurement(get_dpi_aware_tab_bar_font());
        pointer_array_reset(frontend_set_tab_index);
    }

    wgf_active = create_frontend(conf, session_name);
    WinGuiFrontend *wgf = wgf_active;
    pointer_array_insert(0, wgf);

    /*
     * Correct the guesses for extra_{width,height}.
     */
    {
        RECT cr, wr;
        GetWindowRect(frame_hwnd, &wr);
        GetClientRect(frame_hwnd, &cr);
        wgf->offset_width = wgf->offset_height = conf_get_int(conf, CONF_window_border);
        extra_width = wr.right - wr.left - cr.right + cr.left + wgf->offset_width*2;
        extra_height = wr.bottom - wr.top - cr.bottom + cr.top +wgf->offset_height*2;
        adjust_extra_size();
    }

    /*
     * Resize the window, now we know what size we _really_ want it
     * to be.
     */
    guess_width = extra_width + wgf->font_width * wgf->term->cols;
    guess_height = extra_height + wgf->font_height * wgf->term->rows;
    SetWindowPos(frame_hwnd, NULL, 0, 0, guess_width, guess_height,
                 SWP_NOMOVE | SWP_NOREDRAW | SWP_NOZORDER);

    /*
     * Initialise the scroll bar.
     */
    set_scrollbar_from_session(wgf, false);

    /*
     * Prepare the mouse handler.
     */
    lastact = MA_NOTHING;
    lastbtn = MBT_NOTHING;
    dbltime = GetDoubleClickTime();

    /*
     * Set up the session-control options on the system menu.
     */
    {
        HMENU m;
        int j;
        char *str;

        popup_menus[SYSMENU].menu = CreatePopupMenu();
        popup_menus[CTXMENU].menu = CreatePopupMenu();
        AppendMenu(popup_menus[CTXMENU].menu, MF_ENABLED, IDM_COPY, "&Copy");
        AppendMenu(popup_menus[CTXMENU].menu, MF_ENABLED, IDM_PASTE, "&Paste");
        AppendMenu(popup_menus[CTXMENU].menu, MF_SEPARATOR, 0, 0);

        savedsess_menu = CreateMenu();
        get_sesslist(&sesslist, true);
        update_savedsess_menu();

        for (j = 0; j < lenof(popup_menus); j++) {
            m = popup_menus[j].menu;

            AppendMenu(m, MF_ENABLED, IDM_CLOSESESS, "Close &Session");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_NEWSESS, "Ne&w Session...");
            AppendMenu(m, MF_ENABLED, IDM_DUPSESS, "&Duplicate Session");
            AppendMenu(m, MF_POPUP | MF_ENABLED, (UINT_PTR) savedsess_menu,
                       "Sa&ved Sessions");
            AppendMenu(m, MF_ENABLED, IDM_RECONF, "Chan&ge Settings...");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_DUPSESS_NEW, "Duplicate to New &Instance");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_COPYALL, "C&opy All to Clipboard");
            AppendMenu(m, MF_ENABLED, IDM_CLRSB, "C&lear Scrollback");
            AppendMenu(m, MF_ENABLED, IDM_RESET, "Rese&t Terminal");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_FULLSCREEN, "&Full Screen");
            AppendMenu(m, MF_ENABLED, IDM_SHOWLOG, "&Event Log");
            AppendMenu(m, MF_ENABLED | (confirm_paste ? MF_CHECKED : MF_UNCHECKED), IDM_CONFIRM_PASTE, "Confirm Paste");
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            if (has_help())
                AppendMenu(m, MF_ENABLED, IDM_HELP, "&Help");
            str = dupprintf("&About %s", appname);
            AppendMenu(m, MF_ENABLED, IDM_ABOUT, str);
            AppendMenu(m, MF_SEPARATOR, 0, 0);
            AppendMenu(m, MF_ENABLED, IDM_EXIT, "Exit");
            sfree(str);
        }
    }

    if (restricted_acl()) {
        lp_eventlog(&wgf->logpolicy, "Running with restricted process ACL");
    }

    winselgui_set_hwnd(frame_hwnd);
    start_backend(wgf);

    /*
     * Set up the initial input locale.
     */
    set_input_locale(GetKeyboardLayout(0));

    /*
     * Finally show the window!
     */
    ShowWindow(frame_hwnd, show);
    SetForegroundWindow(frame_hwnd);

    term_set_focus(wgf->term, GetForegroundWindow() == frame_hwnd);
    UpdateWindow(term_hwnd);

    while (1) {
        HANDLE *handles;
        int nhandles, n;
        DWORD timeout;

        if (toplevel_callback_pending() ||
            PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
            /*
             * If we have anything we'd like to do immediately, set
             * the timeout for MsgWaitForMultipleObjects to zero so
             * that we'll only do a quick check of our handles and
             * then get on with whatever that was.
             *
             * One such option is a pending toplevel callback. The
             * other is a non-empty Windows message queue, which you'd
             * think we could leave to MsgWaitForMultipleObjects to
             * check for us along with all the handles, but in fact we
             * can't because once PeekMessage in one iteration of this
             * loop has removed a message from the queue, the whole
             * queue is considered uninteresting by the next
             * invocation of MWFMO. So we check ourselves whether the
             * message queue is non-empty, and if so, set this timeout
             * to zero to ensure MWFMO doesn't block.
             */
            timeout = 0;
        } else {
            timeout = INFINITE;
            /* The messages seem unreliable; especially if we're being tricky */
            term_set_focus(wgf_active->term, GetForegroundWindow() == frame_hwnd);
        }

        handles = handle_get_events(&nhandles);

        n = MsgWaitForMultipleObjects(nhandles, handles, false,
                                      timeout, QS_ALLINPUT);

        if ((unsigned)(n - WAIT_OBJECT_0) < (unsigned)nhandles) {
            handle_got_event(handles[n - WAIT_OBJECT_0]);
            sfree(handles);
        } else
            sfree(handles);

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                goto finished;         /* two-level break */

            HWND logbox = event_log_window();
            if (!(IsWindow(logbox) && IsDialogMessage(logbox, &msg)))
                DispatchMessageW(&msg);

            /*
             * WM_NETEVENT messages seem to jump ahead of others in
             * the message queue. I'm not sure why; the docs for
             * PeekMessage mention that messages are prioritised in
             * some way, but I'm unclear on which priorities go where.
             *
             * Anyway, in practice I observe that WM_NETEVENT seems to
             * jump to the head of the queue, which means that if we
             * were to only process one message every time round this
             * loop, we'd get nothing but NETEVENTs if the server
             * flooded us with data, and stop responding to any other
             * kind of window message. So instead, we keep on round
             * this loop until we've consumed at least one message
             * that _isn't_ a NETEVENT, or run out of messages
             * completely (whichever comes first). And we don't go to
             * run_toplevel_callbacks (which is where the netevents
             * are actually processed, causing fresh NETEVENT messages
             * to appear) until we've done this.
             */
            if (msg.message != WM_NETEVENT)
                break;
        }

        run_toplevel_callbacks();
    }

    finished:
    cleanup_exit(msg.wParam);          /* this doesn't return... */
    return msg.wParam;                 /* ... but optimiser doesn't know */
}

static void setup_clipboards(Terminal *term, Conf *conf)
{
    assert(term->mouse_select_clipboards[0] == CLIP_LOCAL);

    term->n_mouse_select_clipboards = 1;

    if (conf_get_bool(conf, CONF_mouseautocopy)) {
        term->mouse_select_clipboards[
            term->n_mouse_select_clipboards++] = CLIP_SYSTEM;
    }

    switch (conf_get_int(conf, CONF_mousepaste)) {
      case CLIPUI_IMPLICIT:
        term->mouse_paste_clipboard = CLIP_LOCAL;
        break;
      case CLIPUI_EXPLICIT:
        term->mouse_paste_clipboard = CLIP_SYSTEM;
        break;
      default:
        term->mouse_paste_clipboard = CLIP_NULL;
        break;
    }
}

/*
 * Clean up and exit.
 */
void cleanup_exit(int code)
{
    /*
     * Clean up.
     */
    sk_cleanup();

    shutdown_help();

    /* Clean up COM. */
    CoUninitialize();

    exit(code);
}

/*
 * Refresh the saved-session submenu from `sesslist'.
 */
static void update_savedsess_menu(void)
{
    int i;
    while (DeleteMenu(savedsess_menu, 0, MF_BYPOSITION)) ;
    /* skip sesslist.sessions[0] == Default Settings */
    for (i = 1;
         i < ((sesslist.nsessions <= MENU_SAVED_MAX+1) ? sesslist.nsessions
                                                       : MENU_SAVED_MAX+1);
         i++)
        AppendMenu(savedsess_menu, MF_ENABLED,
                   IDM_SAVED_MIN + (i-1)*MENU_SAVED_STEP,
                   sesslist.sessions[i]);
    if (sesslist.nsessions <= 1)
        AppendMenu(savedsess_menu, MF_GRAYED, IDM_SAVED_MIN, "(No sessions)");
}

static bool win_seat_is_utf8(Seat *seat)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    return wgf->ucsdata.line_codepage == CP_UTF8;
}

static char *win_seat_get_ttymode(Seat *seat, const char *mode)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    return term_get_ttymode(term, mode);
}

static StripCtrlChars *win_seat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    return stripctrl_new_term(bs_out, false, 0, term);
}

/*
 * Update the Special Commands submenu.
 */
static void win_seat_update_specials_menu(Seat *seat)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    HMENU new_menu;

    if (wgf->specials_menu) {
        DestroyMenu(wgf->specials_menu);
    }

    if (wgf->backend)
        wgf->specials = backend_get_specials(wgf->backend);
    else
        wgf->specials = NULL;

    if (wgf->specials) {
        /* We can't use Windows to provide a stack for submenus, so
         * here's a lame "stack" that will do for now. */
        HMENU saved_menu = NULL;
        int nesting = 1;
        new_menu = CreatePopupMenu();
        int i;
        for (i = 0; nesting > 0; i++) {
            assert(IDM_SPECIAL_MIN + 0x10 * i < IDM_SPECIAL_MAX);
            switch (wgf->specials[i].code) {
              case SS_SEP:
                AppendMenu(new_menu, MF_SEPARATOR, 0, 0);
                break;
              case SS_SUBMENU:
                assert(nesting < 2);
                nesting++;
                saved_menu = new_menu; /* XXX lame stacking */
                new_menu = CreatePopupMenu();
                AppendMenu(saved_menu, MF_POPUP | MF_ENABLED,
                           (UINT_PTR) new_menu, wgf->specials[i].name);
                break;
              case SS_EXITMENU:
                nesting--;
                if (nesting) {
                    new_menu = saved_menu; /* XXX lame stacking */
                    saved_menu = NULL;
                }
                break;
              default:
                AppendMenu(new_menu, MF_ENABLED, IDM_SPECIAL_MIN + 0x10 * i,
                           wgf->specials[i].name);
                break;
            }
        }
        /* Squirrel the highest special. */
        wgf->n_specials = i - 1;
    } else {
        new_menu = NULL;
        wgf->n_specials = 0;
    }
    wgf->specials_menu = new_menu;
}

static void update_mouse_pointer(WinGuiFrontend *wgf)
{
    static LPTSTR cursor_type = IDC_ARROW;
    LPTSTR curstype = NULL;
    wgf->cursor_forced_visible = false;
    switch (wgf->busy_status) {
      case BUSY_NOT:
        if (wgf->pointer_indicates_raw_mouse)
            curstype = IDC_ARROW;
        else
            curstype = IDC_IBEAM;
        break;
      case BUSY_WAITING:
        curstype = IDC_APPSTARTING; /* this may be an abuse */
        wgf->cursor_forced_visible = true;
        break;
      case BUSY_CPU:
        curstype = IDC_WAIT;
        wgf->cursor_forced_visible = true;
        break;
      default:
        unreachable("Bad busy_status");
    }
    if (curstype != cursor_type) {
        HCURSOR cursor = LoadCursor(NULL, curstype);
        SetClassLongPtr(term_hwnd, GCLP_HCURSOR, (LONG_PTR)cursor);
        SetCursor(cursor); /* force redraw of cursor at current posn */
        cursor_type = curstype;
    }
    show_mouseptr(wgf, wgf->cursor_visible);
}

static void win_seat_set_busy_status(Seat *seat, BusyStatus status)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    wgf->busy_status = status;
    if (wgf != wgf_active) {return;}
    update_mouse_pointer(wgf);
}

static void wintw_set_raw_mouse_mode(TermWin *tw, bool activate)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    wgf->send_raw_mouse = activate;
}

static void wintw_set_raw_mouse_mode_pointer(TermWin *tw, bool activate)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    wgf->pointer_indicates_raw_mouse = activate;
    if (wgf != wgf_active) {return;}
    update_mouse_pointer(wgf);
}

/*
 * Print a message box and close the connection.
 */
static void win_seat_connection_fatal(Seat *seat, const char *msg)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);

    if (wgf == wgf_active) {
        int close_on_exit = conf_get_int(wgf->conf, CONF_close_on_exit);
        char *title = dupprintf("%s Fatal Error", appname);
        show_mouseptr(wgf, true);
        if (close_on_exit == AUTO || close_on_exit == FORCE_OFF) {
            char *question = dupprintf("%s\nDo you want to close the session tab?", msg);
            if (MessageBox(frame_hwnd, question, title, MB_ICONERROR | MB_YESNO) == IDYES) {
                wgf->delete_session = true;
            }
            sfree(question);
        } else {
            MessageBox(frame_hwnd, msg, title, MB_ICONERROR | MB_OK);
        }
        sfree(title);
    }
    remote_close(wgf, INT_MAX, msg);
}

/*
 * Report an error at the command-line parsing stage.
 */
void cmdline_error(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    title = dupprintf("%s Command Line Error", appname);
    MessageBox(frame_hwnd, message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    exit(1);
}

/*
 * Actually do the job requested by a WM_NETEVENT
 */
static void wm_netevent_callback(void *vctx)
{
    struct wm_netevent_params *params = (struct wm_netevent_params *)vctx;
    select_result(params->wParam, params->lParam);
    sfree(vctx);
}

static inline rgb rgb_from_colorref(COLORREF cr)
{
    rgb toret;
    toret.r = GetRValue(cr);
    toret.g = GetGValue(cr);
    toret.b = GetBValue(cr);
    return toret;
}

static void wintw_palette_get_overrides(TermWin *tw, Terminal *term)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    Conf *conf = wgf->conf;
    if (conf_get_bool(conf, CONF_system_colour)) {
        rgb rgb;

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOWTEXT));
        term_palette_override(term, OSC4_COLOUR_fg, rgb);
        term_palette_override(term, OSC4_COLOUR_fg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_WINDOW));
        term_palette_override(term, OSC4_COLOUR_bg, rgb);
        term_palette_override(term, OSC4_COLOUR_bg_bold, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHTTEXT));
        term_palette_override(term, OSC4_COLOUR_cursor_fg, rgb);

        rgb = rgb_from_colorref(GetSysColor(COLOR_HIGHLIGHT));
        term_palette_override(term, OSC4_COLOUR_cursor_bg, rgb);
    }
}

/*
 * This is a wrapper to ExtTextOut() to force Windows to display
 * the precise glyphs we give it. Otherwise it would do its own
 * bidi and Arabic shaping, and we would end up uncertain which
 * characters it had put where.
 */
static void exact_textout(HDC hdc, int x, int y, CONST RECT *lprc,
                          unsigned short *lpString, UINT cbCount,
                          CONST INT *lpDx, bool opaque)
{
#ifdef __LCC__
    /*
     * The LCC include files apparently don't supply the
     * GCP_RESULTSW type, but we can make do with GCP_RESULTS
     * proper: the differences aren't important to us (the only
     * variable-width string parameter is one we don't use anyway).
     */
    GCP_RESULTS gcpr;
#else
    GCP_RESULTSW gcpr;
#endif
    char *buffer = snewn(cbCount*2+2, char);
    char *classbuffer = snewn(cbCount, char);
    memset(&gcpr, 0, sizeof(gcpr));
    memset(buffer, 0, cbCount*2+2);
    memset(classbuffer, GCPCLASS_NEUTRAL, cbCount);

    gcpr.lStructSize = sizeof(gcpr);
    gcpr.lpGlyphs = (void *)buffer;
    gcpr.lpClass = (void *)classbuffer;
    gcpr.nGlyphs = cbCount;
    GetCharacterPlacementW(hdc, lpString, cbCount, 0, &gcpr,
                           FLI_MASK | GCP_CLASSIN | GCP_DIACRITIC);

    ExtTextOut(hdc, x, y,
               ETO_GLYPH_INDEX | ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
               lprc, buffer, cbCount, lpDx);
}

/*
 * The exact_textout() wrapper, unfortunately, destroys the useful
 * Windows `font linking' behaviour: automatic handling of Unicode
 * code points not supported in this font by falling back to a font
 * which does contain them. Therefore, we adopt a multi-layered
 * approach: for any potentially-bidi text, we use exact_textout(),
 * and for everything else we use a simple ExtTextOut as we did
 * before exact_textout() was introduced.
 */
static void general_textout(HDC hdc, int x, int y, CONST RECT *lprc,
                            unsigned short *lpString, UINT cbCount,
                            CONST INT *lpDx, bool opaque, bool font_varpitch)
{
    int i, j, xp, xn;
    int bkmode = 0;
    bool got_bkmode = false;

    xp = xn = x;

    for (i = 0; i < (int)cbCount ;) {
        bool rtl = is_rtl(lpString[i]);

        xn += lpDx[i];

        for (j = i+1; j < (int)cbCount; j++) {
            if (rtl != is_rtl(lpString[j]))
                break;
            xn += lpDx[j];
        }

        /*
         * Now [i,j) indicates a maximal substring of lpString
         * which should be displayed using the same textout
         * function.
         */
        if (rtl) {
            exact_textout(hdc, xp, y, lprc, lpString+i, j-i,
                          font_varpitch ? NULL : lpDx+i, opaque);
        } else {
            ExtTextOutW(hdc, xp, y, ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                        lprc, lpString+i, j-i,
                        font_varpitch ? NULL : lpDx+i);
        }

        i = j;
        xp = xn;

        bkmode = GetBkMode(hdc);
        got_bkmode = true;
        SetBkMode(hdc, TRANSPARENT);
        opaque = false;
    }

    if (got_bkmode)
        SetBkMode(hdc, bkmode);
}

static int get_font_width(WinGuiFrontend *wgf, HDC hdc, const TEXTMETRIC *tm)
{
    int ret;
    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm->tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        ret = tm->tmAveCharWidth;
    } else {
#define FIRST '0'
#define LAST '9'
        ABCFLOAT widths[LAST-FIRST + 1];
        int j;

        wgf->font_varpitch = true;
        wgf->font_dualwidth = true;
        if (GetCharABCWidthsFloat(hdc, FIRST, LAST, widths)) {
            ret = 0;
            for (j = 0; j < lenof(widths); j++) {
                int width = (int)(0.5 + widths[j].abcfA +
                                  widths[j].abcfB + widths[j].abcfC);
                if (ret < width)
                    ret = width;
            }
        } else {
            ret = tm->tmMaxCharWidth;
        }
#undef FIRST
#undef LAST
    }
    return ret;
}

static void init_dpi_info(void)
{
    if (dpi_info.x == 0 || dpi_info.y == 0) {
        if (p_GetDpiForMonitor) {
            UINT dpiX, dpiY;
            HMONITOR currentMonitor = MonitorFromWindow(
                frame_hwnd, MONITOR_DEFAULTTOPRIMARY);
            if (p_GetDpiForMonitor(currentMonitor, MDT_EFFECTIVE_DPI,
                                   &dpiX, &dpiY) == S_OK) {
                dpi_info.x = (int)dpiX;
                dpi_info.y = (int)dpiY;
            }
        }

        /* Fall back to system DPI */
        if (dpi_info.x == 0 || dpi_info.y == 0) {
            HDC hdc = GetDC(frame_hwnd);
            dpi_info.x = GetDeviceCaps(hdc, LOGPIXELSX);
            dpi_info.y = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(frame_hwnd, hdc);
        }
    }
}

/*
 * Initialise all the fonts we will need initially. There may be as many as
 * three or as few as one.  The other (potentially) twenty-one fonts are done
 * if/when they are needed.
 *
 * We also:
 *
 * - check the font width and height, correcting our guesses if
 *   necessary.
 *
 * - verify that the bold font is the same width as the ordinary
 *   one, and engage shadow bolding if not.
 *
 * - verify that the underlined font is the same width as the
 *   ordinary one (manual underlining by means of line drawing can
 *   be done in a pinch).
 *
 * - find a trust sigil icon that will look OK with the chosen font.
 */
static void init_fonts(WinGuiFrontend *wgf, int pick_width, int pick_height)
{
    Conf *conf = wgf->conf;
    TEXTMETRIC tm;
    OUTLINETEXTMETRIC otm;
    CPINFO cpinfo;
    FontSpec *font;
    int fontsize[3];
    int i;
    int quality;
    HDC hdc;
    int fw_dontcare, fw_bold;

    for (i = 0; i < FONT_MAXNO; i++)
        wgf->fonts[i] = NULL;

    wgf->bold_font_mode = conf_get_int(conf, CONF_bold_style) & 1 ?
        BOLD_FONT : BOLD_NONE;
    wgf->bold_colours = conf_get_int(conf, CONF_bold_style) & 2 ? true : false;
    wgf->und_mode = UND_FONT;

    font = conf_get_fontspec(conf, CONF_font);
    if (font->isbold) {
        fw_dontcare = FW_BOLD;
        fw_bold = FW_HEAVY;
    } else {
        fw_dontcare = FW_DONTCARE;
        fw_bold = FW_BOLD;
    }

    hdc = GetDC(frame_hwnd);

    if (pick_height)
        wgf->font_height = pick_height;
    else {
        wgf->font_height = font->height;
        if (wgf->font_height > 0) {
            wgf->font_height =
                -MulDiv(wgf->font_height, dpi_info.y, 72);
        }
    }
    wgf->font_width = pick_width;

    quality = conf_get_int(conf, CONF_font_quality);
#define f(i,c,w,u) \
    wgf->fonts[i] = CreateFont (wgf->font_height, wgf->font_width, 0, 0, w, false, u, false, \
                           c, OUT_DEFAULT_PRECIS, \
                           CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality), \
                           FIXED_PITCH | FF_DONTCARE, font->name)

    f(FONT_NORMAL, font->charset, fw_dontcare, false);

    SelectObject(hdc, wgf->fonts[FONT_NORMAL]);
    GetTextMetrics(hdc, &tm);
    if (GetOutlineTextMetrics(hdc, sizeof(otm), &otm))
        wgf->font_strikethrough_y = tm.tmAscent - otm.otmsStrikeoutPosition;
    else
        wgf->font_strikethrough_y = tm.tmAscent - (tm.tmAscent * 3 / 8);

    GetObject(wgf->fonts[FONT_NORMAL], sizeof(LOGFONT), &wgf->lfont);

    /* Note that the TMPF_FIXED_PITCH bit is defined upside down :-( */
    if (!(tm.tmPitchAndFamily & TMPF_FIXED_PITCH)) {
        wgf->font_varpitch = false;
        wgf->font_dualwidth = (tm.tmAveCharWidth != tm.tmMaxCharWidth);
    } else {
        wgf->font_varpitch = true;
        wgf->font_dualwidth = true;
    }
    if (pick_width == 0 || pick_height == 0) {
        wgf->font_height = tm.tmHeight;
        wgf->font_width = get_font_width(wgf, hdc, &tm);
    }

#ifdef RDB_DEBUG_PATCH
    debug("Primary font H=%d, AW=%d, MW=%d\n",
          tm.tmHeight, tm.tmAveCharWidth, tm.tmMaxCharWidth);
#endif

    {
        CHARSETINFO info;
        DWORD cset = tm.tmCharSet;
        memset(&info, 0xFF, sizeof(info));

        /* !!! Yes the next line is right */
        if (cset == OEM_CHARSET)
            wgf->ucsdata.font_codepage = GetOEMCP();
        else
            if (TranslateCharsetInfo ((DWORD *)(ULONG_PTR)cset,
                                      &info, TCI_SRCCHARSET))
                wgf->ucsdata.font_codepage = info.ciACP;
        else
            wgf->ucsdata.font_codepage = -1;

        GetCPInfo(wgf->ucsdata.font_codepage, &cpinfo);
        wgf->ucsdata.dbcs_screenfont = (cpinfo.MaxCharSize > 1);
    }

    f(FONT_UNDERLINE, font->charset, fw_dontcare, true);

    /*
     * Some fonts, e.g. 9-pt Courier, draw their underlines
     * outside their character cell. We successfully prevent
     * screen corruption by clipping the text output, but then
     * we lose the underline completely. Here we try to work
     * out whether this is such a font, and if it is, we set a
     * flag that causes underlines to be drawn by hand.
     *
     * Having tried other more sophisticated approaches (such
     * as examining the TEXTMETRIC structure or requesting the
     * height of a string), I think we'll do this the brute
     * force way: we create a small bitmap, draw an underlined
     * space on it, and test to see whether any pixels are
     * foreground-coloured. (Since we expect the underline to
     * go all the way across the character cell, we only search
     * down a single column of the bitmap, half way across.)
     */
    {
        HDC und_dc;
        HBITMAP und_bm, und_oldbm;
        int i;
        bool gotit;
        COLORREF c;

        und_dc = CreateCompatibleDC(hdc);
        und_bm = CreateCompatibleBitmap(hdc, wgf->font_width, wgf->font_height);
        und_oldbm = SelectObject(und_dc, und_bm);
        SelectObject(und_dc, wgf->fonts[FONT_UNDERLINE]);
        SetTextAlign(und_dc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        SetTextColor(und_dc, RGB(255, 255, 255));
        SetBkColor(und_dc, RGB(0, 0, 0));
        SetBkMode(und_dc, OPAQUE);
        ExtTextOut(und_dc, 0, 0, ETO_OPAQUE, NULL, " ", 1, NULL);
        gotit = false;
        for (i = 0; i < wgf->font_height; i++) {
            c = GetPixel(und_dc, wgf->font_width / 2, i);
            if (c != RGB(0, 0, 0))
                gotit = true;
        }
        SelectObject(und_dc, und_oldbm);
        DeleteObject(und_bm);
        DeleteDC(und_dc);
        if (!gotit) {
            wgf->und_mode = UND_LINE;
            DeleteObject(wgf->fonts[FONT_UNDERLINE]);
            wgf->fonts[FONT_UNDERLINE] = 0;
        }
    }

    if (wgf->bold_font_mode == BOLD_FONT) {
        f(FONT_BOLD, font->charset, fw_bold, false);
    }
#undef f

    wgf->descent = tm.tmAscent + 1;
    if (wgf->descent >= wgf->font_height)
        wgf->descent = wgf->font_height - 1;

    for (i = 0; i < 3; i++) {
        if (wgf->fonts[i]) {
            if (SelectObject(hdc, wgf->fonts[i]) && GetTextMetrics(hdc, &tm))
                fontsize[i] = get_font_width(wgf, hdc, &tm) + 256 * tm.tmHeight;
            else
                fontsize[i] = -i;
        } else
            fontsize[i] = -i;
    }

    ReleaseDC(frame_hwnd, hdc);

    if (wgf->trust_icon != INVALID_HANDLE_VALUE) {
        DestroyIcon(wgf->trust_icon);
    }
    wgf->trust_icon = LoadImage(hinst, MAKEINTRESOURCE(IDI_MAINICON),
                           IMAGE_ICON, wgf->font_width*2, wgf->font_height,
                           LR_DEFAULTCOLOR);

    if (fontsize[FONT_UNDERLINE] != fontsize[FONT_NORMAL]) {
        wgf->und_mode = UND_LINE;
        DeleteObject(wgf->fonts[FONT_UNDERLINE]);
        wgf->fonts[FONT_UNDERLINE] = 0;
    }

    if (wgf->bold_font_mode == BOLD_FONT &&
        fontsize[FONT_BOLD] != fontsize[FONT_NORMAL]) {
        wgf->bold_font_mode = BOLD_SHADOW;
        DeleteObject(wgf->fonts[FONT_BOLD]);
        wgf->fonts[FONT_BOLD] = 0;
    }
    wgf->fontflag[0] = true;
    wgf->fontflag[1] = true;
    wgf->fontflag[2] = true;

    init_ucs(conf, &wgf->ucsdata);

    wgf->font_dpi = dpi_info.y;
}

static void another_font(WinGuiFrontend *wgf, int fontno)
{
    Conf *conf = wgf->conf;
    int basefont;
    int fw_dontcare, fw_bold, quality;
    int c, w, x;
    bool u;
    char *s;
    FontSpec *font;

    if (fontno < 0 || fontno >= FONT_MAXNO || wgf->fontflag[fontno])
        return;

    basefont = (fontno & ~(FONT_BOLDUND));
    if (basefont != fontno && !wgf->fontflag[basefont])
        another_font(wgf, basefont);

    font = conf_get_fontspec(conf, CONF_font);

    if (font->isbold) {
        fw_dontcare = FW_BOLD;
        fw_bold = FW_HEAVY;
    } else {
        fw_dontcare = FW_DONTCARE;
        fw_bold = FW_BOLD;
    }

    c = font->charset;
    w = fw_dontcare;
    u = false;
    s = font->name;
    x = wgf->font_width;

    if (fontno & FONT_WIDE)
        x *= 2;
    if (fontno & FONT_NARROW)
        x = (x+1)/2;
    if (fontno & FONT_OEM)
        c = OEM_CHARSET;
    if (fontno & FONT_BOLD)
        w = fw_bold;
    if (fontno & FONT_UNDERLINE)
        u = true;

    quality = conf_get_int(conf, CONF_font_quality);

    wgf->fonts[fontno] =
        CreateFont(wgf->font_height * (1 + !!(fontno & FONT_HIGH)), x, 0, 0, w,
                   false, u, false, c, OUT_DEFAULT_PRECIS,
                   CLIP_DEFAULT_PRECIS, FONT_QUALITY(quality),
                   DEFAULT_PITCH | FF_DONTCARE, s);

    wgf->fontflag[fontno] = true;
}

static void deinit_fonts(WinGuiFrontend *wgf)
{
    int i;
    for (i = 0; i < FONT_MAXNO; i++) {
        if (wgf->fonts[i])
            DeleteObject(wgf->fonts[i]);
        wgf->fonts[i] = 0;
        wgf->fontflag[i] = false;
    }

    if (wgf->trust_icon != INVALID_HANDLE_VALUE) {
        DestroyIcon(wgf->trust_icon);
    }
    wgf->trust_icon = INVALID_HANDLE_VALUE;
}

static void wintw_request_resize(TermWin *tw, int w, int h)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    const struct BackendVtable *vt;
    int width, height;

    /* If the window is maximized suppress resizing attempts */
    if (IsZoomed(frame_hwnd)) {
        if (conf_get_int(conf, CONF_resize_action) == RESIZE_TERM)
            return;
    }

    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED) return;
    vt = backend_vt_from_proto(be_default_protocol);
    if (vt && vt->flags & BACKEND_RESIZE_FORBIDDEN)
        return;
    if (h == term->rows && w == term->cols) return;

    /* Sanity checks ... */
    {
        switch (wgf->request_resize.first_time) {
          case 1:
            /* Get the size of the screen */
            if (get_fullscreen_rect(&wgf->request_resize.ss))
                /* first_time = 0 */ ;
            else {
                wgf->request_resize.first_time = 2;
                break;
            }
          case 0:
            /* Make sure the values are sane */
            width = (wgf->request_resize.ss.right - wgf->request_resize.ss.left - extra_width) / 4;
            height = (wgf->request_resize.ss.bottom - wgf->request_resize.ss.top - extra_height) / 6;

            if (w > width || h > height)
                return;
            if (w < 15)
                w = 15;
            if (h < 1)
                h = 1;
        }
    }

    term_size(term, h, w, conf_get_int(conf, CONF_savelines));

    if (wgf != wgf_active) {return;}
    if (conf_get_int(conf, CONF_resize_action) != RESIZE_FONT &&
        !IsZoomed(frame_hwnd)) {
        width = extra_width + wgf->font_width * w;
        height = extra_height + wgf->font_height * h;

        SetWindowPos(frame_hwnd, NULL, 0, 0, width, height,
            SWP_NOACTIVATE | SWP_NOCOPYBITS |
            SWP_NOMOVE | SWP_NOZORDER);
    } else
        reset_window(wgf, 0);

    InvalidateRect(term_hwnd, NULL, true);
}

static void recompute_window_offset(WinGuiFrontend *wgf)
{
    Terminal *term = wgf->term;
    RECT cr;
    GetClientRect(frame_hwnd, &cr);

    int win_width  = cr.right - cr.left;
    int win_height = cr.bottom - cr.top;
    adjust_client_size(&win_width, &win_height);

    int new_offset_width = (win_width-wgf->font_width*term->cols)/2;
    int new_offset_height = (win_height-wgf->font_height*term->rows)/2;

    if (wgf->offset_width != new_offset_width ||
        wgf->offset_height != new_offset_height) {
        wgf->offset_width = new_offset_width;
        wgf->offset_height = new_offset_height;
        InvalidateRect(term_hwnd, NULL, true);
    }
}

static void reset_window(WinGuiFrontend *wgf, int reinit) {
    /*
     * This function decides how to resize or redraw when the
     * user changes something.
     *
     * This function doesn't like to change the terminal size but if the
     * font size is locked that may be it's only soluion.
     */
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    int win_width, win_height, resize_action, window_border;
    RECT cr, wr;

#ifdef RDB_DEBUG_PATCH
    debug("reset_window()\n");
#endif

    /* Current window sizes ... */
    GetWindowRect(frame_hwnd, &wr);
    GetClientRect(frame_hwnd, &cr);

    win_width  = cr.right - cr.left;
    win_height = cr.bottom - cr.top;
    adjust_client_size(&win_width, &win_height);

    resize_action = conf_get_int(conf, CONF_resize_action);
    window_border = conf_get_int(conf, CONF_window_border);

    if (resize_action == RESIZE_DISABLED)
        reinit = 2;

    /* Are we being forced to reload the fonts ? */
    if (reinit>1) {
#ifdef RDB_DEBUG_PATCH
        debug("reset_window() -- Forced deinit\n");
#endif
        deinit_fonts(wgf);
        init_fonts(wgf, 0,0);
    }

    /* Oh, looks like we're minimised */
    if (win_width == 0 || win_height == 0)
        return;

    /* Is the window out of position ? */
    if (!reinit) {
        recompute_window_offset(wgf);
#ifdef RDB_DEBUG_PATCH
        debug("reset_window() -> Reposition terminal\n");
#endif
    }

    if (IsZoomed(frame_hwnd)) {
        /* We're fullscreen, this means we must not change the size of
         * the window so it's the font size or the terminal itself.
         */

        extra_width = wr.right - wr.left - cr.right + cr.left;
        extra_height = wr.bottom - wr.top - cr.bottom + cr.top;
        adjust_extra_size();

        if (resize_action != RESIZE_TERM) {
            if (wgf->font_width != win_width/term->cols ||
                wgf->font_height != win_height/term->rows) {
                deinit_fonts(wgf);
                init_fonts(wgf, win_width/term->cols, win_height/term->rows);
                wgf->offset_width = (win_width-wgf->font_width*term->cols)/2;
                wgf->offset_height = (win_height-wgf->font_height*term->rows)/2;
                InvalidateRect(term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
                debug("reset_window() -> Z font resize to (%d, %d)\n",
                      wgf->font_width, wgf->font_height);
#endif
            }
        } else {
            if (wgf->font_width * term->cols != win_width ||
                wgf->font_height * term->rows != win_height) {
                /* Our only choice at this point is to change the
                 * size of the terminal; Oh well.
                 */
                term_size(term, win_height/wgf->font_height, win_width/wgf->font_width,
                          conf_get_int(conf, CONF_savelines));
                wgf->offset_width = (win_width-wgf->font_width*term->cols)/2;
                wgf->offset_height = (win_height-wgf->font_height*term->rows)/2;
                InvalidateRect(term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
                debug("reset_window() -> Zoomed term_size\n");
#endif
            }
        }
        return;
    }

    /* Resize window after DPI change */
    if (reinit == 3 && p_GetSystemMetricsForDpi && p_AdjustWindowRectExForDpi) {
        RECT rect;
        rect.left = rect.top = 0;
        rect.right = (wgf->font_width * term->cols);
        if (conf_get_bool(conf, CONF_scrollbar))
            rect.right += p_GetSystemMetricsForDpi(SM_CXVSCROLL,
                                                   dpi_info.x);
        rect.bottom = (wgf->font_height * term->rows);
        rect.right += tab_bar_get_extra_width();
        rect.bottom += tab_bar_get_extra_height();
        p_AdjustWindowRectExForDpi(
            &rect, GetWindowLongPtr(frame_hwnd, GWL_STYLE),
            FALSE, GetWindowLongPtr(frame_hwnd, GWL_EXSTYLE),
            dpi_info.x);
        rect.right += (window_border * 2);
        rect.bottom += (window_border * 2);
        OffsetRect(&dpi_changed_new_wnd_rect,
            ((dpi_changed_new_wnd_rect.right - dpi_changed_new_wnd_rect.left) -
             (rect.right - rect.left)) / 2,
            ((dpi_changed_new_wnd_rect.bottom - dpi_changed_new_wnd_rect.top) -
             (rect.bottom - rect.top)) / 2);
        SetWindowPos(frame_hwnd, NULL,
                     dpi_changed_new_wnd_rect.left, dpi_changed_new_wnd_rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER);

        InvalidateRect(term_hwnd, NULL, true);
        return;
    }

    /* Hmm, a force re-init means we should ignore the current window
     * so we resize to the default font size.
     */
    if (reinit>0) {
#ifdef RDB_DEBUG_PATCH
        debug("reset_window() -> Forced re-init\n");
#endif

        wgf->offset_width = wgf->offset_height = window_border;
        extra_width = wr.right - wr.left - cr.right + cr.left + wgf->offset_width*2;
        extra_height = wr.bottom - wr.top - cr.bottom + cr.top +wgf->offset_height*2;
        adjust_extra_size();

        if (win_width != wgf->font_width*term->cols + wgf->offset_width*2 ||
            win_height != wgf->font_height*term->rows + wgf->offset_height*2) {

            /* If this is too large windows will resize it to the maximum
             * allowed window size, we will then be back in here and resize
             * the font or terminal to fit.
             */
            SetWindowPos(frame_hwnd, NULL, 0, 0,
                         wgf->font_width*term->cols + extra_width,
                         wgf->font_height*term->rows + extra_height,
                         SWP_NOMOVE | SWP_NOZORDER);
        }

        InvalidateRect(term_hwnd, NULL, true);
        return;
    }

    /* Okay the user doesn't want us to change the font so we try the
     * window. But that may be too big for the screen which forces us
     * to change the terminal.
     */
    if ((resize_action == RESIZE_TERM && reinit<=0) ||
        (resize_action == RESIZE_EITHER && reinit<0) ||
            reinit>0) {
        wgf->offset_width = wgf->offset_height = window_border;
        extra_width = wr.right - wr.left - cr.right + cr.left + wgf->offset_width*2;
        extra_height = wr.bottom - wr.top - cr.bottom + cr.top +wgf->offset_height*2;
        adjust_extra_size();

        if (win_width != wgf->font_width*term->cols + wgf->offset_width*2 ||
            win_height != wgf->font_height*term->rows + wgf->offset_height*2) {

            static RECT ss;
            int width, height;

                get_fullscreen_rect(&ss);

            width = (ss.right - ss.left - extra_width) / wgf->font_width;
            height = (ss.bottom - ss.top - extra_height) / wgf->font_height;

            /* Grrr too big */
            if ( term->rows > height || term->cols > width ) {
                if (resize_action == RESIZE_EITHER) {
                    /* Make the font the biggest we can */
                    if (term->cols > width)
                        wgf->font_width = (ss.right - ss.left - extra_width)
                            / term->cols;
                    if (term->rows > height)
                        wgf->font_height = (ss.bottom - ss.top - extra_height)
                            / term->rows;

                    deinit_fonts(wgf);
                    init_fonts(wgf, wgf->font_width, wgf->font_height);

                    width = (ss.right - ss.left - extra_width) / wgf->font_width;
                    height = (ss.bottom - ss.top - extra_height) / wgf->font_height;
                } else {
                    if ( height > term->rows ) height = term->rows;
                    if ( width > term->cols )  width = term->cols;
                    term_size(term, height, width,
                              conf_get_int(conf, CONF_savelines));
#ifdef RDB_DEBUG_PATCH
                    debug("reset_window() -> term resize to (%d,%d)\n",
                          height, width);
#endif
                }
            }

            SetWindowPos(frame_hwnd, NULL, 0, 0,
                         wgf->font_width*term->cols + extra_width,
                         wgf->font_height*term->rows + extra_height,
                         SWP_NOMOVE | SWP_NOZORDER);

            InvalidateRect(term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
            debug("reset_window() -> window resize to (%d,%d)\n",
                  wgf->font_width*term->cols + extra_width,
                  wgf->font_height*term->rows + extra_height);
#endif
        }
        return;
    }

    /* We're allowed to or must change the font but do we want to ?  */

    if (wgf->font_width != (win_width-window_border*2)/term->cols ||
        wgf->font_height != (win_height-window_border*2)/term->rows) {

        deinit_fonts(wgf);
        init_fonts(wgf, (win_width-window_border*2)/term->cols,
                   (win_height-window_border*2)/term->rows);
        wgf->offset_width = (win_width-wgf->font_width*term->cols)/2;
        wgf->offset_height = (win_height-wgf->font_height*term->rows)/2;

        extra_width = wr.right - wr.left - cr.right + cr.left +wgf->offset_width*2;
        extra_height = wr.bottom - wr.top - cr.bottom + cr.top+wgf->offset_height*2;
        adjust_extra_size();

        InvalidateRect(term_hwnd, NULL, true);
#ifdef RDB_DEBUG_PATCH
        debug("reset_window() -> font resize to (%d,%d)\n",
              wgf->font_width, wgf->font_height);
#endif
    }
}

static void set_input_locale(HKL kl)
{
    char lbuf[20];

    GetLocaleInfo(LOWORD(kl), LOCALE_IDEFAULTANSICODEPAGE,
                  lbuf, sizeof(lbuf));

    kbd_codepage = atoi(lbuf);
}

static void click(WinGuiFrontend *wgf, Mouse_Button b, int x, int y,
                  bool shift, bool ctrl, bool alt)
{
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    int thistime = GetMessageTime();

    if (wgf->send_raw_mouse &&
        !(shift && conf_get_bool(conf, CONF_mouse_override))) {
        lastbtn = MBT_NOTHING;
        term_mouse(term, b, translate_button(conf, b), MA_CLICK,
                   x, y, shift, ctrl, alt);
        return;
    }

    if (lastbtn == b && thistime - lasttime < dbltime) {
        lastact = (lastact == MA_CLICK ? MA_2CLK :
                   lastact == MA_2CLK ? MA_3CLK :
                   lastact == MA_3CLK ? MA_CLICK : MA_NOTHING);
    } else {
        lastbtn = b;
        lastact = MA_CLICK;
    }
    if (lastact != MA_NOTHING)
        term_mouse(term, b, translate_button(conf, b), lastact,
                   x, y, shift, ctrl, alt);
    lasttime = thistime;
}

/*
 * Translate a raw mouse button designation (LEFT, MIDDLE, RIGHT)
 * into a cooked one (SELECT, EXTEND, PASTE).
 */
static Mouse_Button translate_button(Conf *conf, Mouse_Button button)
{
    if (button == MBT_LEFT)
        return MBT_SELECT;
    if (button == MBT_MIDDLE)
        return conf_get_int(conf, CONF_mouse_is_xterm) == 1 ?
        MBT_PASTE : MBT_EXTEND;
    if (button == MBT_RIGHT)
        return conf_get_int(conf, CONF_mouse_is_xterm) == 1 ?
        MBT_EXTEND : MBT_PASTE;
    return 0;                          /* shouldn't happen */
}

static void show_mouseptr(WinGuiFrontend *wgf, bool show)
{
    static bool cursor_visible = true;
    if (!conf_get_bool(wgf->conf, CONF_hide_mouseptr))
        show = true;                   /* override if this feature disabled */
    wgf->cursor_visible = show;
    if (wgf->cursor_forced_visible)
        show = true;
    if (cursor_visible && !show)
        ShowCursor(false);
    else if (!cursor_visible && show)
        ShowCursor(true);
    cursor_visible = show;
}

static bool is_alt_pressed(void)
{
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

static bool resizing;

static void win_seat_notify_remote_exit(Seat *seat)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Conf *conf = wgf->conf;
    int exitcode, close_on_exit;

    if (!wgf->remote_closed &&
        (exitcode = backend_exitcode(wgf->backend)) >= 0) {
        close_on_exit = conf_get_int(conf, CONF_close_on_exit);
        /* Abnormal exits will already have set session_closed and taken
         * appropriate action. */

        if (close_on_exit == FORCE_ON ||
            (close_on_exit == AUTO && exitcode != INT_MAX)) {
        } else {
            /* exitcode == INT_MAX indicates that the connection was closed
             * by a fatal error, so an error box will be coming our way and
             * we should not generate this informational one. */
            if (exitcode != INT_MAX && wgf == wgf_active) {
                show_mouseptr(wgf, true);
                if (MessageBox(frame_hwnd, "Connection closed by remote host\nDo you want to close the session tab?",
                       appname, MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                    wgf->delete_session = true;
                }
            }
        }
        remote_close(wgf, exitcode, "Connection closed by remote host");
    }
}

void timer_change_notify(unsigned long next)
{
    unsigned long now = GETTICKCOUNT();
    long ticks;
    if (now - next < INT_MAX)
        ticks = 0;
    else
        ticks = next - now;
    KillTimer(frame_hwnd, TIMING_TIMER_ID);
    SetTimer(frame_hwnd, TIMING_TIMER_ID, ticks, NULL);
    timing_next_time = next;
}

static void conf_cache_data(WinGuiFrontend *wgf)
{
    /* Cache some items from conf to speed lookups in very hot code */
    Conf *conf = wgf->conf;
    wgf->cursor_type = conf_get_int(conf, CONF_cursor_type);
    wgf->vtmode = conf_get_int(conf, CONF_vtmode);
}

static const int clips_system[] = { CLIP_SYSTEM };

static HDC make_hdc(WinGuiFrontend *wgf)
{
    HDC hdc;

    if (!term_hwnd)
        return NULL;

    hdc = GetDC(term_hwnd);
    if (!hdc)
        return NULL;

    SelectPalette(hdc, wgf->pal, false);
    return hdc;
}

static void free_hdc(HWND hwnd, HDC hdc)
{
    assert(hwnd);
    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), false);
    ReleaseDC(hwnd, hdc);
}

static void wm_size_resize_term(WinGuiFrontend *wgf, LPARAM lParam, bool border)
{
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    int width = LOWORD(lParam);
    int height = HIWORD(lParam);
    int border_size = border ? conf_get_int(conf, CONF_window_border) : 0;
    adjust_client_size(&width, &height);

    int w = (width - border_size*2) / wgf->font_width;
    int h = (height - border_size*2) / wgf->font_height;

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (resizing) {
        /*
         * If we're in the middle of an interactive resize, we don't
         * call term_size. This means that, firstly, the user can drag
         * the size back and forth indecisively without wiping out any
         * actual terminal contents, and secondly, the Terminal
         * doesn't call back->size in turn for each increment of the
         * resizing drag, so we don't spam the server with huge
         * numbers of resize events.
         */
        wgf->need_backend_resize = true;
        conf_set_int(conf, CONF_height, h);
        conf_set_int(conf, CONF_width, w);
    } else {
        if (term->cols != w || term->rows != h) {
            term_size(term, h, w, conf_get_int(conf, CONF_savelines));
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
                                WPARAM wParam, LPARAM lParam)
{
    Conf *conf = NULL;
    Terminal *term = NULL;
    Backend *backend = NULL;
    Ldisc *ldisc = NULL;
    bool is_term_hwnd = (frame_hwnd != NULL && hwnd != frame_hwnd);
    WinGuiFrontend *wgf = wgf_active;
    if (wgf) {
        conf = wgf->conf;
        term = wgf->term;
        backend = wgf->backend;
        ldisc = wgf->ldisc;
    }
    HDC hdc;
    static bool fullscr_on_max = false;
    static bool processed_resize = false;
    static bool in_scrollbar_loop = false;
    static UINT last_mousemove = 0;
    int resize_action;

    switch (message) {
      case WM_DRAWITEM:
        if (is_term_hwnd) {
          break;
        }
        return SendMessage(((DRAWITEMSTRUCT *)lParam)->hwndItem, WM_DRAWITEM, wParam, lParam);
      case WM_NOTIFY:
        if (is_term_hwnd) {
          break;
        }
        handle_wm_notify(lParam);
        break;
      case WM_INITMENU:
        handle_wm_initmenu(wParam);
        break;
      case WM_TIMER:
        if (is_term_hwnd) {
            return 0;
        }
        if ((UINT_PTR)wParam == TIMING_TIMER_ID) {
            unsigned long next;

            KillTimer(hwnd, TIMING_TIMER_ID);
            if (run_timers(timing_next_time, &next)) {
                timer_change_notify(next);
            } else {
            }
        }
        return 0;
      case WM_CLOSE: {
        if (is_term_hwnd) {
            return 0;
        }
        show_mouseptr(wgf, true);
        char *title = dupprintf("%s Exit Confirmation", appname);
        int ret = MessageBox(hwnd, "Are you sure you want to close All sessions?", title,
                       MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1);
        sfree(title);
        if (ret != IDOK) {
            return 0;
        }
        SetFocus(NULL);
        for (int i=0; i<pointer_array_size(); i++) {
            WinGuiFrontend *wgf = (WinGuiFrontend *)pointer_array_get(i);
            if (wgf->backend) {
                stop_backend(wgf);
            }
            if (wgf->remote_closed) {
                delete_callbacks_for_context(wgf);
            }
            destroy_frontend(wgf);
        }
        wgf_active = NULL;
        DestroyWindow(hwnd);
        return 0;
      }
      case WM_DESTROY:
        if (is_term_hwnd) {
            return 0;
        }
        DestroyWindow(term_hwnd);
        destroy_tab_bar();
        pointer_array_reset(NULL);
        PostQuitMessage(0);
        return 0;
      case WM_INITMENUPOPUP:
        if ((HMENU)wParam == savedsess_menu) {
            /* About to pop up Saved Sessions sub-menu.
             * Refresh the session list. */
            get_sesslist(&sesslist, false); /* free */
            get_sesslist(&sesslist, true);
            update_savedsess_menu();
            return 0;
        }
        break;
      case WM_COMMAND:
      case WM_SYSCOMMAND:
        if (is_term_hwnd) {
            bool process_by_term_window = false;
            switch (wParam & ~0xF) {
              case IDM_COPY:
              case IDM_PASTE:
                process_by_term_window = true;
                break;
              default:
                SendMessage(frame_hwnd, message, wParam, lParam);
                break;
            }
            if (!process_by_term_window) {
                break;
            }
        }
        else {
            bool process_by_frame_window = true;
            switch (wParam & ~0xF) {
              case IDM_COPY:
              case IDM_PASTE:
                process_by_frame_window = false;
                break;
              default:
                break;
            }
            if (!process_by_frame_window) {
                break;
            }
        }
        switch (wParam & ~0xF) {       /* low 4 bits reserved to Windows */
          case SC_VSCROLL:
          case SC_HSCROLL:
            if (message == WM_SYSCOMMAND) {
                /* As per the long comment in WM_VSCROLL handler: give
                 * this message the default handling, which starts a
                 * subsidiary message loop, but set a flag so that
                 * when we're re-entered from that loop, scroll events
                 * within an interactive scrollbar-drag can be handled
                 * differently. */
                in_scrollbar_loop = true;
                LRESULT result = DefWindowProcW(hwnd, message, wParam, lParam);
                in_scrollbar_loop = false;
                return result;
            }
            break;
          case IDM_CLOSESESS: {
            struct TBHDR nmhdr;
            nmhdr._hdr.hwndFrom = hwnd;
            nmhdr._hdr.code = TCN_TABDELETE;
            nmhdr._hdr.idFrom = 0;
            nmhdr._tabOrigin = wgf->tab_index;
            SendMessage(hwnd, WM_NOTIFY, 0, (LPARAM)(&nmhdr));
            break;
          }
          case IDM_EXIT:
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            break;
          case IDM_SHOWLOG:
            showeventlog(hwnd, &wgf->eventlogstuff);
            break;
          case IDM_CONFIRM_PASTE:
            confirm_paste = !confirm_paste;
            check_menu_item(IDM_CONFIRM_PASTE, (confirm_paste ? MF_CHECKED : MF_UNCHECKED));
            break;
          case IDM_NEWSESS: {
            Conf *conf = NULL;
            const char *session_name = NULL;
            if (create_conf(NULL, &conf, &session_name)) {
                add_session(conf, session_name, pointer_array_size());
            }
            break;
          }
          case IDM_DUPSESS: {
            Conf *conf = conf_copy(wgf->conf);
            const char *session_name = dupstr(wgf->session_name);
            add_session(conf, session_name, wgf->tab_index+1);
            break;
          }
          case IDM_SAVEDSESS: {
            unsigned int sessno = ((lParam - IDM_SAVED_MIN) / MENU_SAVED_STEP) + 1;
            if (sessno >= (unsigned)sesslist.nsessions) {
                break;
            }
            Conf *conf = NULL;
            const char *session_name = NULL;
            if (create_conf(sesslist.sessions[sessno], &conf, &session_name)) {
                add_session(conf, session_name, pointer_array_size());
            }
            break;
          }
          case IDM_DUPSESS_NEW: {
            char b[2048];
            char *cl;
            const char *argprefix;
            bool inherit_handles;
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            HANDLE filemap = NULL;

            if (restricted_acl())
                argprefix = "&R";
            else
                argprefix = "";

              /*
               * Allocate a file-mapping memory chunk for the
               * config structure.
               */
              SECURITY_ATTRIBUTES sa;
              strbuf *serbuf;
              void *p;
              int size;

              serbuf = strbuf_new();
              conf_serialise(BinarySink_UPCAST(serbuf), conf);
              size = serbuf->len;

              sa.nLength = sizeof(sa);
              sa.lpSecurityDescriptor = NULL;
              sa.bInheritHandle = true;
              filemap = CreateFileMapping(INVALID_HANDLE_VALUE,
                                          &sa,
                                          PAGE_READWRITE,
                                          0, size, NULL);
              if (filemap && filemap != INVALID_HANDLE_VALUE) {
                p = MapViewOfFile(filemap, FILE_MAP_WRITE, 0, 0, size);
                if (p) {
                  memcpy(p, serbuf->s, size);
                  UnmapViewOfFile(p);
                }
              }

              strbuf_free(serbuf);
              inherit_handles = true;
              cl = dupprintf("putty %s&%p:%u:%s", argprefix,
                             filemap, (unsigned)size, wgf->session_name);

            GetModuleFileName(NULL, b, sizeof(b) - 1);
            si.cb = sizeof(si);
            si.lpReserved = NULL;
            si.lpDesktop = NULL;
            si.lpTitle = NULL;
            si.dwFlags = 0;
            si.cbReserved2 = 0;
            si.lpReserved2 = NULL;
            CreateProcess(b, cl, NULL, NULL, inherit_handles,
                          NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            if (filemap)
                CloseHandle(filemap);
            sfree(cl);
            break;
          }
          case IDM_RESTART:
            if (!backend) {
                lp_eventlog(&wgf->logpolicy, "----- Session restarted -----");
                term_pwron(term, false);
                start_backend(wgf);
                if (wgf->backend) {
                    tab_bar_set_tab_unusable(wgf->tab_index, false);
                }
            }

            break;
          case IDM_RECONF: {
            Conf *prev_conf;
            int init_lvl = 1;
            bool reconfig_result;

            term_pre_reconfig(term, conf);
            prev_conf = conf_copy(conf);

            const char *session_name = dupstr(wgf->session_name);
            reconfig_result = do_reconfig(frame_hwnd,
                conf, &session_name, backend ? backend_cfg_info(backend) : 0);
            if (!reconfig_result) {
              conf_free(prev_conf);
              // do_reconfig will free session_name if cancelled.
              break;
            }
            if (strcmp(session_name, wgf->session_name) == 0) {
                sfree((char *)session_name);
            } else {
                char *tab_title = create_tab_title(wgf->session_id, session_name);
                tab_bar_set_tab_title(wgf->tab_index, tab_title);
                sfree(tab_title);
                sfree((char *) wgf->session_name);
                wgf->session_name = session_name;
            }

            conf_cache_data(wgf);

            resize_action = conf_get_int(conf, CONF_resize_action);
            {
              /* Gracefully unzoom if necessary */
              if (IsZoomed(hwnd) && (resize_action == RESIZE_DISABLED))
                  ShowWindow(hwnd, SW_RESTORE);
            }

            /* Pass new config data to the logging module */
            log_reconfig(wgf->logctx, conf);

            sfree(wgf->logpal);
            /*
             * Flush the line discipline's edit buffer in the
             * case where local editing has just been disabled.
             */
            if (ldisc) {
              ldisc_configure(ldisc, conf);
              ldisc_echoedit_update(ldisc);
            }

            if (conf_get_bool(conf, CONF_system_colour) !=
                conf_get_bool(prev_conf, CONF_system_colour))
                term_notify_palette_changed(term);

            /* Pass new config data to the terminal */
            term_reconfig(term, conf);
            setup_clipboards(term, conf);

            /* Reinitialise the colour palette, in case the terminal
             * just read new settings out of Conf */
            if (wgf->pal)
                DeleteObject(wgf->pal);
            wgf->logpal = NULL;
            wgf->pal = NULL;
            init_palette(wgf);

            /* Pass new config data to the back end */
            if (backend)
                backend_reconfig(backend, conf);

            /* Screen size changed ? */
            if (conf_get_int(conf, CONF_height) !=
                conf_get_int(prev_conf, CONF_height) ||
                conf_get_int(conf, CONF_width) !=
                conf_get_int(prev_conf, CONF_width) ||
                conf_get_int(conf, CONF_savelines) !=
                conf_get_int(prev_conf, CONF_savelines) ||
                resize_action == RESIZE_FONT ||
                (resize_action == RESIZE_EITHER && IsZoomed(hwnd)) ||
                resize_action == RESIZE_DISABLED)
                term_size(term, conf_get_int(conf, CONF_height),
                          conf_get_int(conf, CONF_width),
                          conf_get_int(conf, CONF_savelines));

            /* Enable or disable the scroll bar, etc */
            {
                if (set_frame_style(conf)) {
                    init_lvl = 2;
                }
            }

            /* Oops */
            if (resize_action == RESIZE_DISABLED && IsZoomed(hwnd)) {
              force_normal(hwnd);
              init_lvl = 2;
            }

            {
              FontSpec *font = conf_get_fontspec(conf, CONF_font);
              FontSpec *prev_font = conf_get_fontspec(prev_conf,
                                                      CONF_font);

              if (!strcmp(font->name, prev_font->name) ||
                  !strcmp(conf_get_str(conf, CONF_line_codepage),
                          conf_get_str(prev_conf, CONF_line_codepage)) ||
                  font->isbold != prev_font->isbold ||
                  font->height != prev_font->height ||
                  font->charset != prev_font->charset ||
                  conf_get_int(conf, CONF_font_quality) !=
                  conf_get_int(prev_conf, CONF_font_quality) ||
                  conf_get_int(conf, CONF_vtmode) !=
                  conf_get_int(prev_conf, CONF_vtmode) ||
                  conf_get_int(conf, CONF_bold_style) !=
                  conf_get_int(prev_conf, CONF_bold_style)) {
                  init_lvl = 2;
                  wgf->resize_either.font_width = 0;
                  wgf->resize_either.font_height = 0;
              }
              if (resize_action == RESIZE_DISABLED ||
                  resize_action == RESIZE_EITHER ||
                  resize_action != conf_get_int(prev_conf,
                                                CONF_resize_action))
                  init_lvl = 2;
            }

            InvalidateRect(hwnd, NULL, true);
            reset_window(wgf, init_lvl);

            conf_free(prev_conf);
            break;
          }
          case IDM_COPYALL:
            term_copyall(term, clips_system, lenof(clips_system));
            break;
          case IDM_COPY:
            term_request_copy(term, clips_system, lenof(clips_system));
            break;
          case IDM_PASTE:
            term_request_paste(term, CLIP_SYSTEM);
            break;
          case IDM_CLRSB:
            term_clrsb(term);
            break;
          case IDM_RESET:
            term_pwron(term, true);
            if (ldisc)
                ldisc_echoedit_update(ldisc);
            break;
          case IDM_ABOUT:
            showabout(hwnd);
            break;
          case IDM_HELP:
            launch_help(hwnd, NULL);
            break;
          case SC_MOUSEMENU:
            /*
             * We get this if the System menu has been activated
             * using the mouse.
             */
            show_mouseptr(wgf, true);
            break;
          case SC_KEYMENU:
            /*
             * We get this if the System menu has been activated
             * using the keyboard. This might happen from within
             * TranslateKey, in which case it really wants to be
             * followed by a `space' character to actually _bring
             * the menu up_ rather than just sitting there in
             * `ready to appear' state.
             */
            show_mouseptr(wgf, true);    /* make sure pointer is visible */
            if( lParam == 0 )
                PostMessage(hwnd, WM_CHAR, ' ', 0);
            break;
          case IDM_FULLSCREEN:
            flip_full_screen();
            break;
          default:
            if (wParam >= IDM_SAVED_MIN && wParam < IDM_SAVED_MAX) {
                SendMessage(hwnd, WM_SYSCOMMAND, IDM_SAVEDSESS, wParam);
            }
            if (wParam >= IDM_SPECIAL_MIN && wParam <= IDM_SPECIAL_MAX) {
                int i = (wParam - IDM_SPECIAL_MIN) / 0x10;
                /*
                 * Ensure we haven't been sent a bogus SYSCOMMAND
                 * which would cause us to reference invalid memory
                 * and crash. Perhaps I'm just too paranoid here.
                 */
                if (i >= wgf->n_specials)
                    break;
                if (backend)
                    backend_special(
                        backend, wgf->specials[i].code, wgf->specials[i].arg);
            }
        }
        break;

#define X_POS(l) ((int)(short)LOWORD(l))
#define Y_POS(l) ((int)(short)HIWORD(l))

#define TO_CHR_X(x) ((((x)<0 ? (x)-wgf->font_width+1 : (x))-wgf->offset_width) / wgf->font_width)
#define TO_CHR_Y(y) ((((y)<0 ? (y)-wgf->font_height+1: (y))-wgf->offset_height) / wgf->font_height)
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:
        if (!is_term_hwnd) {
          return 0;
        }
        if (message == WM_RBUTTONDOWN &&
            ((wParam & MK_CONTROL) ||
             (conf_get_int(conf, CONF_mouse_is_xterm) == 2))) {
            POINT cursorpos;

            show_mouseptr(wgf, true);    /* make sure pointer is visible */
            GetCursorPos(&cursorpos);
            TrackPopupMenu(popup_menus[CTXMENU].menu,
                           TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                           cursorpos.x, cursorpos.y,
                           0, hwnd, NULL);
            break;
        }
        {
            int button;
            bool press;

            switch (message) {
              case WM_LBUTTONDOWN:
                button = MBT_LEFT;
                wParam |= MK_LBUTTON;
                press = true;
                break;
              case WM_MBUTTONDOWN:
                button = MBT_MIDDLE;
                wParam |= MK_MBUTTON;
                press = true;
                break;
              case WM_RBUTTONDOWN:
                button = MBT_RIGHT;
                wParam |= MK_RBUTTON;
                press = true;
                break;
              case WM_LBUTTONUP:
                button = MBT_LEFT;
                wParam &= ~MK_LBUTTON;
                press = false;
                break;
              case WM_MBUTTONUP:
                button = MBT_MIDDLE;
                wParam &= ~MK_MBUTTON;
                press = false;
                break;
              case WM_RBUTTONUP:
                button = MBT_RIGHT;
                wParam &= ~MK_RBUTTON;
                press = false;
                break;
              default: /* shouldn't happen */
                button = 0;
                press = false;
            }
            show_mouseptr(wgf, true);
            /*
             * Special case: in full-screen mode, if the left
             * button is clicked in the very top left corner of the
             * window, we put up the System menu instead of doing
             * selection.
             */
            {
                bool mouse_on_hotspot = false;
                POINT pt;

                GetCursorPos(&pt);
#ifndef NO_MULTIMON
                {
                    HMONITOR mon;
                    MONITORINFO mi;

                    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);

                    if (mon != NULL) {
                        mi.cbSize = sizeof(MONITORINFO);
                        GetMonitorInfo(mon, &mi);

                        if (mi.rcMonitor.left == pt.x &&
                            mi.rcMonitor.top == pt.y) {
                            mouse_on_hotspot = true;
                        }
                    }
                }
#else
                if (pt.x == 0 && pt.y == 0) {
                    mouse_on_hotspot = true;
                }
#endif
                if (is_full_screen() && press &&
                    button == MBT_LEFT && mouse_on_hotspot) {
                    SendMessage(hwnd, WM_SYSCOMMAND, SC_MOUSEMENU,
                                MAKELPARAM(pt.x, pt.y));
                    return 0;
                }
            }

            if (press) {
                click(wgf, button,
                      TO_CHR_X(X_POS(lParam)), TO_CHR_Y(Y_POS(lParam)),
                      wParam & MK_SHIFT, wParam & MK_CONTROL,
                      is_alt_pressed());
                SetCapture(hwnd);
            } else {
                term_mouse(term, button, translate_button(conf, button), MA_RELEASE,
                           TO_CHR_X(X_POS(lParam)),
                           TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
                           wParam & MK_CONTROL, is_alt_pressed());
                if (!(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
                    ReleaseCapture();
            }
        }
        return 0;
      case WM_MOUSEMOVE: {
        if (!is_term_hwnd) {
          return 0;
        }
        /*
         * Windows seems to like to occasionally send MOUSEMOVE
         * events even if the mouse hasn't moved. Don't unhide
         * the mouse pointer in this case.
         */
        static WPARAM wp = 0;
        static LPARAM lp = 0;
        if (wParam != wp || lParam != lp ||
            last_mousemove != WM_MOUSEMOVE) {
          show_mouseptr(wgf, true);
          wp = wParam; lp = lParam;
          last_mousemove = WM_MOUSEMOVE;
        }
        /*
         * Add the mouse position and message time to the random
         * number noise.
         */
        noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);

        if (wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON) &&
            GetCapture() == hwnd) {
            Mouse_Button b;
            if (wParam & MK_LBUTTON)
                b = MBT_LEFT;
            else if (wParam & MK_MBUTTON)
                b = MBT_MIDDLE;
            else
                b = MBT_RIGHT;
            term_mouse(term, b, translate_button(conf, b), MA_DRAG,
                       TO_CHR_X(X_POS(lParam)),
                       TO_CHR_Y(Y_POS(lParam)), wParam & MK_SHIFT,
                       wParam & MK_CONTROL, is_alt_pressed());
        }
        return 0;
      }
      case WM_NCMOUSEMOVE: {
        static WPARAM wp = 0;
        static LPARAM lp = 0;
        if (is_term_hwnd) {
          return 0;
        }
        if (wParam != wp || lParam != lp ||
            last_mousemove != WM_NCMOUSEMOVE) {
          show_mouseptr(wgf, true);
          wp = wParam; lp = lParam;
          last_mousemove = WM_NCMOUSEMOVE;
        }
        noise_ultralight(NOISE_SOURCE_MOUSEPOS, lParam);
        return 0;
      }
      case WM_IGNORE_CLIP:
        if (!is_term_hwnd) {
          return 0;
        }
        wgf->wnd_proc.ignore_clip = wParam;          /* don't panic on DESTROYCLIPBOARD */
        break;
      case WM_DESTROYCLIPBOARD:
        if (!is_term_hwnd) {
          return 0;
        }
        if (!wgf->wnd_proc.ignore_clip)
            term_lost_clipboard_ownership(term, CLIP_SYSTEM);
        wgf->wnd_proc.ignore_clip = false;
        return 0;
      case WM_PAINT: {
        if (!is_term_hwnd) {
          break;
        }
        PAINTSTRUCT p;

        HideCaret(hwnd);
        hdc = BeginPaint(hwnd, &p);
        if (wgf->pal) {
          SelectPalette(hdc, wgf->pal, true);
          RealizePalette(hdc);
        }

        /*
         * We have to be careful about term_paint(). It will
         * set a bunch of character cells to INVALID and then
         * call do_paint(), which will redraw those cells and
         * _then mark them as done_. This may not be accurate:
         * when painting in WM_PAINT context we are restricted
         * to the rectangle which has just been exposed - so if
         * that only covers _part_ of a character cell and the
         * rest of it was already visible, that remainder will
         * not be redrawn at all. Accordingly, we must not
         * paint any character cell in a WM_PAINT context which
         * already has a pending update due to terminal output.
         * The simplest solution to this - and many, many
         * thanks to Hung-Te Lin for working all this out - is
         * not to do any actual painting at _all_ if there's a
         * pending terminal update: just mark the relevant
         * character cells as INVALID and wait for the
         * scheduled full update to sort it out.
         *
         * I have a suspicion this isn't the _right_ solution.
         * An alternative approach would be to have terminal.c
         * separately track what _should_ be on the terminal
         * screen and what _is_ on the terminal screen, and
         * have two completely different types of redraw (one
         * for full updates, which syncs the former with the
         * terminal itself, and one for WM_PAINT which syncs
         * the latter with the former); yet another possibility
         * would be to have the Windows front end do what the
         * GTK one already does, and maintain a bitmap of the
         * current terminal appearance so that WM_PAINT becomes
         * completely trivial. However, this should do for now.
         */
        assert(!wgf->wintw_hdc);
        wgf->wintw_hdc = hdc;
        term_paint(term,
                   (p.rcPaint.left-wgf->offset_width)/wgf->font_width,
                   (p.rcPaint.top-wgf->offset_height)/wgf->font_height,
                   (p.rcPaint.right-wgf->offset_width-1)/wgf->font_width,
                   (p.rcPaint.bottom-wgf->offset_height-1)/wgf->font_height,
                   !term->window_update_pending);
        wgf->wintw_hdc = NULL;

        if (p.fErase ||
            p.rcPaint.left  < wgf->offset_width  ||
            p.rcPaint.top   < wgf->offset_height ||
            p.rcPaint.right >= wgf->offset_width + wgf->font_width*term->cols ||
            p.rcPaint.bottom>= wgf->offset_height + wgf->font_height*term->rows)
        {
          HBRUSH fillcolour, oldbrush;
          HPEN   edge, oldpen;
          fillcolour = CreateSolidBrush (
              wgf->colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
          oldbrush = SelectObject(hdc, fillcolour);
          edge = CreatePen(PS_SOLID, 0,
                           wgf->colours[ATTR_DEFBG>>ATTR_BGSHIFT]);
          oldpen = SelectObject(hdc, edge);

          /*
           * Jordan Russell reports that this apparently
           * ineffectual IntersectClipRect() call masks a
           * Windows NT/2K bug causing strange display
           * problems when the PuTTY window is taller than
           * the primary monitor. It seems harmless enough...
           */
          IntersectClipRect(hdc,
                            p.rcPaint.left, p.rcPaint.top,
                            p.rcPaint.right, p.rcPaint.bottom);

          ExcludeClipRect(hdc,
                          wgf->offset_width, wgf->offset_height,
                          wgf->offset_width+wgf->font_width*term->cols,
                          wgf->offset_height+wgf->font_height*term->rows);

          Rectangle(hdc, p.rcPaint.left, p.rcPaint.top,
                    p.rcPaint.right, p.rcPaint.bottom);

          /* SelectClipRgn(hdc, NULL); */

          SelectObject(hdc, oldbrush);
          DeleteObject(fillcolour);
          SelectObject(hdc, oldpen);
          DeleteObject(edge);
        }
        SelectObject(hdc, GetStockObject(SYSTEM_FONT));
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        EndPaint(hwnd, &p);
        ShowCaret(hwnd);
        return 0;
      }
      case WM_NETEVENT: {
        /*
         * To protect against re-entrancy when Windows's recv()
         * immediately triggers a new WSAAsyncSelect window
         * message, we don't call select_result directly from this
         * handler but instead wait until we're back out at the
         * top level of the message loop.
         */
        if (is_term_hwnd) {
          return 0;
        }
        struct wm_netevent_params *params =
            snew(struct wm_netevent_params);
        params->wParam = wParam;
        params->lParam = lParam;
        queue_toplevel_callback(wm_netevent_callback, params);
        return 0;
      }
      case WM_SETFOCUS:
        if (!is_term_hwnd) {
          SetFocus(term_hwnd);
          break;
        }
        term_set_focus(term, true);
        CreateCaret(hwnd, wgf->caretbm, wgf->font_width, wgf->font_height);
        ShowCaret(hwnd);
        flash_window(0);               /* stop */
        wgf->compose_state = 0;
        term_update(term);
        break;
      case WM_KILLFOCUS:
        if (!is_term_hwnd) {
          break;
        }
        show_mouseptr(wgf, true);
        term_set_focus(term, false);
        DestroyCaret();
        wgf->caret_x = wgf->caret_y = -1;        /* ensure caret is replaced next time */
        term_update(term);
        break;
      case WM_ENTERSIZEMOVE:
        if (is_term_hwnd) {
          break;
        }
        EnableSizeTip(true);
        resizing = true;
        wgf->need_backend_resize = false;
        break;
      case WM_EXITSIZEMOVE:
        if (is_term_hwnd) {
          break;
        }
        EnableSizeTip(false);
        resizing = false;
        if (wgf->need_backend_resize) {
            term_size(term, conf_get_int(conf, CONF_height),
                      conf_get_int(conf, CONF_width),
                      conf_get_int(conf, CONF_savelines));
            InvalidateRect(term_hwnd, NULL, true);
        }
        recompute_window_offset(wgf);
        break;
      case WM_SIZING:
        if (is_term_hwnd) {
          break;
        }
        /*
         * This does two jobs:
         * 1) Keep the sizetip uptodate
         * 2) Make sure the window size is _stepped_ in units of the font size.
         */
        resize_action = conf_get_int(conf, CONF_resize_action);
        if (resize_action == RESIZE_TERM ||
            (resize_action == RESIZE_EITHER && !is_alt_pressed())) {
            int width, height, w, h, ew, eh;
            LPRECT r = (LPRECT) lParam;

            if (!wgf->need_backend_resize && resize_action == RESIZE_EITHER &&
                (conf_get_int(conf, CONF_height) != term->rows ||
                 conf_get_int(conf, CONF_width) != term->cols)) {
                /*
                 * Great! It seems that both the terminal size and the
                 * font size have been changed and the user is now dragging.
                 *
                 * It will now be difficult to get back to the configured
                 * font size!
                 *
                 * This would be easier but it seems to be too confusing.
                 */
                conf_set_int(conf, CONF_height, term->rows);
                conf_set_int(conf, CONF_width, term->cols);

                InvalidateRect(term_hwnd, NULL, true);
                wgf->need_backend_resize = true;
            }

            width = r->right - r->left - extra_width;
            height = r->bottom - r->top - extra_height;
            w = (width + wgf->font_width / 2) / wgf->font_width;
            if (w < 1)
                w = 1;
            h = (height + wgf->font_height / 2) / wgf->font_height;
            if (h < 1)
                h = 1;
            UpdateSizeTip(hwnd, w, h);
            ew = width - w * wgf->font_width;
            eh = height - h * wgf->font_height;
            if (ew != 0) {
                if (wParam == WMSZ_LEFT ||
                    wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                    r->left += ew;
                else
                    r->right -= ew;
            }
            if (eh != 0) {
                if (wParam == WMSZ_TOP ||
                    wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                    r->top += eh;
                else
                    r->bottom -= eh;
            }
            if (ew || eh)
                return 1;
            else
                return 0;
        } else {
            int width, height, w, h, rv = 0;
            int window_border = conf_get_int(conf, CONF_window_border);
            int ex_width = extra_width + (window_border - wgf->offset_width) * 2;
            int ex_height = extra_height + (window_border - wgf->offset_height) * 2;
            LPRECT r = (LPRECT) lParam;

            width = r->right - r->left - ex_width;
            height = r->bottom - r->top - ex_height;

            w = (width + term->cols/2)/term->cols;
            h = (height + term->rows/2)/term->rows;
            if ( r->right != r->left + w*term->cols + ex_width)
                rv = 1;

            if (wParam == WMSZ_LEFT ||
                wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT)
                r->left = r->right - w*term->cols - ex_width;
            else
                r->right = r->left + w*term->cols + ex_width;

            if (r->bottom != r->top + h*term->rows + ex_height)
                rv = 1;

            if (wParam == WMSZ_TOP ||
                wParam == WMSZ_TOPRIGHT || wParam == WMSZ_TOPLEFT)
                r->top = r->bottom - h*term->rows - ex_height;
            else
                r->bottom = r->top + h*term->rows + ex_height;

            return rv;
        }
        /* break;  (never reached) */
      case WM_FULLSCR_ON_MAX:
        if (is_term_hwnd) {
          break;
        }
        fullscr_on_max = true;
        break;
      case WM_MOVE:
        if (is_term_hwnd) {
          break;
        }
        term_notify_window_pos(term, LOWORD(lParam), HIWORD(lParam));
        sys_cursor_update(wgf);
        break;
      case WM_SIZE:
        if (is_term_hwnd) {
          break;
        }
        resize_action = conf_get_int(conf, CONF_resize_action);
#ifdef RDB_DEBUG_PATCH
        debug("WM_SIZE %s (%d,%d)\n",
              (wParam == SIZE_MINIMIZED) ? "SIZE_MINIMIZED":
              (wParam == SIZE_MAXIMIZED) ? "SIZE_MAXIMIZED":
              (wParam == SIZE_RESTORED && resizing) ? "to":
              (wParam == SIZE_RESTORED) ? "SIZE_RESTORED":
              "...",
              LOWORD(lParam), HIWORD(lParam));
#endif
        term_notify_minimised(term, wParam == SIZE_MINIMIZED);
        {
            /*
             * WM_SIZE's lParam tells us the size of the client area.
             * But historic PuTTY practice is that we want to tell the
             * terminal the size of the overall window.
             */
            RECT r;
            GetWindowRect(hwnd, &r);
            term_notify_window_size_pixels(
                term, r.right - r.left, r.bottom - r.top);
        }
        if (wParam == SIZE_MINIMIZED)
            SetWindowText(hwnd,
                          conf_get_bool(conf, CONF_win_name_always) ?
                          wgf->window_name : wgf->icon_name);
        if (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)
            SetWindowText(hwnd, wgf->window_name);
        if (wParam == SIZE_RESTORED) {
            processed_resize = false;
            clear_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * clear_full_screen which contained the correct
                 * client area size.
                 */
                return 0;
            }
        }
        if (wParam == SIZE_MAXIMIZED && fullscr_on_max) {
            fullscr_on_max = false;
            processed_resize = false;
            make_full_screen();
            if (processed_resize) {
                /*
                 * Inhibit normal processing of this WM_SIZE; a
                 * secondary one was triggered just now by
                 * make_full_screen which contained the correct client
                 * area size.
                 */
                return 0;
            }
        }

        processed_resize = true;

        if (resize_action == RESIZE_DISABLED) {
            /* A resize, well it better be a minimize. */
            reset_window(wgf, -1);
        } else {
            if (wParam == SIZE_MAXIMIZED) {
                was_zoomed = true;
                wgf->resize_either.was_zoomed = true;
                wgf->resize_either.font_width = wgf->font_width;
                wgf->resize_either.font_height = wgf->font_height;
                if (resize_action == RESIZE_TERM)
                    wm_size_resize_term(wgf, lParam, false);
                reset_window(wgf, 0);
                tab_bar_adjust_window();
                adjust_terminal_window(frame_hwnd, term_hwnd);
            } else if (wParam == SIZE_RESTORED && was_zoomed) {
                was_zoomed = false;
                wgf->resize_either.was_zoomed = false;
                if (resize_action == RESIZE_TERM) {
                    wm_size_resize_term(wgf, lParam, true);
                    reset_window(wgf, 1);
                } else if (resize_action != RESIZE_FONT)
                    reset_window(wgf, 2);
                else
                    reset_window(wgf, 0);
                tab_bar_adjust_window();
                adjust_terminal_window(frame_hwnd, term_hwnd);
            } else if (wParam == SIZE_MINIMIZED) {
                /* do nothing */
            } else if (resize_action == RESIZE_TERM ||
                       (resize_action == RESIZE_EITHER &&
                        !is_alt_pressed())) {
                wm_size_resize_term(wgf, lParam, true);
                /*
                 * Sometimes, we can get a spontaneous resize event
                 * outside a WM_SIZING interactive drag which wants to
                 * set us to a new specific SIZE_RESTORED size. An
                 * example is what happens if you press Windows+Right
                 * and then Windows+Up: the first operation fits the
                 * window to the right-hand half of the screen, and
                 * the second one changes that for the top right
                 * quadrant. In that situation, if we've responded
                 * here by resizing the terminal, we may still need to
                 * recompute the border around the window and do a
                 * full redraw to clear the new border.
                 */
                if (!resizing)
                    recompute_window_offset(wgf);
                tab_bar_adjust_window();
                adjust_terminal_window(frame_hwnd, term_hwnd);
            } else {
                reset_window(wgf, 0);
                tab_bar_adjust_window();
                adjust_terminal_window(frame_hwnd, term_hwnd);
            }
        }
        sys_cursor_update(wgf);
        return 0;
      case WM_DPICHANGED:
        if (is_term_hwnd) {
          break;
        }
        dpi_info.x = LOWORD(wParam);
        dpi_info.y = HIWORD(wParam);
        dpi_changed_new_wnd_rect = *(RECT*)(lParam);
        tab_bar_set_measurement(get_dpi_aware_tab_bar_font());
        reset_window(wgf, 3);
        return 0;
      case WM_VSCROLL:
        if (is_term_hwnd) {
          break;
        }
        switch (LOWORD(wParam)) {
          case SB_BOTTOM:
            term_scroll(term, -1, 0);
            break;
          case SB_TOP:
            term_scroll(term, +1, 0);
            break;
          case SB_LINEDOWN:
            term_scroll(term, 0, +1);
            break;
          case SB_LINEUP:
            term_scroll(term, 0, -1);
            break;
          case SB_PAGEDOWN:
            term_scroll(term, 0, +term->rows / 2);
            break;
          case SB_PAGEUP:
            term_scroll(term, 0, -term->rows / 2);
            break;
          case SB_THUMBPOSITION:
          case SB_THUMBTRACK: {
            /*
             * Use GetScrollInfo instead of HIWORD(wParam) to get
             * 32-bit scroll position.
             */
            SCROLLINFO si;

            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(hwnd, SB_VERT, &si) == 0)
                si.nTrackPos = HIWORD(wParam);
            term_scroll(term, 1, si.nTrackPos);
            break;
          }
        }

        if (in_scrollbar_loop) {
            /*
             * Allow window updates to happen during interactive
             * scroll.
             *
             * When the user takes hold of our window's scrollbar and
             * wobbles it interactively back and forth, or presses on
             * one of the arrow buttons at the ends, the first thing
             * that happens is that this window procedure receives
             * WM_SYSCOMMAND / SC_VSCROLL. [1] The default handler for
             * that window message starts a subsidiary message loop,
             * which continues to run until the user lets go of the
             * scrollbar again. All WM_VSCROLL / SB_THUMBTRACK
             * messages are generated by the handlers within that
             * subsidiary message loop.
             *
             * So, during that time, _our_ message loop is not
             * running, which means toplevel callbacks and timers and
             * so forth are not happening, which means that when we
             * redraw the window and set a timer to clear the cooldown
             * flag 20ms later, that timer never fires, and we aren't
             * able to keep redrawing the window.
             *
             * The 'obvious' answer would be to seize that SYSCOMMAND
             * ourselves and inhibit the default handler, so that our
             * message loop carries on running. But that would mean
             * we'd have to reimplement the whole of the scrollbar
             * handler!
             *
             * So instead we apply a bodge: set a static variable that
             * indicates that we're _in_ that sub-loop, and if so,
             * decide it's OK to manually call term_update() proper,
             * bypassing the timer and cooldown and rate-limiting
             * systems completely, whenever we see an SB_THUMBTRACK.
             * This shouldn't cause a rate overload, because we're
             * only doing it once per UI event!
             *
             * [1] Actually, there's an extra oddity where SC_HSCROLL
             * and SC_VSCROLL have their documented values the wrong
             * way round. Many people on the Internet have noticed
             * this, e.g. https://stackoverflow.com/q/55528397
             */
            term_update(term);
        }
        break;
      case WM_PALETTECHANGED:
        if (!is_term_hwnd) {
          SendMessage(term_hwnd, message, wParam, lParam);
          break;
        }
        if ((HWND) wParam != hwnd && wgf->pal != NULL) {
            HDC hdc = make_hdc(wgf);
            if (hdc) {
                if (RealizePalette(hdc) > 0)
                    UpdateColors(hdc);
                free_hdc(term_hwnd, hdc);
            }
        }
        break;
      case WM_QUERYNEWPALETTE:
        if (!is_term_hwnd) {
          SendMessage(term_hwnd, message, wParam, lParam);
          return false;
        }
        if (wgf->pal != NULL) {
            HDC hdc = make_hdc(wgf);
            if (hdc) {
                if (RealizePalette(hdc) > 0)
                    UpdateColors(hdc);
                free_hdc(term_hwnd, hdc);
                return true;
            }
        }
        return false;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
      case WM_KEYUP:
      case WM_SYSKEYUP:
        if (!is_term_hwnd) {
          return 0;
        }
        /*
         * Add the scan code and keypress timing to the random
         * number noise.
         */
        noise_ultralight(NOISE_SOURCE_KEY, lParam);

        /*
         * We don't do TranslateMessage since it disassociates the
         * resulting CHAR message from the KEYDOWN that sparked it,
         * which we occasionally don't want. Instead, we process
         * KEYDOWN, and call the Win32 translator functions so that
         * we get the translations under _our_ control.
         */
        {
            unsigned char buf[20];
            int len;

            if (wParam == VK_PROCESSKEY || /* IME PROCESS key */
                wParam == VK_PACKET) {     /* 'this key is a Unicode char' */
                if (message == WM_KEYDOWN) {
                    MSG m;
                    m.hwnd = hwnd;
                    m.message = WM_KEYDOWN;
                    m.wParam = wParam;
                    m.lParam = lParam & 0xdfff;
                    TranslateMessage(&m);
                } else break; /* pass to Windows for default processing */
            } else {
                len = TranslateKey(wgf, message, wParam, lParam, buf);
                if (len == -1)
                    return DefWindowProcW(hwnd, message, wParam, lParam);

                if (len != 0) {
                    /*
                     * We need not bother about stdin backlogs
                     * here, because in GUI PuTTY we can't do
                     * anything about it anyway; there's no means
                     * of asking Windows to hold off on KEYDOWN
                     * messages. We _have_ to buffer everything
                     * we're sent.
                     */
                    term_keyinput(term, -1, buf, len);
                    show_mouseptr(wgf, false);
                }
            }
        }
        return 0;
      case WM_INPUTLANGCHANGE:
        if (!is_term_hwnd) {
          SendMessage(term_hwnd, message, wParam, lParam);
          break;
        }
        /* wParam == Font number */
        /* lParam == Locale */
        set_input_locale((HKL)lParam);
        sys_cursor_update(wgf);
        break;
      case WM_IME_STARTCOMPOSITION: {
        if (!is_term_hwnd) {
          break;
        }
        HIMC hImc = ImmGetContext(hwnd);
        ImmSetCompositionFont(hImc, &wgf->lfont);
        ImmReleaseContext(hwnd, hImc);
        break;
      }
      case WM_IME_COMPOSITION: {
        if (!is_term_hwnd) {
          return 0;
        }
        HIMC hIMC;
        int n;
        char *buff;

        if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS ||
            osPlatformId == VER_PLATFORM_WIN32s)
            break; /* no Unicode */

        if ((lParam & GCS_RESULTSTR) == 0) /* Composition unfinished. */
            break; /* fall back to DefWindowProc */

        hIMC = ImmGetContext(hwnd);
        n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);

        if (n > 0) {
          int i;
          buff = snewn(n, char);
          ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buff, n);
          /*
           * Jaeyoun Chung reports that Korean character
           * input doesn't work correctly if we do a single
           * term_keyinputw covering the whole of buff. So
           * instead we send the characters one by one.
           */
          /* don't divide SURROGATE PAIR */
          if (ldisc) {
            for (i = 0; i < n; i += 2) {
              WCHAR hs = *(unsigned short *)(buff+i);
              if (IS_HIGH_SURROGATE(hs) && i+2 < n) {
                WCHAR ls = *(unsigned short *)(buff+i+2);
                if (IS_LOW_SURROGATE(ls)) {
                  term_keyinputw(
                      term, (unsigned short *)(buff+i), 2);
                  i += 2;
                  continue;
                }
              }
              term_keyinputw(
                  term, (unsigned short *)(buff+i), 1);
            }
          }
          free(buff);
        }
        ImmReleaseContext(hwnd, hIMC);
        return 1;
      }

      case WM_IME_CHAR:
        if (!is_term_hwnd) {
          return 0;
        }
        if (wParam & 0xFF00) {
            char buf[2];

            buf[1] = wParam;
            buf[0] = wParam >> 8;
            term_keyinput(term, kbd_codepage, buf, 2);
        } else {
            char c = (unsigned char) wParam;
            term_seen_key_event(term);
            term_keyinput(term, kbd_codepage, &c, 1);
        }
        return (0);
      case WM_CHAR:
      case WM_SYSCHAR:
        if (!is_term_hwnd) {
          return 0;
        }
        /*
         * Nevertheless, we are prepared to deal with WM_CHAR
         * messages, should they crop up. So if someone wants to
         * post the things to us as part of a macro manoeuvre,
         * we're ready to cope.
         */
        {
            wchar_t c = wParam;

            if (IS_HIGH_SURROGATE(c)) {
                wgf->syschar.pending_surrogate = c;
            } else if (IS_SURROGATE_PAIR(wgf->syschar.pending_surrogate, c)) {
                wchar_t pair[2];
                pair[0] = wgf->syschar.pending_surrogate;
                pair[1] = c;
                term_keyinputw(term, pair, 2);
            } else if (!IS_SURROGATE(c)) {
                term_keyinputw(term, &c, 1);
            }
        }
        return 0;
      case WM_SYSCOLORCHANGE:
        if (!is_term_hwnd) {
          SendMessage(term_hwnd, message, wParam, lParam);
          break;
        }
        if (conf_get_bool(conf, CONF_system_colour)) {
            /* Refresh palette from system colours. */
            term_notify_palette_changed(term);
            init_palette(wgf);
            /* Force a repaint of the terminal window. */
            term_invalidate(term);
        }
        break;
      case WM_GOT_CLIPDATA:
        if (!is_term_hwnd) {
          return 0;
        }
        paste_clipdata(term, wParam, lParam);
        return 0;
      default:
        if (message == wm_mousewheel || message == WM_MOUSEWHEEL) {
            if (!is_term_hwnd) {
              return 0;
            }
            bool shift_pressed = false, control_pressed = false;

            if (message == WM_MOUSEWHEEL) {
                wgf->wheel_accumulator += (short)HIWORD(wParam);
                shift_pressed=LOWORD(wParam) & MK_SHIFT;
                control_pressed=LOWORD(wParam) & MK_CONTROL;
            } else {
                BYTE keys[256];
                wgf->wheel_accumulator += (int)wParam;
                if (GetKeyboardState(keys)!=0) {
                    shift_pressed=keys[VK_SHIFT]&0x80;
                    control_pressed=keys[VK_CONTROL]&0x80;
                }
            }

            /* process events when the threshold is reached */
            while (abs(wgf->wheel_accumulator) >= WHEEL_DELTA) {
                int b;

                /* reduce amount for next time */
                if (wgf->wheel_accumulator > 0) {
                    b = MBT_WHEEL_UP;
                    wgf->wheel_accumulator -= WHEEL_DELTA;
                } else if (wgf->wheel_accumulator < 0) {
                    b = MBT_WHEEL_DOWN;
                    wgf->wheel_accumulator += WHEEL_DELTA;
                } else
                    break;

                if (wgf->send_raw_mouse &&
                    !(conf_get_bool(conf, CONF_mouse_override) &&
                      shift_pressed)) {
                    /* Mouse wheel position is in screen coordinates for
                     * some reason */
                    POINT p;
                    p.x = X_POS(lParam); p.y = Y_POS(lParam);
                    if (ScreenToClient(hwnd, &p)) {
                        /* send a mouse-down followed by a mouse up */
                        term_mouse(term, b, translate_button(conf, b),
                                   MA_CLICK,
                                   TO_CHR_X(p.x),
                                   TO_CHR_Y(p.y), shift_pressed,
                                   control_pressed, is_alt_pressed());
                    } /* else: not sure when this can fail */
                } else {
                    /* trigger a scroll */
                    int where = (control_pressed ? term->rows / 2 : 3);
                    term_scroll(term, 0,
                                b == MBT_WHEEL_UP ?
                                -where : where);
                }
            }
            return 0;
        }
    }

    /*
     * Any messages we don't process completely above are passed through to
     * DefWindowProc() for default processing.
     */
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

/*
 * Move the system caret. (We maintain one, even though it's
 * invisible, for the benefit of blind people: apparently some
 * helper software tracks the system caret, so we should arrange to
 * have one.)
 */
static void wintw_set_cursor_pos(TermWin *tw, int x, int y)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    Terminal *term = wgf->term;
    int cx, cy;

    if (!term->has_focus) return;

    /*
     * Avoid gratuitously re-updating the cursor position and IMM
     * window if there's no actual change required.
     */
    cx = x * wgf->font_width + wgf->offset_width;
    cy = y * wgf->font_height + wgf->offset_height;
    if (cx == wgf->caret_x && cy == wgf->caret_y)
        return;
    wgf->caret_x = cx;
    wgf->caret_y = cy;

    sys_cursor_update(wgf);
}

static void sys_cursor_update(WinGuiFrontend *wgf)
{
    Terminal *term = wgf->term;
    COMPOSITIONFORM cf;
    HIMC hIMC;

    if (!term->has_focus) return;

    if (wgf->caret_x < 0 || wgf->caret_y < 0)
        return;

    SetCaretPos(wgf->caret_x, wgf->caret_y);

    /* IMM calls on Win98 and beyond only */
    if (osPlatformId == VER_PLATFORM_WIN32s) return; /* 3.11 */

    if (osPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
        osMinorVersion == 0) return; /* 95 */

    /* we should have the IMM functions */
    hIMC = ImmGetContext(term_hwnd);
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = wgf->caret_x;
    cf.ptCurrentPos.y = wgf->caret_y;
    ImmSetCompositionWindow(hIMC, &cf);

    ImmReleaseContext(term_hwnd, hIMC);
}

static void draw_horizontal_line_on_text(HDC wintw_hdc, int y, int lattr, RECT line_box,
                                         COLORREF colour, int font_height)
{
    if (lattr == LATTR_TOP || lattr == LATTR_BOT) {
        y *= 2;
        if (lattr == LATTR_BOT)
            y -= font_height;
    }

    if (!(0 <= y && y < font_height))
        return;

    HPEN oldpen = SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, colour));
    MoveToEx(wintw_hdc, line_box.left, line_box.top + y, NULL);
    LineTo(wintw_hdc, line_box.right, line_box.top + y);
    oldpen = SelectObject(wintw_hdc, oldpen);
    DeleteObject(oldpen);
}

/*
 * Draw a line of text in the window, at given character
 * coordinates, in given attributes.
 *
 * We are allowed to fiddle with the contents of `text'.
 */
static void do_text_internal(
    WinGuiFrontend *wgf,
    int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    Terminal *term = wgf->term;
    HDC wintw_hdc = wgf->wintw_hdc;
    COLORREF fg, bg, t;
    int nfg, nbg, nfont;
    RECT line_box;
    bool force_manual_underline = false;
    int fnt_width, char_width;
    int text_adjust = 0;
    int xoffset = 0;
    int maxlen, remaining;
    bool opaque;
    bool is_cursor = false;
    static int *lpDx = NULL;
    static size_t lpDx_len = 0;
    int *lpDx_maybe;
    int len2; /* for SURROGATE PAIR */

    lattr &= LATTR_MODE;

    char_width = fnt_width = wgf->font_width * (1 + (lattr != LATTR_NORM));

    if (attr & ATTR_WIDE)
        char_width *= 2;

    /* Only want the left half of double width lines */
    if (lattr != LATTR_NORM && x*2 >= term->cols)
        return;

    x *= fnt_width;
    y *= wgf->font_height;
    x += wgf->offset_width;
    y += wgf->offset_height;

    if ((attr & TATTR_ACTCURS) && (wgf->cursor_type == 0 || term->big_cursor)) {
        truecolour.fg = truecolour.bg = optionalrgb_none;
        attr &= ~(ATTR_REVERSE|ATTR_BLINK|ATTR_COLOURS|ATTR_DIM);
        /* cursor fg and bg */
        attr |= (260 << ATTR_FGSHIFT) | (261 << ATTR_BGSHIFT);
        is_cursor = true;
    }

    nfont = 0;
    if (wgf->vtmode == VT_POORMAN && lattr != LATTR_NORM) {
        /* Assume a poorman font is borken in other ways too. */
        lattr = LATTR_WIDE;
    } else
        switch (lattr) {
          case LATTR_NORM:
            break;
          case LATTR_WIDE:
            nfont |= FONT_WIDE;
            break;
          default:
            nfont |= FONT_WIDE + FONT_HIGH;
            break;
        }
    if (attr & ATTR_NARROW)
        nfont |= FONT_NARROW;

#ifdef USES_VTLINE_HACK
    /* Special hack for the VT100 linedraw glyphs. */
    if (text[0] >= 0x23BA && text[0] <= 0x23BD) {
        switch ((unsigned char) (text[0])) {
          case 0xBA:
            text_adjust = -2 * wgf->font_height / 5;
            break;
          case 0xBB:
            text_adjust = -1 * wgf->font_height / 5;
            break;
          case 0xBC:
            text_adjust = wgf->font_height / 5;
            break;
          case 0xBD:
            text_adjust = 2 * wgf->font_height / 5;
            break;
        }
        if (lattr == LATTR_TOP || lattr == LATTR_BOT)
            text_adjust *= 2;
        text[0] = wgf->ucsdata.unitab_xterm['q'];
        if (attr & ATTR_UNDER) {
            attr &= ~ATTR_UNDER;
            force_manual_underline = true;
        }
    }
#endif

    /* Anything left as an original character set is unprintable. */
    if (DIRECT_CHAR(text[0]) &&
        (len < 2 || !IS_SURROGATE_PAIR(text[0], text[1]))) {
        int i;
        for (i = 0; i < len; i++)
            text[i] = 0xFFFD;
    }

    /* OEM CP */
    if ((text[0] & CSET_MASK) == CSET_OEMCP)
        nfont |= FONT_OEM;

    nfg = ((attr & ATTR_FGMASK) >> ATTR_FGSHIFT);
    nbg = ((attr & ATTR_BGMASK) >> ATTR_BGSHIFT);
    if (wgf->bold_font_mode == BOLD_FONT && (attr & ATTR_BOLD))
        nfont |= FONT_BOLD;
    if (wgf->und_mode == UND_FONT && (attr & ATTR_UNDER))
        nfont |= FONT_UNDERLINE;
    another_font(wgf, nfont);
    if (!wgf->fonts[nfont]) {
        if (nfont & FONT_UNDERLINE)
            force_manual_underline = true;
        /* Don't do the same for manual bold, it could be bad news. */

        nfont &= ~(FONT_BOLD | FONT_UNDERLINE);
    }
    another_font(wgf, nfont);
    if (!wgf->fonts[nfont])
        nfont = FONT_NORMAL;
    if (attr & ATTR_REVERSE) {
        struct optionalrgb trgb;

        t = nfg;
        nfg = nbg;
        nbg = t;

        trgb = truecolour.fg;
        truecolour.fg = truecolour.bg;
        truecolour.bg = trgb;
    }
    if (wgf->bold_colours && (attr & ATTR_BOLD) && !is_cursor) {
        if (nfg < 16) nfg |= 8;
        else if (nfg >= 256) nfg |= 1;
    }
    if (wgf->bold_colours && (attr & ATTR_BLINK)) {
        if (nbg < 16) nbg |= 8;
        else if (nbg >= 256) nbg |= 1;
    }
    if (!wgf->pal && truecolour.fg.enabled)
        fg = RGB(truecolour.fg.r, truecolour.fg.g, truecolour.fg.b);
    else
        fg = wgf->colours[nfg];

    if (!wgf->pal && truecolour.bg.enabled)
        bg = RGB(truecolour.bg.r, truecolour.bg.g, truecolour.bg.b);
    else
        bg = wgf->colours[nbg];

    if (!wgf->pal && (attr & ATTR_DIM)) {
        fg = RGB(GetRValue(fg) * 2 / 3,
                 GetGValue(fg) * 2 / 3,
                 GetBValue(fg) * 2 / 3);
    }

    SelectObject(wintw_hdc, wgf->fonts[nfont]);
    SetTextColor(wintw_hdc, fg);
    SetBkColor(wintw_hdc, bg);
    if (attr & TATTR_COMBINING)
        SetBkMode(wintw_hdc, TRANSPARENT);
    else
        SetBkMode(wintw_hdc, OPAQUE);
    line_box.left = x;
    line_box.top = y;
    line_box.right = x + char_width * len;
    line_box.bottom = y + wgf->font_height;
    /* adjust line_box.right for SURROGATE PAIR & VARIATION SELECTOR */
    {
        int i;
        int rc_width = 0;
        for (i = 0; i < len ; i++) {
            if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
                i++;
            } else if (i+1 < len && IS_SURROGATE_PAIR(text[i], text[i+1])) {
                rc_width += char_width;
                i++;
            } else if (IS_LOW_VARSEL(text[i])) {
                /* do nothing */
            } else {
                rc_width += char_width;
            }
        }
        line_box.right = line_box.left + rc_width;
    }

    /* Only want the left half of double width lines */
    if (line_box.right > wgf->font_width*term->cols+wgf->offset_width)
        line_box.right = wgf->font_width*term->cols+wgf->offset_width;

    if (wgf->font_varpitch) {
        /*
         * If we're using a variable-pitch font, we unconditionally
         * draw the glyphs one at a time and centre them in their
         * character cells (which means in particular that we must
         * disable the lpDx mechanism). This gives slightly odd but
         * generally reasonable results.
         */
        xoffset = char_width / 2;
        SetTextAlign(wintw_hdc, TA_TOP | TA_CENTER | TA_NOUPDATECP);
        lpDx_maybe = NULL;
        maxlen = 1;
    } else {
        /*
         * In a fixed-pitch font, we draw the whole string in one go
         * in the normal way.
         */
        xoffset = 0;
        SetTextAlign(wintw_hdc, TA_TOP | TA_LEFT | TA_NOUPDATECP);
        lpDx_maybe = lpDx;
        maxlen = len;
    }

    opaque = true;                     /* start by erasing the rectangle */
    for (remaining = len; remaining > 0;
         text += len, remaining -= len, x += char_width * len2) {
        len = (maxlen < remaining ? maxlen : remaining);
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
        len2 = len;
        if (maxlen == 1) {
            if (remaining >= 1 && IS_SURROGATE_PAIR(text[0], text[1]))
                len++;
            if (remaining-len >= 1 && IS_LOW_VARSEL(text[len]))
                len++;
            else if (remaining-len >= 2 &&
                     IS_HIGH_VARSEL(text[len], text[len+1]))
                len += 2;
        }

        if (len > lpDx_len) {
            sgrowarray(lpDx, lpDx_len, len);
            if (lpDx_maybe) lpDx_maybe = lpDx;
        }

        {
            int i;
            /* only last char has dx width in SURROGATE PAIR and
             * VARIATION sequence */
            for (i = 0; i < len; i++) {
                lpDx[i] = char_width;
                if (i+1 < len && IS_HIGH_VARSEL(text[i], text[i+1])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (i+1 < len && IS_SURROGATE_PAIR(text[i],text[i+1])) {
                    lpDx[i] = 0;
                    i++;
                    lpDx[i] = char_width;
                } else if (IS_LOW_VARSEL(text[i])) {
                    if (i > 0) lpDx[i-1] = 0;
                    lpDx[i] = char_width;
                }
            }
        }

        /* We're using a private area for direct to font. (512 chars.) */
        if (wgf->ucsdata.dbcs_screenfont && (text[0] & CSET_MASK) == CSET_ACP) {
            /* Ho Hum, dbcs fonts are a PITA! */
            /* To display on W9x I have to convert to UCS */
            static wchar_t *uni_buf = 0;
            static int uni_len = 0;
            int nlen, mptr;
            if (len > uni_len) {
                sfree(uni_buf);
                uni_len = len;
                uni_buf = snewn(uni_len, wchar_t);
            }

            for(nlen = mptr = 0; mptr<len; mptr++) {
                uni_buf[nlen] = 0xFFFD;
                if (IsDBCSLeadByteEx(wgf->ucsdata.font_codepage,
                                     (BYTE) text[mptr])) {
                    char dbcstext[2];
                    dbcstext[0] = text[mptr] & 0xFF;
                    dbcstext[1] = text[mptr+1] & 0xFF;
                    lpDx[nlen] += char_width;
                    MultiByteToWideChar(wgf->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 2, uni_buf+nlen, 1);
                    mptr++;
                }
                else
                {
                    char dbcstext[1];
                    dbcstext[0] = text[mptr] & 0xFF;
                    MultiByteToWideChar(wgf->ucsdata.font_codepage, MB_USEGLYPHCHARS,
                                        dbcstext, 1, uni_buf+nlen, 1);
                }
                nlen++;
            }
            if (nlen <= 0)
                return;                /* Eeek! */

            ExtTextOutW(wintw_hdc, x + xoffset,
                        y - wgf->font_height * (lattr == LATTR_BOT) + text_adjust,
                        ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                        &line_box, uni_buf, nlen,
                        lpDx_maybe);
            if (wgf->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);
                ExtTextOutW(wintw_hdc, x + xoffset - 1,
                            y - wgf->font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, uni_buf, nlen, lpDx_maybe);
            }

            lpDx[0] = -1;
        } else if (DIRECT_FONT(text[0])) {
            static char *directbuf = NULL;
            static size_t directlen = 0;

            sgrowarray(directbuf, directlen, len);
            for (size_t i = 0; i < len; i++)
                directbuf[i] = text[i] & 0xFF;

            ExtTextOut(wintw_hdc, x + xoffset,
                       y - wgf->font_height * (lattr == LATTR_BOT) + text_adjust,
                       ETO_CLIPPED | (opaque ? ETO_OPAQUE : 0),
                       &line_box, directbuf, len, lpDx_maybe);
            if (wgf->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);

                /* GRR: This draws the character outside its box and
                 * can leave 'droppings' even with the clip box! I
                 * suppose I could loop it one character at a time ...
                 * yuk.
                 *
                 * Or ... I could do a test print with "W", and use +1
                 * or -1 for this shift depending on if the leftmost
                 * column is blank...
                 */
                ExtTextOut(wintw_hdc, x + xoffset - 1,
                           y - wgf->font_height * (lattr ==
                                              LATTR_BOT) + text_adjust,
                           ETO_CLIPPED, &line_box, directbuf, len, lpDx_maybe);
            }
        } else {
            /* And 'normal' unicode characters */
            static WCHAR *wbuf = NULL;
            static int wlen = 0;
            int i;

            if (wlen < len) {
                sfree(wbuf);
                wlen = len;
                wbuf = snewn(wlen, WCHAR);
            }

            for (i = 0; i < len; i++)
                wbuf[i] = text[i];

            /* print Glyphs as they are, without Windows' Shaping*/
            general_textout(wintw_hdc, x + xoffset,
                            y - wgf->font_height * (lattr==LATTR_BOT) + text_adjust,
                            &line_box, wbuf, len, lpDx,
                            opaque && !(attr & TATTR_COMBINING), wgf->font_varpitch);

            /* And the shadow bold hack. */
            if (wgf->bold_font_mode == BOLD_SHADOW && (attr & ATTR_BOLD)) {
                SetBkMode(wintw_hdc, TRANSPARENT);
                ExtTextOutW(wintw_hdc, x + xoffset - 1,
                            y - wgf->font_height * (lattr ==
                                               LATTR_BOT) + text_adjust,
                            ETO_CLIPPED, &line_box, wbuf, len, lpDx_maybe);
            }
        }

        /*
         * If we're looping round again, stop erasing the background
         * rectangle.
         */
        SetBkMode(wintw_hdc, TRANSPARENT);
        opaque = false;
    }

    if (lattr != LATTR_TOP && (force_manual_underline ||
                               (wgf->und_mode == UND_LINE && (attr & ATTR_UNDER))))
        draw_horizontal_line_on_text(wgf->wintw_hdc, wgf->descent, lattr, line_box, fg, wgf->font_height);

    if (attr & ATTR_STRIKE)
        draw_horizontal_line_on_text(wgf->wintw_hdc, wgf->font_strikethrough_y, lattr, line_box, fg, wgf->font_height);
}

/*
 * Wrapper that handles combining characters.
 */
static void wintw_draw_text(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (attr & TATTR_COMBINING) {
        unsigned long a = 0;
        int len0 = 1;
        /* don't divide SURROGATE PAIR and VARIATION SELECTOR */
        if (len >= 2 && IS_SURROGATE_PAIR(text[0], text[1]))
            len0 = 2;
        if (len-len0 >= 1 && IS_LOW_VARSEL(text[len0])) {
            attr &= ~TATTR_COMBINING;
            do_text_internal(wgf, x, y, text, len0+1, attr, lattr, truecolour);
            text += len0+1;
            len -= len0+1;
            a = TATTR_COMBINING;
        } else if (len-len0 >= 2 && IS_HIGH_VARSEL(text[len0], text[len0+1])) {
            attr &= ~TATTR_COMBINING;
            do_text_internal(wgf, x, y, text, len0+2, attr, lattr, truecolour);
            text += len0+2;
            len -= len0+2;
            a = TATTR_COMBINING;
        } else {
            attr &= ~TATTR_COMBINING;
        }

        while (len--) {
            if (len >= 1 && IS_SURROGATE_PAIR(text[0], text[1])) {
                do_text_internal(wgf, x, y, text, 2, attr | a, lattr, truecolour);
                len--;
                text++;
            } else
                do_text_internal(wgf, x, y, text, 1, attr | a, lattr, truecolour);

            text++;
            a = TATTR_COMBINING;
        }
    } else
        do_text_internal(wgf, x, y, text, len, attr, lattr, truecolour);
}

static void wintw_draw_cursor(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour truecolour)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    Terminal *term = wgf->term;
    HDC wintw_hdc = wgf->wintw_hdc;
    int fnt_width;
    int char_width;
    int ctype = wgf->cursor_type;

    lattr &= LATTR_MODE;

    if ((attr & TATTR_ACTCURS) && (ctype == 0 || term->big_cursor)) {
        if (*text != UCSWIDE) {
            win_draw_text(tw, x, y, text, len, attr, lattr, truecolour);
            return;
        }
        ctype = 2;
        attr |= TATTR_RIGHTCURS;
    }

    fnt_width = char_width = wgf->font_width * (1 + (lattr != LATTR_NORM));
    if (attr & ATTR_WIDE)
        char_width *= 2;
    x *= fnt_width;
    y *= wgf->font_height;
    x += wgf->offset_width;
    y += wgf->offset_height;

    if ((attr & TATTR_PASCURS) && (ctype == 0 || term->big_cursor)) {
        POINT pts[5];
        HPEN oldpen;
        pts[0].x = pts[1].x = pts[4].x = x;
        pts[2].x = pts[3].x = x + char_width - 1;
        pts[0].y = pts[3].y = pts[4].y = y;
        pts[1].y = pts[2].y = y + wgf->font_height - 1;
        oldpen = SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, wgf->colours[261]));
        Polyline(wintw_hdc, pts, 5);
        oldpen = SelectObject(wintw_hdc, oldpen);
        DeleteObject(oldpen);
    } else if ((attr & (TATTR_ACTCURS | TATTR_PASCURS)) && ctype != 0) {
        int startx, starty, dx, dy, length, i;
        if (ctype == 1) {
            startx = x;
            starty = y + wgf->descent;
            dx = 1;
            dy = 0;
            length = char_width;
        } else {
            int xadjust = 0;
            if (attr & TATTR_RIGHTCURS)
                xadjust = char_width - 1;
            startx = x + xadjust;
            starty = y;
            dx = 0;
            dy = 1;
            length = wgf->font_height;
        }
        if (attr & TATTR_ACTCURS) {
            HPEN oldpen;
            oldpen =
                SelectObject(wintw_hdc, CreatePen(PS_SOLID, 0, wgf->colours[261]));
            MoveToEx(wintw_hdc, startx, starty, NULL);
            LineTo(wintw_hdc, startx + dx * length, starty + dy * length);
            oldpen = SelectObject(wintw_hdc, oldpen);
            DeleteObject(oldpen);
        } else {
            for (i = 0; i < length; i++) {
                if (i % 2 == 0) {
                    SetPixel(wintw_hdc, startx, starty, wgf->colours[261]);
                }
                startx += dx;
                starty += dy;
            }
        }
    }
}

static void wintw_draw_trust_sigil(TermWin *tw, int x, int y)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);

    x *= wgf->font_width;
    y *= wgf->font_height;
    x += wgf->offset_width;
    y += wgf->offset_height;

    DrawIconEx(wgf->wintw_hdc, x, y, wgf->trust_icon, wgf->font_width * 2, wgf->font_height,
               0, NULL, DI_NORMAL);
}

/* This function gets the actual width of a character in the normal font.
 */
static int wintw_char_width(TermWin *tw, int uc)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    HDC wintw_hdc = wgf->wintw_hdc;
    int ibuf = 0;

    /* If the font max is the same as the font ave width then this
     * function is a no-op.
     */
    if (!wgf->font_dualwidth) return 1;

    switch (uc & CSET_MASK) {
      case CSET_ASCII:
        uc = wgf->ucsdata.unitab_line[uc & 0xFF];
        break;
      case CSET_LINEDRW:
        uc = wgf->ucsdata.unitab_xterm[uc & 0xFF];
        break;
      case CSET_SCOACS:
        uc = wgf->ucsdata.unitab_scoacs[uc & 0xFF];
        break;
    }
    if (DIRECT_FONT(uc)) {
        if (wgf->ucsdata.dbcs_screenfont) return 1;

        /* Speedup, I know of no font where ascii is the wrong width */
        if ((uc&~CSET_MASK) >= ' ' && (uc&~CSET_MASK)<= '~')
            return 1;

        if ( (uc & CSET_MASK) == CSET_ACP ) {
            SelectObject(wintw_hdc, wgf->fonts[FONT_NORMAL]);
        } else if ( (uc & CSET_MASK) == CSET_OEMCP ) {
            another_font(wgf, FONT_OEM);
            if (!wgf->fonts[FONT_OEM]) return 0;

            SelectObject(wintw_hdc, wgf->fonts[FONT_OEM]);
        } else
            return 0;

        if (GetCharWidth32(wintw_hdc, uc & ~CSET_MASK,
                           uc & ~CSET_MASK, &ibuf) != 1 &&
            GetCharWidth(wintw_hdc, uc & ~CSET_MASK,
                         uc & ~CSET_MASK, &ibuf) != 1)
            return 0;
    } else {
        /* Speedup, I know of no font where ascii is the wrong width */
        if (uc >= ' ' && uc <= '~') return 1;

        SelectObject(wintw_hdc, wgf->fonts[FONT_NORMAL]);
        if (GetCharWidth32W(wintw_hdc, uc, uc, &ibuf) == 1)
            /* Okay that one worked */ ;
        else if (GetCharWidthW(wintw_hdc, uc, uc, &ibuf) == 1)
            /* This should work on 9x too, but it's "less accurate" */ ;
        else
            return 0;
    }

    ibuf += wgf->font_width / 2 -1;
    ibuf /= wgf->font_width;

    return ibuf;
}

DECL_WINDOWS_FUNCTION(static, BOOL, FlashWindowEx, (PFLASHWINFO));
DECL_WINDOWS_FUNCTION(static, BOOL, ToUnicodeEx,
                      (UINT, UINT, const BYTE *, LPWSTR, int, UINT, HKL));
DECL_WINDOWS_FUNCTION(static, BOOL, PlaySound, (LPCTSTR, HMODULE, DWORD));

static void init_winfuncs(void)
{
    HMODULE user32_module = load_system32_dll("user32.dll");
    HMODULE winmm_module = load_system32_dll("winmm.dll");
    HMODULE shcore_module = load_system32_dll("shcore.dll");
    GET_WINDOWS_FUNCTION(user32_module, FlashWindowEx);
    GET_WINDOWS_FUNCTION(user32_module, ToUnicodeEx);
    GET_WINDOWS_FUNCTION_PP(winmm_module, PlaySound);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(shcore_module, GetDpiForMonitor);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, GetSystemMetricsForDpi);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, AdjustWindowRectExForDpi);
    GET_WINDOWS_FUNCTION_NO_TYPECHECK(user32_module, SystemParametersInfoForDpi);
}

/*
 * Translate a WM_(SYS)?KEY(UP|DOWN) message into a string of ASCII
 * codes. Returns number of bytes used, zero to drop the message,
 * -1 to forward the message to Windows, or another negative number
 * to indicate a NUL-terminated "special" string.
 */
static int TranslateKey(WinGuiFrontend *wgf, UINT message, WPARAM wParam, LPARAM lParam,
                        unsigned char *output)
{
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    BYTE keystate[256];
    int scan, shift_state;
    bool left_alt = false, key_down;
    int r, i;
    unsigned char *p = output;
    int funky_type = conf_get_int(conf, CONF_funky_type);
    bool no_applic_k = conf_get_bool(conf, CONF_no_applic_k);
    bool ctrlaltkeys = conf_get_bool(conf, CONF_ctrlaltkeys);
    bool nethack_keypad = conf_get_bool(conf, CONF_nethack_keypad);
    char keypad_key = '\0';

    HKL kbd_layout = GetKeyboardLayout(0);

    static wchar_t keys_unicode[3];

    r = GetKeyboardState(keystate);
    if (!r)
        memset(keystate, 0, sizeof(keystate));
    else {
#if 0
#define SHOW_TOASCII_RESULT
        {                              /* Tell us all about key events */
            static BYTE oldstate[256];
            static int first = 1;
            static int scan;
            int ch;
            if (first)
                memcpy(oldstate, keystate, sizeof(oldstate));
            first = 0;

            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT) {
                debug("+");
            } else if ((HIWORD(lParam) & KF_UP)
                       && scan == (HIWORD(lParam) & 0xFF)) {
                debug(". U");
            } else {
                debug(".\n");
                if (wParam >= VK_F1 && wParam <= VK_F20)
                    debug("K_F%d", wParam + 1 - VK_F1);
                else
                    switch (wParam) {
                      case VK_SHIFT:
                        debug("SHIFT");
                        break;
                      case VK_CONTROL:
                        debug("CTRL");
                        break;
                      case VK_MENU:
                        debug("ALT");
                        break;
                      default:
                        debug("VK_%02x", wParam);
                    }
                if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
                    debug("*");
                debug(", S%02x", scan = (HIWORD(lParam) & 0xFF));

                ch = MapVirtualKeyEx(wParam, 2, kbd_layout);
                if (ch >= ' ' && ch <= '~')
                    debug(", '%c'", ch);
                else if (ch)
                    debug(", $%02x", ch);

                if (keys_unicode[0])
                    debug(", KB0=%04x", keys_unicode[0]);
                if (keys_unicode[1])
                    debug(", KB1=%04x", keys_unicode[1]);
                if (keys_unicode[2])
                    debug(", KB2=%04x", keys_unicode[2]);

                if ((keystate[VK_SHIFT] & 0x80) != 0)
                    debug(", S");
                if ((keystate[VK_CONTROL] & 0x80) != 0)
                    debug(", C");
                if ((HIWORD(lParam) & KF_EXTENDED))
                    debug(", E");
                if ((HIWORD(lParam) & KF_UP))
                    debug(", U");
            }

            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT);
            else if ((HIWORD(lParam) & KF_UP))
                oldstate[wParam & 0xFF] ^= 0x80;
            else
                oldstate[wParam & 0xFF] ^= 0x81;

            for (ch = 0; ch < 256; ch++)
                if (oldstate[ch] != keystate[ch])
                    debug(", M%02x=%02x", ch, keystate[ch]);

            memcpy(oldstate, keystate, sizeof(oldstate));
        }
#endif

        if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED)) {
            keystate[VK_RMENU] = keystate[VK_MENU];
        }


        /* Nastyness with NUMLock - Shift-NUMLock is left alone though */
        if ((funky_type == FUNKY_VT400 ||
             (funky_type <= FUNKY_LINUX && term->app_keypad_keys &&
              !no_applic_k))
            && wParam == VK_NUMLOCK && !(keystate[VK_SHIFT] & 0x80)) {

            wParam = VK_EXECUTE;

            /* UnToggle NUMLock */
            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0)
                keystate[VK_NUMLOCK] ^= 1;
        }

        /* And write back the 'adjusted' state */
        SetKeyboardState(keystate);
    }

    /* Disable Auto repeat if required */
    if (term->repeat_off &&
        (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == KF_REPEAT)
        return 0;

    if ((HIWORD(lParam) & KF_ALTDOWN) && (keystate[VK_RMENU] & 0x80) == 0)
        left_alt = true;

    key_down = ((HIWORD(lParam) & KF_UP) == 0);

    /* Make sure Ctrl-ALT is not the same as AltGr for ToAscii unless told. */
    if (left_alt && (keystate[VK_CONTROL] & 0x80)) {
        if (ctrlaltkeys)
            keystate[VK_MENU] = 0;
        else {
            keystate[VK_RMENU] = 0x80;
            left_alt = false;
        }
    }

    scan = (HIWORD(lParam) & (KF_UP | KF_EXTENDED | 0xFF));
    shift_state = ((keystate[VK_SHIFT] & 0x80) != 0)
        + ((keystate[VK_CONTROL] & 0x80) != 0) * 2;

    /* Note if AltGr was pressed and if it was used as a compose key */
    if (!wgf->compose_state) {
        wgf->translate_key.compose_keycode = 0x100;
        if (conf_get_bool(conf, CONF_compose_key)) {
            if (wParam == VK_MENU && (HIWORD(lParam) & KF_EXTENDED))
                wgf->translate_key.compose_keycode = wParam;
        }
        if (wParam == VK_APPS)
            wgf->translate_key.compose_keycode = wParam;
    }

    if (wParam == wgf->translate_key.compose_keycode) {
        if (wgf->compose_state == 0
            && (HIWORD(lParam) & (KF_UP | KF_REPEAT)) == 0) wgf->compose_state =
                1;
        else if (wgf->compose_state == 1 && (HIWORD(lParam) & KF_UP))
            wgf->compose_state = 2;
        else
            wgf->compose_state = 0;
    } else if (wgf->compose_state == 1 && wParam != VK_CONTROL)
        wgf->compose_state = 0;

    if (wgf->compose_state > 1 && left_alt)
        wgf->compose_state = 0;

    /* Sanitize the number pad if not using a PC NumPad */
    if (left_alt || (term->app_keypad_keys && !no_applic_k
                     && funky_type != FUNKY_XTERM)
        || funky_type == FUNKY_VT400 || nethack_keypad || wgf->compose_state) {
        if ((HIWORD(lParam) & KF_EXTENDED) == 0) {
            int nParam = 0;
            switch (wParam) {
              case VK_INSERT:
                nParam = VK_NUMPAD0;
                break;
              case VK_END:
                nParam = VK_NUMPAD1;
                break;
              case VK_DOWN:
                nParam = VK_NUMPAD2;
                break;
              case VK_NEXT:
                nParam = VK_NUMPAD3;
                break;
              case VK_LEFT:
                nParam = VK_NUMPAD4;
                break;
              case VK_CLEAR:
                nParam = VK_NUMPAD5;
                break;
              case VK_RIGHT:
                nParam = VK_NUMPAD6;
                break;
              case VK_HOME:
                nParam = VK_NUMPAD7;
                break;
              case VK_UP:
                nParam = VK_NUMPAD8;
                break;
              case VK_PRIOR:
                nParam = VK_NUMPAD9;
                break;
              case VK_DELETE:
                nParam = VK_DECIMAL;
                break;
            }
            if (nParam) {
                if (keystate[VK_NUMLOCK] & 1)
                    shift_state |= 1;
                wParam = nParam;
            }
        }
    }

    /* If a key is pressed and AltGr is not active */
    if (key_down && (keystate[VK_RMENU] & 0x80) == 0 && !wgf->compose_state) {
        /* Okay, prepare for most alts then ... */
        if (left_alt)
            *p++ = '\033';

        /* Lets see if it's a pattern we know all about ... */
        if (wParam == VK_PRIOR && shift_state == 1) {
            SendMessage(term_hwnd, WM_VSCROLL, SB_PAGEUP, 0);
            return 0;
        }
        if (wParam == VK_PRIOR && shift_state == 3) { /* ctrl-shift-pageup */
            SendMessage(term_hwnd, WM_VSCROLL, SB_TOP, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 3) { /* ctrl-shift-pagedown */
            SendMessage(term_hwnd, WM_VSCROLL, SB_BOTTOM, 0);
            return 0;
        }

        if (wParam == VK_PRIOR && shift_state == 2) {
            SendMessage(term_hwnd, WM_VSCROLL, SB_LINEUP, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 1) {
            SendMessage(term_hwnd, WM_VSCROLL, SB_PAGEDOWN, 0);
            return 0;
        }
        if (wParam == VK_NEXT && shift_state == 2) {
            SendMessage(term_hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
            return 0;
        }
        if ((wParam == VK_PRIOR || wParam == VK_NEXT) && shift_state == 3) {
            term_scroll_to_selection(term, (wParam == VK_PRIOR ? 0 : 1));
            return 0;
        }
        if (wParam == VK_INSERT && shift_state == 2) {
            switch (conf_get_int(conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(term, clips_system, lenof(clips_system));
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == VK_INSERT && shift_state == 1) {
            switch (conf_get_int(conf, CONF_ctrlshiftins)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == 'C' && shift_state == 3) {
            switch (conf_get_int(conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                break;          /* no need to re-copy to CLIP_LOCAL */
              case CLIPUI_EXPLICIT:
                term_request_copy(term, clips_system, lenof(clips_system));
                break;
              default:
                break;
            }
            return 0;
        }
        if (wParam == 'V' && shift_state == 3) {
            switch (conf_get_int(conf, CONF_ctrlshiftcv)) {
              case CLIPUI_IMPLICIT:
                term_request_paste(term, CLIP_LOCAL);
                break;
              case CLIPUI_EXPLICIT:
                term_request_paste(term, CLIP_SYSTEM);
                break;
              default:
                break;
            }
            return 0;
        }
        if (left_alt && wParam == VK_F4 && conf_get_bool(conf, CONF_alt_f4)) {
            return -1;
        }
        if (left_alt && wParam == VK_SPACE && conf_get_bool(conf,
                                                            CONF_alt_space)) {
            SendMessage(term_hwnd, WM_SYSCOMMAND, SC_KEYMENU, 0);
            return -1;
        }
        if (left_alt && wParam == VK_RETURN &&
            conf_get_bool(conf, CONF_fullscreenonaltenter) &&
            (conf_get_int(conf, CONF_resize_action) != RESIZE_DISABLED)) {
            if ((HIWORD(lParam) & (KF_UP | KF_REPEAT)) != KF_REPEAT)
                SendMessage(frame_hwnd, WM_COMMAND, IDM_FULLSCREEN, 0);
            return -1;
        }
        /* Control-Numlock for app-keypad mode switch */
        if (wParam == VK_PAUSE && shift_state == 2) {
            term->app_keypad_keys = !term->app_keypad_keys;
            return 0;
        }

        if (wParam == VK_BACK && shift_state == 0) {    /* Backspace */
            *p++ = (conf_get_bool(conf, CONF_bksp_is_delete) ? 0x7F : 0x08);
            *p++ = 0;
            return -2;
        }
        if (wParam == VK_BACK && shift_state == 1) {    /* Shift Backspace */
            /* We do the opposite of what is configured */
            *p++ = (conf_get_bool(conf, CONF_bksp_is_delete) ? 0x08 : 0x7F);
            *p++ = 0;
            return -2;
        }
        if (wParam == VK_TAB && shift_state == 1) {     /* Shift tab */
            *p++ = 0x1B;
            *p++ = '[';
            *p++ = 'Z';
            return p - output;
        }
        if (wParam == VK_SPACE && shift_state == 2) {   /* Ctrl-Space */
            *p++ = 0;
            return p - output;
        }
        if (wParam == VK_SPACE && shift_state == 3) {   /* Ctrl-Shift-Space */
            *p++ = 160;
            return p - output;
        }
        if (wParam == VK_CANCEL && shift_state == 2) {  /* Ctrl-Break */
            if (wgf->backend)
                backend_special(wgf->backend, SS_BRK, 0);
            return 0;
        }
        if (wParam == VK_PAUSE) {      /* Break/Pause */
            *p++ = 26;
            *p++ = 0;
            return -2;
        }
        /* Control-2 to Control-8 are special */
        if (shift_state == 2 && wParam >= '2' && wParam <= '8') {
            *p++ = "\000\033\034\035\036\037\177"[wParam - '2'];
            return p - output;
        }
        if (shift_state == 2 && (wParam == 0xBD || wParam == 0xBF)) {
            *p++ = 0x1F;
            return p - output;
        }
        if (shift_state == 2 && (wParam == 0xDF || wParam == 0xDC)) {
            *p++ = 0x1C;
            return p - output;
        }
        if (shift_state == 3 && wParam == 0xDE) {
            *p++ = 0x1E;               /* Ctrl-~ == Ctrl-^ in xterm at least */
            return p - output;
        }

        switch (wParam) {
          case VK_NUMPAD0: keypad_key = '0'; goto numeric_keypad;
          case VK_NUMPAD1: keypad_key = '1'; goto numeric_keypad;
          case VK_NUMPAD2: keypad_key = '2'; goto numeric_keypad;
          case VK_NUMPAD3: keypad_key = '3'; goto numeric_keypad;
          case VK_NUMPAD4: keypad_key = '4'; goto numeric_keypad;
          case VK_NUMPAD5: keypad_key = '5'; goto numeric_keypad;
          case VK_NUMPAD6: keypad_key = '6'; goto numeric_keypad;
          case VK_NUMPAD7: keypad_key = '7'; goto numeric_keypad;
          case VK_NUMPAD8: keypad_key = '8'; goto numeric_keypad;
          case VK_NUMPAD9: keypad_key = '9'; goto numeric_keypad;
          case VK_DECIMAL: keypad_key = '.'; goto numeric_keypad;
          case VK_ADD: keypad_key = '+'; goto numeric_keypad;
          case VK_SUBTRACT: keypad_key = '-'; goto numeric_keypad;
          case VK_MULTIPLY: keypad_key = '*'; goto numeric_keypad;
          case VK_DIVIDE: keypad_key = '/'; goto numeric_keypad;
          case VK_EXECUTE: keypad_key = 'G'; goto numeric_keypad;
            /* also the case for VK_RETURN below can sometimes come here */
          numeric_keypad:
            /* Left Alt overrides all numeric keypad usage to act as
             * numeric character code input */
            if (left_alt) {
                if (keypad_key >= '0' && keypad_key <= '9')
                    wgf->translate_key.alt_sum = wgf->translate_key.alt_sum * 10 + keypad_key - '0';
                else
                    wgf->translate_key.alt_sum = 0;
                break;
            }

            {
                int nchars = format_numeric_keypad_key(
                    (char *)p, term, keypad_key,
                    shift_state & 1, shift_state & 2);
                if (!nchars) {
                    /*
                     * If we didn't get an escape sequence out of the
                     * numeric keypad key, then that must be because
                     * we're in Num Lock mode without application
                     * keypad enabled. In that situation we leave this
                     * keypress to the ToUnicode/ToAsciiEx handler
                     * below, which will translate it according to the
                     * appropriate keypad layout (e.g. so that what a
                     * Brit thinks of as keypad '.' can become ',' in
                     * the German layout).
                     *
                     * An exception is the keypad Return key: if we
                     * didn't get an escape sequence for that, we
                     * treat it like ordinary Return, taking into
                     * account Telnet special new line codes and
                     * config options.
                     */
                    if (keypad_key == '\r')
                        goto ordinary_return_key;
                    break;
                }

                p += nchars;
                return p - output;
            }

            int fkey_number;
          case VK_F1: fkey_number = 1; goto numbered_function_key;
          case VK_F2: fkey_number = 2; goto numbered_function_key;
          case VK_F3: fkey_number = 3; goto numbered_function_key;
          case VK_F4: fkey_number = 4; goto numbered_function_key;
          case VK_F5: fkey_number = 5; goto numbered_function_key;
          case VK_F6: fkey_number = 6; goto numbered_function_key;
          case VK_F7: fkey_number = 7; goto numbered_function_key;
          case VK_F8: fkey_number = 8; goto numbered_function_key;
          case VK_F9: fkey_number = 9; goto numbered_function_key;
          case VK_F10: fkey_number = 10; goto numbered_function_key;
          case VK_F11: fkey_number = 11; goto numbered_function_key;
          case VK_F12: fkey_number = 12; goto numbered_function_key;
          case VK_F13: fkey_number = 13; goto numbered_function_key;
          case VK_F14: fkey_number = 14; goto numbered_function_key;
          case VK_F15: fkey_number = 15; goto numbered_function_key;
          case VK_F16: fkey_number = 16; goto numbered_function_key;
          case VK_F17: fkey_number = 17; goto numbered_function_key;
          case VK_F18: fkey_number = 18; goto numbered_function_key;
          case VK_F19: fkey_number = 19; goto numbered_function_key;
          case VK_F20: fkey_number = 20; goto numbered_function_key;
          numbered_function_key:
            p += format_function_key((char *)p, term, fkey_number,
                                     shift_state & 1, shift_state & 2);
            return p - output;

            SmallKeypadKey sk_key;
          case VK_HOME: sk_key = SKK_HOME; goto small_keypad_key;
          case VK_END: sk_key = SKK_END; goto small_keypad_key;
          case VK_INSERT: sk_key = SKK_INSERT; goto small_keypad_key;
          case VK_DELETE: sk_key = SKK_DELETE; goto small_keypad_key;
          case VK_PRIOR: sk_key = SKK_PGUP; goto small_keypad_key;
          case VK_NEXT: sk_key = SKK_PGDN; goto small_keypad_key;
          small_keypad_key:
            /* These keys don't generate terminal input with Ctrl */
            if (shift_state & 2)
                break;

            p += format_small_keypad_key((char *)p, term, sk_key);
            return p - output;

            char xkey;
          case VK_UP: xkey = 'A'; goto arrow_key;
          case VK_DOWN: xkey = 'B'; goto arrow_key;
          case VK_RIGHT: xkey = 'C'; goto arrow_key;
          case VK_LEFT: xkey = 'D'; goto arrow_key;
          case VK_CLEAR: xkey = 'G'; goto arrow_key; /* close enough */
          arrow_key:
            p += format_arrow_key((char *)p, term, xkey, shift_state & 2);
            return p - output;

          case VK_RETURN:
            if (HIWORD(lParam) & KF_EXTENDED) {
                keypad_key = '\r';
                goto numeric_keypad;
            }
          ordinary_return_key:
            if (shift_state == 0 && term->cr_lf_return) {
                *p++ = '\r';
                *p++ = '\n';
                return p - output;
            } else {
                *p++ = 0x0D;
                *p++ = 0;
                return -2;
            }
        }
    }

    /* Okay we've done everything interesting; let windows deal with
     * the boring stuff */
    {
        bool capsOn = false;

        /* helg: clear CAPS LOCK state if caps lock switches to cyrillic */
        if(keystate[VK_CAPITAL] != 0 &&
           conf_get_bool(conf, CONF_xlat_capslockcyr)) {
            capsOn= !left_alt;
            keystate[VK_CAPITAL] = 0;
        }

        /* XXX how do we know what the max size of the keys array should
         * be is? There's indication on MS' website of an Inquire/InquireEx
         * functioning returning a KBINFO structure which tells us. */
        if (osPlatformId == VER_PLATFORM_WIN32_NT && p_ToUnicodeEx) {
            r = p_ToUnicodeEx(wParam, scan, keystate, keys_unicode,
                              lenof(keys_unicode), 0, kbd_layout);
        } else {
            /* XXX 'keys' parameter is declared in MSDN documentation as
             * 'LPWORD lpChar'.
             * The experience of a French user indicates that on
             * Win98, WORD[] should be passed in, but on Win2K, it should
             * be BYTE[]. German WinXP and my Win2K with "US International"
             * driver corroborate this.
             * Experimentally I've conditionalised the behaviour on the
             * Win9x/NT split, but I suspect it's worse than that.
             * See wishlist item `win-dead-keys' for more horrible detail
             * and speculations. */
            int i;
            static WORD keys[3];
            static BYTE keysb[3];
            r = ToAsciiEx(wParam, scan, keystate, keys, 0, kbd_layout);
            if (r > 0) {
                for (i = 0; i < r; i++) {
                    keysb[i] = (BYTE)keys[i];
                }
                MultiByteToWideChar(CP_ACP, 0, (LPCSTR)keysb, r,
                                    keys_unicode, lenof(keys_unicode));
            }
        }
#ifdef SHOW_TOASCII_RESULT
        if (r == 1 && !key_down) {
            if (wgf->translate_key.alt_sum) {
                if (in_utf(term) || wgf->ucsdata.dbcs_screenfont)
                    debug(", (U+%04x)", wgf->translate_key.alt_sum);
                else
                    debug(", LCH(%d)", wgf->translate_key.alt_sum);
            } else {
                debug(", ACH(%d)", keys_unicode[0]);
            }
        } else if (r > 0) {
            int r1;
            debug(", ASC(");
            for (r1 = 0; r1 < r; r1++) {
                debug("%s%d", r1 ? "," : "", keys_unicode[r1]);
            }
            debug(")");
        }
#endif
        if (r > 0) {
            WCHAR keybuf;

            p = output;
            for (i = 0; i < r; i++) {
                wchar_t wch = keys_unicode[i];

                if (wgf->compose_state == 2 && wch >= ' ' && wch < 0x80) {
                    wgf->translate_key.compose_char = wch;
                    wgf->compose_state++;
                    continue;
                }
                if (wgf->compose_state == 3 && wch >= ' ' && wch < 0x80) {
                    int nc;
                    wgf->compose_state = 0;

                    if ((nc = check_compose(wgf->translate_key.compose_char, wch)) == -1) {
                        MessageBeep(MB_ICONHAND);
                        return 0;
                    }
                    keybuf = nc;
                    term_keyinputw(term, &keybuf, 1);
                    continue;
                }

                wgf->compose_state = 0;

                if (!key_down) {
                    if (wgf->translate_key.alt_sum) {
                        if (in_utf(term) || wgf->ucsdata.dbcs_screenfont) {
                            keybuf = wgf->translate_key.alt_sum;
                            term_keyinputw(term, &keybuf, 1);
                        } else {
                            char ch = (char) wgf->translate_key.alt_sum;
                            /*
                             * We need not bother about stdin
                             * backlogs here, because in GUI PuTTY
                             * we can't do anything about it
                             * anyway; there's no means of asking
                             * Windows to hold off on KEYDOWN
                             * messages. We _have_ to buffer
                             * everything we're sent.
                             */
                            term_keyinput(term, -1, &ch, 1);
                        }
                        wgf->translate_key.alt_sum = 0;
                    } else {
                        term_keyinputw(term, &wch, 1);
                    }
                } else {
                    if(capsOn && wch < 0x80) {
                        WCHAR cbuf[2];
                        cbuf[0] = 27;
                        cbuf[1] = xlat_uskbd2cyrllic(wch);
                        term_keyinputw(term, cbuf+!left_alt, 1+!!left_alt);
                    } else {
                        WCHAR cbuf[2];
                        cbuf[0] = '\033';
                        cbuf[1] = wch;
                        term_keyinputw(term, cbuf +!left_alt, 1+!!left_alt);
                    }
                }
                show_mouseptr(wgf, false);
            }

            /* This is so the ALT-Numpad and dead keys work correctly. */
            keys_unicode[0] = 0;

            return p - output;
        }
        /* If we're definitely not building up an ALT-54321 then clear it */
        if (!left_alt)
            keys_unicode[0] = 0;
        /* If we will be using alt_sum fix the 256s */
        else if (keys_unicode[0] && (in_utf(term) || wgf->ucsdata.dbcs_screenfont))
            keys_unicode[0] = 10;
    }

    /*
     * ALT alone may or may not want to bring up the System menu.
     * If it's not meant to, we return 0 on presses or releases of
     * ALT, to show that we've swallowed the keystroke. Otherwise
     * we return -1, which means Windows will give the keystroke
     * its default handling (i.e. bring up the System menu).
     */
    if (wParam == VK_MENU && !conf_get_bool(conf, CONF_alt_only))
        return 0;

    return -1;
}

static void wintw_set_title(TermWin *tw, const char *title)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    sfree(wgf->window_name);
    wgf->window_name = dupstr(title);
    if (wgf != wgf_active) {return;}
    set_title_from_session(wgf);
}

static void wintw_set_icon_title(TermWin *tw, const char *title)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    sfree(wgf->icon_name);
    wgf->icon_name = dupstr(title);
    if (wgf != wgf_active) {return;}
    set_icon_title_from_session(wgf);
}

static void wintw_set_scrollbar(TermWin *tw, int total, int start, int page)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    wgf->si.total = total;
    wgf->si.page = page;
    wgf->si.start = start;
    if (wgf != wgf_active) {return;}
    set_scrollbar_from_session(wgf, true);
}

static bool wintw_setup_draw_ctx(TermWin *tw)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return false;}
    assert(!wgf->wintw_hdc);
    wgf->wintw_hdc = make_hdc(wgf);
    return wgf->wintw_hdc != NULL;
}

static void wintw_free_draw_ctx(TermWin *tw)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    assert(wgf->wintw_hdc);
    free_hdc(term_hwnd, wgf->wintw_hdc);
    wgf->wintw_hdc = NULL;
}

/*
 * Set up the colour palette.
 */
static void init_palette(WinGuiFrontend *wgf)
{
    wgf->pal = NULL;
    wgf->logpal = snew_plus(LOGPALETTE, (OSC4_NCOLOURS - 1) * sizeof(PALETTEENTRY));
    wgf->logpal->palVersion = 0x300;
    wgf->logpal->palNumEntries = OSC4_NCOLOURS;
    for (unsigned i = 0; i < OSC4_NCOLOURS; i++)
        wgf->logpal->palPalEntry[i].peFlags = PC_NOCOLLAPSE;
}

static void wintw_palette_set(TermWin *win, unsigned start,
                              unsigned ncolours, const rgb *colours_in)
{
    WinGuiFrontend *wgf = container_of(win, WinGuiFrontend, wintw);
    assert(start <= OSC4_NCOLOURS);
    assert(ncolours <= OSC4_NCOLOURS - start);

    if (wgf->term_palette_init)
    {
        colours_in -= start;
        start = 0;
        ncolours = OSC4_NCOLOURS;
        wgf->term_palette_init = false;
    }
    for (unsigned i = 0; i < ncolours; i++) {
        const rgb *in = &colours_in[i];
        PALETTEENTRY *out = &wgf->logpal->palPalEntry[i + start];
        out->peRed = in->r;
        out->peGreen = in->g;
        out->peBlue = in->b;
        wgf->colours[i + start] = RGB(in->r, in->g, in->b) ^ wgf->colorref_modifier;
    }

    if (wgf != wgf_active) {return;}
    realize_palette(wgf);

    if (start <= OSC4_COLOUR_bg && OSC4_COLOUR_bg < start + ncolours) {
        /* If Default Background changes, we need to ensure any space between
         * the text area and the window border is redrawn. */
        InvalidateRect(term_hwnd, NULL, true);
    }
}

void write_aclip(int clipboard, char *data, int len, bool must_deselect)
{
    HGLOBAL clipdata;
    void *lock;

    if (clipboard != CLIP_SYSTEM)
        return;

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
        return;
    lock = GlobalLock(clipdata);
    if (!lock)
        return;
    memcpy(lock, data, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (!must_deselect)
        SendMessage(term_hwnd, WM_IGNORE_CLIP, true, 0);

    if (OpenClipboard(term_hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, clipdata);
        CloseClipboard();
    } else
        GlobalFree(clipdata);

    if (!must_deselect)
        SendMessage(term_hwnd, WM_IGNORE_CLIP, false, 0);
}

typedef struct _rgbindex {
    int index;
    COLORREF ref;
} rgbindex;

int cmpCOLORREF(void *va, void *vb)
{
    COLORREF a = ((rgbindex *)va)->ref;
    COLORREF b = ((rgbindex *)vb)->ref;
    return (a < b) ? -1 : (a > b) ? +1 : 0;
}

/*
 * Note: unlike write_aclip() this will not append a nul.
 */
static void wintw_clip_write(
    TermWin *tw, int clipboard, wchar_t *data, int *attr,
    truecolour *truecolour, int len, bool must_deselect)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    Conf *conf = wgf->conf;
    HGLOBAL clipdata, clipdata2, clipdata3;
    int len2;
    void *lock, *lock2, *lock3;

    if (clipboard != CLIP_SYSTEM)
        return;

    len2 = WideCharToMultiByte(CP_ACP, 0, data, len, 0, 0, NULL, NULL);

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE,
                           len * sizeof(wchar_t));
    clipdata2 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len2);

    if (!clipdata || !clipdata2) {
        if (clipdata)
            GlobalFree(clipdata);
        if (clipdata2)
            GlobalFree(clipdata2);
        return;
    }
    if (!(lock = GlobalLock(clipdata))) {
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
        return;
    }
    if (!(lock2 = GlobalLock(clipdata2))) {
        GlobalUnlock(clipdata);
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
        return;
    }

    memcpy(lock, data, len * sizeof(wchar_t));
    WideCharToMultiByte(CP_ACP, 0, data, len, lock2, len2, NULL, NULL);

    if (conf_get_bool(conf, CONF_rtf_paste)) {
        wchar_t unitab[256];
        strbuf *rtf = strbuf_new();
        unsigned char *tdata = (unsigned char *)lock2;
        wchar_t *udata = (wchar_t *)lock;
        int uindex = 0, tindex = 0;
        int multilen, blen, alen, totallen, i;
        char before[16], after[4];
        int fgcolour,  lastfgcolour  = -1;
        int bgcolour,  lastbgcolour  = -1;
        COLORREF fg,   lastfg = -1;
        COLORREF bg,   lastbg = -1;
        int attrBold,  lastAttrBold  = 0;
        int attrUnder, lastAttrUnder = 0;
        int palette[OSC4_NCOLOURS];
        int numcolours;
        tree234 *rgbtree = NULL;
        FontSpec *font = conf_get_fontspec(conf, CONF_font);

        get_unitab(CP_ACP, unitab, 0);

        strbuf_catf(
            rtf, "{\\rtf1\\ansi\\deff0{\\fonttbl\\f0\\fmodern %s;}\\f0\\fs%d",
            font->name, font->height*2);

        /*
         * Add colour palette
         * {\colortbl ;\red255\green0\blue0;\red0\green0\blue128;}
         */

        /*
         * First - Determine all colours in use
         *    o  Foregound and background colours share the same palette
         */
        if (attr) {
            memset(palette, 0, sizeof(palette));
            for (i = 0; i < (len-1); i++) {
                fgcolour = ((attr[i] & ATTR_FGMASK) >> ATTR_FGSHIFT);
                bgcolour = ((attr[i] & ATTR_BGMASK) >> ATTR_BGSHIFT);

                if (attr[i] & ATTR_REVERSE) {
                    int tmpcolour = fgcolour;   /* Swap foreground and background */
                    fgcolour = bgcolour;
                    bgcolour = tmpcolour;
                }

                if (wgf->bold_colours && (attr[i] & ATTR_BOLD)) {
                    if (fgcolour  <   8)        /* ANSI colours */
                        fgcolour +=   8;
                    else if (fgcolour >= 256)   /* Default colours */
                        fgcolour ++;
                }

                if ((attr[i] & ATTR_BLINK)) {
                    if (bgcolour  <   8)        /* ANSI colours */
                        bgcolour +=   8;
                    else if (bgcolour >= 256)   /* Default colours */
                        bgcolour ++;
                }

                palette[fgcolour]++;
                palette[bgcolour]++;
            }

            if (truecolour) {
                rgbtree = newtree234(cmpCOLORREF);
                for (i = 0; i < (len-1); i++) {
                    if (truecolour[i].fg.enabled) {
                        rgbindex *rgbp = snew(rgbindex);
                        rgbp->ref = RGB(truecolour[i].fg.r,
                                        truecolour[i].fg.g,
                                        truecolour[i].fg.b);
                        if (add234(rgbtree, rgbp) != rgbp)
                            sfree(rgbp);
                    }
                    if (truecolour[i].bg.enabled) {
                        rgbindex *rgbp = snew(rgbindex);
                        rgbp->ref = RGB(truecolour[i].bg.r,
                                        truecolour[i].bg.g,
                                        truecolour[i].bg.b);
                        if (add234(rgbtree, rgbp) != rgbp)
                            sfree(rgbp);
                    }
                }
            }

            /*
             * Next - Create a reduced palette
             */
            numcolours = 0;
            for (i = 0; i < OSC4_NCOLOURS; i++) {
                if (palette[i] != 0)
                    palette[i]  = ++numcolours;
            }

            if (rgbtree) {
                rgbindex *rgbp;
                for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
                    rgbp->index = ++numcolours;
            }

            /*
             * Finally - Write the colour table
             */
            put_datapl(rtf, PTRLEN_LITERAL("{\\colortbl ;"));

            for (i = 0; i < OSC4_NCOLOURS; i++) {
                if (palette[i] != 0) {
                    const PALETTEENTRY *pe = &wgf->logpal->palPalEntry[i];
                    strbuf_catf(rtf, "\\red%d\\green%d\\blue%d;",
                                pe->peRed, pe->peGreen, pe->peBlue);
                }
            }
            if (rgbtree) {
                rgbindex *rgbp;
                for (i = 0; (rgbp = index234(rgbtree, i)) != NULL; i++)
                    strbuf_catf(rtf, "\\red%d\\green%d\\blue%d;",
                                GetRValue(rgbp->ref), GetGValue(rgbp->ref),
                                GetBValue(rgbp->ref));
            }
            put_datapl(rtf, PTRLEN_LITERAL("}"));
        }

        /*
         * We want to construct a piece of RTF that specifies the
         * same Unicode text. To do this we will read back in
         * parallel from the Unicode data in `udata' and the
         * non-Unicode data in `tdata'. For each character in
         * `tdata' which becomes the right thing in `udata' when
         * looked up in `unitab', we just copy straight over from
         * tdata. For each one that doesn't, we must WCToMB it
         * individually and produce a \u escape sequence.
         *
         * It would probably be more robust to just bite the bullet
         * and WCToMB each individual Unicode character one by one,
         * then MBToWC each one back to see if it was an accurate
         * translation; but that strikes me as a horrifying number
         * of Windows API calls so I want to see if this faster way
         * will work. If it screws up badly we can always revert to
         * the simple and slow way.
         */
        while (tindex < len2 && uindex < len &&
               tdata[tindex] && udata[uindex]) {
            if (tindex + 1 < len2 &&
                tdata[tindex] == '\r' &&
                tdata[tindex+1] == '\n') {
                tindex++;
                uindex++;
            }

            /*
             * Set text attributes
             */
            if (attr) {
                /*
                 * Determine foreground and background colours
                 */
                if (truecolour && truecolour[tindex].fg.enabled) {
                    fgcolour = -1;
                    fg = RGB(truecolour[tindex].fg.r,
                             truecolour[tindex].fg.g,
                             truecolour[tindex].fg.b);
                } else {
                    fgcolour = ((attr[tindex] & ATTR_FGMASK) >> ATTR_FGSHIFT);
                    fg = -1;
                }

                if (truecolour && truecolour[tindex].bg.enabled) {
                    bgcolour = -1;
                    bg = RGB(truecolour[tindex].bg.r,
                             truecolour[tindex].bg.g,
                             truecolour[tindex].bg.b);
                } else {
                    bgcolour = ((attr[tindex] & ATTR_BGMASK) >> ATTR_BGSHIFT);
                    bg = -1;
                }

                if (attr[tindex] & ATTR_REVERSE) {
                    int tmpcolour = fgcolour;       /* Swap foreground and background */
                    fgcolour = bgcolour;
                    bgcolour = tmpcolour;

                    COLORREF tmpref = fg;
                    fg = bg;
                    bg = tmpref;
                }

                if (wgf->bold_colours && (attr[tindex] & ATTR_BOLD) && (fgcolour >= 0)) {
                    if (fgcolour  <   8)            /* ANSI colours */
                        fgcolour +=   8;
                    else if (fgcolour >= 256)       /* Default colours */
                        fgcolour ++;
                }

                if ((attr[tindex] & ATTR_BLINK) && (bgcolour >= 0)) {
                    if (bgcolour  <   8)            /* ANSI colours */
                        bgcolour +=   8;
                    else if (bgcolour >= 256)       /* Default colours */
                        bgcolour ++;
                }

                /*
                 * Collect other attributes
                 */
                if (wgf->bold_font_mode != BOLD_NONE)
                    attrBold  = attr[tindex] & ATTR_BOLD;
                else
                    attrBold  = 0;

                attrUnder = attr[tindex] & ATTR_UNDER;

                /*
                 * Reverse video
                 *   o  If video isn't reversed, ignore colour attributes for default foregound
                 *      or background.
                 *   o  Special case where bolded text is displayed using the default foregound
                 *      and background colours - force to bolded RTF.
                 */
                if (!(attr[tindex] & ATTR_REVERSE)) {
                    if (bgcolour >= 256)            /* Default color */
                        bgcolour  = -1;             /* No coloring */

                    if (fgcolour >= 256) {          /* Default colour */
                        if (wgf->bold_colours && (fgcolour & 1) && bgcolour == -1)
                            attrBold = ATTR_BOLD;   /* Emphasize text with bold attribute */

                        fgcolour  = -1;             /* No coloring */
                    }
                }

                /*
                 * Write RTF text attributes
                 */
                if ((lastfgcolour != fgcolour) || (lastfg != fg)) {
                    lastfgcolour  = fgcolour;
                    lastfg        = fg;
                    if (fg == -1) {
                        strbuf_catf(rtf, "\\cf%d ",
                                    (fgcolour >= 0) ? palette[fgcolour] : 0);
                    } else {
                        rgbindex rgb, *rgbp;
                        rgb.ref = fg;
                        if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
                            strbuf_catf(rtf, "\\cf%d ", rgbp->index);
                    }
                }

                if ((lastbgcolour != bgcolour) || (lastbg != bg)) {
                    lastbgcolour  = bgcolour;
                    lastbg        = bg;
                    if (bg == -1)
                        strbuf_catf(rtf, "\\highlight%d ",
                                    (bgcolour >= 0) ? palette[bgcolour] : 0);
                    else {
                        rgbindex rgb, *rgbp;
                        rgb.ref = bg;
                        if ((rgbp = find234(rgbtree, &rgb, NULL)) != NULL)
                            strbuf_catf(rtf, "\\highlight%d ", rgbp->index);
                    }
                }

                if (lastAttrBold != attrBold) {
                    lastAttrBold  = attrBold;
                    put_datapl(rtf, attrBold ?
                               PTRLEN_LITERAL("\\b ") :
                               PTRLEN_LITERAL("\\b0 "));
                }

                if (lastAttrUnder != attrUnder) {
                    lastAttrUnder  = attrUnder;
                    put_datapl(rtf, attrUnder ?
                               PTRLEN_LITERAL("\\ul ") :
                               PTRLEN_LITERAL("\\ulnone "));
                }
            }

            if (unitab[tdata[tindex]] == udata[uindex]) {
                multilen = 1;
                before[0] = '\0';
                after[0] = '\0';
                blen = alen = 0;
            } else {
                multilen = WideCharToMultiByte(CP_ACP, 0, unitab+uindex, 1,
                                               NULL, 0, NULL, NULL);
                if (multilen != 1) {
                    blen = sprintf(before, "{\\uc%d\\u%d", (int)multilen,
                                   (int)udata[uindex]);
                    alen = 1; strcpy(after, "}");
                } else {
                    blen = sprintf(before, "\\u%d", (int)udata[uindex]);
                    alen = 0; after[0] = '\0';
                }
            }
            assert(tindex + multilen <= len2);
            totallen = blen + alen;
            for (i = 0; i < multilen; i++) {
                if (tdata[tindex+i] == '\\' ||
                    tdata[tindex+i] == '{' ||
                    tdata[tindex+i] == '}')
                    totallen += 2;
                else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A)
                    totallen += 6;     /* \par\r\n */
                else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20)
                    totallen += 4;
                else
                    totallen++;
            }

            put_data(rtf, before, blen);
            for (i = 0; i < multilen; i++) {
                if (tdata[tindex+i] == '\\' ||
                    tdata[tindex+i] == '{' ||
                    tdata[tindex+i] == '}') {
                    put_byte(rtf, '\\');
                    put_byte(rtf, tdata[tindex+i]);
                } else if (tdata[tindex+i] == 0x0D || tdata[tindex+i] == 0x0A) {
                    put_datapl(rtf, PTRLEN_LITERAL("\\par\r\n"));
                } else if (tdata[tindex+i] > 0x7E || tdata[tindex+i] < 0x20) {
                    strbuf_catf(rtf, "\\'%02x", tdata[tindex+i]);
                } else {
                    put_byte(rtf, tdata[tindex+i]);
                }
            }
            put_data(rtf, after, alen);

            tindex += multilen;
            uindex++;
        }

        put_datapl(rtf, PTRLEN_LITERAL("}\0\0")); /* Terminate RTF stream */

        clipdata3 = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, rtf->len);
        if (clipdata3 && (lock3 = GlobalLock(clipdata3)) != NULL) {
            memcpy(lock3, rtf->u, rtf->len);
            GlobalUnlock(clipdata3);
        }
        strbuf_free(rtf);

        if (rgbtree) {
            rgbindex *rgbp;
            while ((rgbp = delpos234(rgbtree, 0)) != NULL)
                sfree(rgbp);
            freetree234(rgbtree);
        }
    } else
        clipdata3 = NULL;

    GlobalUnlock(clipdata);
    GlobalUnlock(clipdata2);

    if (!must_deselect)
        SendMessage(term_hwnd, WM_IGNORE_CLIP, true, 0);

    if (OpenClipboard(term_hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, clipdata);
        SetClipboardData(CF_TEXT, clipdata2);
        if (clipdata3)
            SetClipboardData(RegisterClipboardFormat(CF_RTF), clipdata3);
        CloseClipboard();
    } else {
        GlobalFree(clipdata);
        GlobalFree(clipdata2);
    }

    if (!must_deselect)
        SendMessage(term_hwnd, WM_IGNORE_CLIP, false, 0);
}

static DWORD WINAPI clipboard_read_threadfunc(void *param)
{
    HWND hwnd = (HWND)param;
    HGLOBAL clipdata;

    if (OpenClipboard(NULL)) {
        if ((clipdata = GetClipboardData(CF_UNICODETEXT))) {
            process_clipdata(hwnd, clipdata, true);
        } else if ((clipdata = GetClipboardData(CF_TEXT))) {
            process_clipdata(hwnd, clipdata, false);
        }
        CloseClipboard();
    }

    return 0;
}

static void process_clipdata(HWND hwnd, HGLOBAL clipdata, bool unicode)
{
    wchar_t *clipboard_contents = NULL;
    size_t clipboard_length = 0;

    if (unicode) {
        wchar_t *p = GlobalLock(clipdata);
        wchar_t *p2;

        if (p) {
            /* Unwilling to rely on Windows having wcslen() */
            for (p2 = p; *p2; p2++);
            clipboard_length = p2 - p;
            clipboard_contents = snewn(clipboard_length + 1, wchar_t);
            memcpy(clipboard_contents, p, clipboard_length * sizeof(wchar_t));
            clipboard_contents[clipboard_length] = L'\0';
        }
    } else {
        char *s = GlobalLock(clipdata);
        int i;

        if (s) {
            i = MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1, 0, 0);
            clipboard_contents = snewn(i, wchar_t);
            MultiByteToWideChar(CP_ACP, 0, s, strlen(s) + 1,
                                clipboard_contents, i);
            clipboard_length = i - 1;
            clipboard_contents[clipboard_length] = L'\0';
        }
    }
    GlobalUnlock(clipdata);
    if (clipboard_contents) {
        PostMessage(hwnd, WM_GOT_CLIPDATA,
                    (WPARAM)clipboard_length, (LPARAM)clipboard_contents);
    }
}

static void paste_clipdata(Terminal *term, WPARAM wParam, LPARAM lParam)
{
    wchar_t *clipboard_contents = (wchar_t *)lParam;
    size_t clipboard_length = (size_t)wParam;

    if (confirm_paste && wcschr(clipboard_contents, L'\r')) {
        RECT ss;
        get_fullscreen_rect(&ss);
        if (!show_paste_confirm(&ss, &clipboard_contents, &clipboard_length)) {
            return;
        }
    }
    term_do_paste(term, clipboard_contents, clipboard_length);
    sfree(clipboard_contents);
}

static void wintw_clip_request_paste(TermWin *tw, int clipboard)
{
    assert(clipboard == CLIP_SYSTEM);

    /*
     * I always thought pasting was synchronous in Windows; the
     * clipboard access functions certainly _look_ synchronous,
     * unlike the X ones. But in fact it seems that in some
     * situations the contents of the clipboard might not be
     * immediately available, and the clipboard-reading functions
     * may block. This leads to trouble if the application
     * delivering the clipboard data has to get hold of it by -
     * for example - talking over a network connection which is
     * forwarded through this very PuTTY.
     *
     * Hence, we spawn a subthread to read the clipboard, and do
     * our paste when it's finished. The thread will send a
     * message back to our main window when it terminates, and
     * that tells us it's OK to paste.
     */
    DWORD in_threadid; /* required for Win9x */
    HANDLE hThread = CreateThread(NULL, 0, clipboard_read_threadfunc,
                                  term_hwnd, 0, &in_threadid);
    if (hThread)
        CloseHandle(hThread);          /* we don't need the thread handle */
}

/*
 * Print a modal (Really Bad) message box and perform a fatal exit.
 */
void modalfatalbox(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(wgf_active, true);
    title = dupprintf("%s Fatal Error", appname);
    MessageBox(frame_hwnd, message, title,
               MB_SYSTEMMODAL | MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
    cleanup_exit(1);
}

/*
 * Print a message box and don't close the connection.
 */
void nonfatal(const char *fmt, ...)
{
    va_list ap;
    char *message, *title;

    va_start(ap, fmt);
    message = dupvprintf(fmt, ap);
    va_end(ap);
    show_mouseptr(wgf_active, true);
    title = dupprintf("%s Error", appname);
    MessageBox(frame_hwnd, message, title, MB_ICONERROR | MB_OK);
    sfree(message);
    sfree(title);
}

static bool flash_window_ex(DWORD dwFlags, UINT uCount, DWORD dwTimeout)
{
    if (p_FlashWindowEx) {
        FLASHWINFO fi;
        fi.cbSize = sizeof(fi);
        fi.hwnd = frame_hwnd;
        fi.dwFlags = dwFlags;
        fi.uCount = uCount;
        fi.dwTimeout = dwTimeout;
        return (*p_FlashWindowEx)(&fi);
    }
    else
        return false; /* shrug */
}

static long next_flash;
static bool flashing = false;

/*
 * Timer for platforms where we must maintain window flashing manually
 * (e.g., Win95).
 */
static void flash_window_timer(void *ctx, unsigned long now)
{
    if (flashing && now == next_flash) {
        flash_window(1);
    }
}

/*
 * Manage window caption / taskbar flashing, if enabled.
 * 0 = stop, 1 = maintain, 2 = start
 */
static void flash_window(int mode)
{
    Conf *conf = wgf_active->conf;
    int beep_ind = conf_get_int(conf, CONF_beep_ind);
    if ((mode == 0) || (beep_ind == B_IND_DISABLED)) {
        /* stop */
        if (flashing) {
            flashing = false;
            if (p_FlashWindowEx)
                flash_window_ex(FLASHW_STOP, 0, 0);
            else
                FlashWindow(frame_hwnd, false);
        }

    } else if (mode == 2) {
        /* start */
        if (!flashing) {
            flashing = true;
            if (p_FlashWindowEx) {
                /* For so-called "steady" mode, we use uCount=2, which
                 * seems to be the traditional number of flashes used
                 * by user notifications (e.g., by Explorer).
                 * uCount=0 appears to enable continuous flashing, per
                 * "flashing" mode, although I haven't seen this
                 * documented. */
                flash_window_ex(FLASHW_ALL | FLASHW_TIMER,
                                (beep_ind == B_IND_FLASH ? 0 : 2),
                                0 /* system cursor blink rate */);
                /* No need to schedule timer */
            } else {
                FlashWindow(frame_hwnd, true);
                next_flash = schedule_timer(450, flash_window_timer,
                                            frame_hwnd);
            }
        }

    } else if ((mode == 1) && (beep_ind == B_IND_FLASH)) {
        /* maintain */
        if (flashing && !p_FlashWindowEx) {
            FlashWindow(frame_hwnd, true);    /* toggle */
            next_flash = schedule_timer(450, flash_window_timer,
                                        frame_hwnd);
        }
    }
}

/*
 * Beep.
 */
static void wintw_bell(TermWin *tw, int mode)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    Conf *conf = wgf->conf;
    Terminal *term = wgf->term;
    if (mode == BELL_DEFAULT) {
        /*
         * For MessageBeep style bells, we want to be careful of
         * timing, because they don't have the nice property of
         * PlaySound bells that each one cancels the previous
         * active one. So we limit the rate to one per 50ms or so.
         */
        static long lastbeep = 0;
        long beepdiff;

        beepdiff = GetTickCount() - lastbeep;
        if (beepdiff >= 0 && beepdiff < 50)
            return;
        MessageBeep(MB_OK);
        /*
         * The above MessageBeep call takes time, so we record the
         * time _after_ it finishes rather than before it starts.
         */
        lastbeep = GetTickCount();
    } else if (mode == BELL_WAVEFILE) {
        Filename *bell_wavefile = conf_get_filename(conf, CONF_bell_wavefile);
        if (!p_PlaySound || !p_PlaySound(bell_wavefile->path, NULL,
                         SND_ASYNC | SND_FILENAME)) {
            char *buf, *otherbuf;
            show_mouseptr(wgf, true);
            buf = dupprintf(
                "Unable to play sound file\n%s\nUsing default sound instead",
                bell_wavefile->path);
            otherbuf = dupprintf("%s Sound Error", appname);
            MessageBox(frame_hwnd, buf, otherbuf,
                       MB_OK | MB_ICONEXCLAMATION);
            sfree(buf);
            sfree(otherbuf);
            conf_set_int(conf, CONF_beep, BELL_DEFAULT);
        }
    } else if (mode == BELL_PCSPEAKER) {
        static long lastbeep = 0;
        long beepdiff;

        beepdiff = GetTickCount() - lastbeep;
        if (beepdiff >= 0 && beepdiff < 50)
            return;

        /*
         * We must beep in different ways depending on whether this
         * is a 95-series or NT-series OS.
         */
        if (osPlatformId == VER_PLATFORM_WIN32_NT)
            Beep(800, 100);
        else
            MessageBeep(-1);
        lastbeep = GetTickCount();
    }
    /* Otherwise, either visual bell or disabled; do nothing here */
    if (!term->has_focus) {
        flash_window(2);               /* start */
    }
}

/*
 * Minimise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_minimised(TermWin *tw, bool minimised)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    if (IsIconic(frame_hwnd)) {
        if (!minimised)
            ShowWindow(frame_hwnd, SW_RESTORE);
    } else {
        if (minimised)
            ShowWindow(frame_hwnd, SW_MINIMIZE);
    }
}

/*
 * Move the window in response to a server-side request.
 */
static void wintw_move(TermWin *tw, int x, int y)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    Conf *conf = wgf->conf;
    int resize_action = conf_get_int(conf, CONF_resize_action);
    if (resize_action == RESIZE_DISABLED ||
        resize_action == RESIZE_FONT ||
        IsZoomed(frame_hwnd))
       return;

    SetWindowPos(frame_hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Move the window to the top or bottom of the z-order in response
 * to a server-side request.
 */
static void wintw_set_zorder(TermWin *tw, bool top)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    if (conf_get_bool(wgf->conf, CONF_alwaysontop))
        return;                        /* ignore */
    SetWindowPos(frame_hwnd, top ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
}

/*
 * Refresh the window in response to a server-side request.
 */
static void wintw_refresh(TermWin *tw)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    InvalidateRect(term_hwnd, NULL, true);
}

/*
 * Maximise or restore the window in response to a server-side
 * request.
 */
static void wintw_set_maximised(TermWin *tw, bool maximised)
{
    WinGuiFrontend *wgf = container_of(tw, WinGuiFrontend, wintw);
    if (wgf != wgf_active) {return;}
    if (IsZoomed(frame_hwnd)) {
        if (!maximised)
            ShowWindow(frame_hwnd, SW_RESTORE);
    } else {
        if (maximised)
            ShowWindow(frame_hwnd, SW_MAXIMIZE);
    }
}

/*
 * See if we're in full-screen mode.
 */
static bool is_full_screen()
{
    if (!IsZoomed(frame_hwnd))
        return false;
    if (GetWindowLongPtr(frame_hwnd, GWL_STYLE) & WS_CAPTION)
        return false;
    return true;
}

/* Get the rect/size of a full screen window using the nearest available
 * monitor in multimon systems; default to something sensible if only
 * one monitor is present. */
static bool get_fullscreen_rect(RECT * ss)
{
#if defined(MONITOR_DEFAULTTONEAREST) && !defined(NO_MULTIMON)
        HMONITOR mon;
        MONITORINFO mi;
        mon = MonitorFromWindow(frame_hwnd, MONITOR_DEFAULTTONEAREST);
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(mon, &mi);

        /* structure copy */
        *ss = mi.rcMonitor;
        return true;
#else
/* could also use code like this:
        ss->left = ss->top = 0;
        ss->right = GetSystemMetrics(SM_CXSCREEN);
        ss->bottom = GetSystemMetrics(SM_CYSCREEN);
*/
        return GetClientRect(GetDesktopWindow(), ss);
#endif
}


/*
 * Go full-screen. This should only be called when we are already
 * maximised.
 */
static void make_full_screen()
{
    Conf *conf = wgf_active->conf;
    DWORD style;
        RECT ss;

    assert(IsZoomed(frame_hwnd));

        if (is_full_screen())
                return;

    /* Remove the window furniture. */
    style = GetWindowLongPtr(frame_hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_BORDER | WS_THICKFRAME);
    if (conf_get_bool(conf, CONF_scrollbar_in_fullscreen))
        style |= WS_VSCROLL;
    else
        style &= ~WS_VSCROLL;
    SetWindowLongPtr(frame_hwnd, GWL_STYLE, style);

    /* Resize ourselves to exactly cover the nearest monitor. */
        get_fullscreen_rect(&ss);
    SetWindowPos(frame_hwnd, HWND_TOP, ss.left, ss.top,
                 ss.right - ss.left, ss.bottom - ss.top, SWP_FRAMECHANGED);

    /* We may have changed size as a result */

    reset_window(wgf_active, 0);

    /* Tick the menu item in the System and context menus. */
    check_menu_item(IDM_FULLSCREEN, MF_CHECKED);
}

/*
 * Clear the full-screen attributes.
 */
static void clear_full_screen()
{
    Conf *conf = wgf_active->conf;
    DWORD oldstyle, style;

    /* Reinstate the window furniture. */
    style = oldstyle = GetWindowLongPtr(frame_hwnd, GWL_STYLE);
    style |= WS_CAPTION | WS_BORDER;
    if (conf_get_int(conf, CONF_resize_action) == RESIZE_DISABLED)
        style &= ~WS_THICKFRAME;
    else
        style |= WS_THICKFRAME;
    if (conf_get_bool(conf, CONF_scrollbar))
        style |= WS_VSCROLL;
    else
        style &= ~WS_VSCROLL;
    if (style != oldstyle) {
        SetWindowLongPtr(frame_hwnd, GWL_STYLE, style);
        SetWindowPos(frame_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED);
    }

    /* Untick the menu item in the System and context menus. */
    check_menu_item(IDM_FULLSCREEN, MF_UNCHECKED);
}

/*
 * Toggle full-screen mode.
 */
static void flip_full_screen()
{
    if (is_full_screen()) {
        ShowWindow(frame_hwnd, SW_RESTORE);
    } else if (IsZoomed(frame_hwnd)) {
        make_full_screen();
    } else {
        SendMessage(frame_hwnd, WM_FULLSCR_ON_MAX, 0, 0);
        ShowWindow(frame_hwnd, SW_MAXIMIZE);
    }
}

static size_t win_seat_output(Seat *seat, bool is_stderr,
                              const void *data, size_t len)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    if (wgf != wgf_active && len > 0) {
        tab_bar_set_tab_notified(wgf->tab_index);
    }
    return term_data(term, is_stderr, data, len);
}

static bool win_seat_eof(Seat *seat)
{
    return true;   /* do respond to incoming EOF with outgoing */
}

static int win_seat_get_userpass_input(
    Seat *seat, prompts_t *p, bufchain *input)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    int ret;
    ret = cmdline_get_passwd_input(p);
    if (ret == -1)
        ret = term_get_userpass_input(term, p, input);
    return ret;
}

static bool win_seat_set_trust_status(Seat *seat, bool trusted)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    term_set_trust_status(term, trusted);
    return true;
}

static bool win_seat_get_cursor_position(Seat *seat, int *x, int *y)
{
    WinGuiFrontend *wgf = container_of(seat, WinGuiFrontend, seat);
    Terminal *term = wgf->term;
    term_get_cursor_position(term, x, y);
    return true;
}

static bool win_seat_get_window_pixel_size(Seat *seat, int *x, int *y)
{
    RECT r;
    GetWindowRect(frame_hwnd, &r);
    *x = r.right - r.left;
    *y = r.bottom - r.top;
    return true;
}

#include "frame.c"
