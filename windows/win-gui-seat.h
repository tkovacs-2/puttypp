/*
 * Main state structure for an instance of the Windows PuTTY front
 * end, containing all the PuTTY objects and all the Windows API
 * resources like the window handle.
 */

#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

typedef struct eventlog_stuff {
    char *events_initial[LOGEVENT_INITIAL_MAX];
    char *events_circular[LOGEVENT_CIRCULAR_MAX];
    int ninitial, ncircular, circular_first;
} eventlog_stuff;

bool do_config_pp(HWND, Conf *, const char **);
bool do_reconfig_pp(HWND, Conf *, const char **, int);
void showeventlog_pp(HWND, eventlog_stuff *);
void reseteventlog(eventlog_stuff *);

const SeatDialogPromptDescriptions *win_seat_prompt_descriptions(Seat *seat);

SeatPromptResult dlg_confirm_ssh_host_key(
    HWND hwnd, Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx);
SeatPromptResult dlg_confirm_weak_crypto_primitive(
    SeatDialogText *text);
SeatPromptResult dlg_confirm_weak_cached_hostkey(
    SeatDialogText *text);

void dlg_eventlog(eventlog_stuff *es, const char *string);
int dlg_askappend(Filename *filename);

#define PROT_CONPTY PROTOCOL_LIMIT
#define PROT_SFTP (PROTOCOL_LIMIT+1)

typedef struct WinGuiSession WinGuiSession;

struct PopupMenu {
    HMENU menu;
};
enum { SYSMENU, CTXMENU };             /* indices into popup_menus field */

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

enum BoldMode {
    BOLD_NONE, BOLD_SHADOW, BOLD_FONT
};
enum UnderlineMode {
    UND_LINE, UND_FONT
};

struct WinGuiSession {
    Seat seat;
    TermWin termwin;
    LogPolicy logpolicy;

    HWND term_hwnd;

    int font_width, font_height;
    bool font_dualwidth, font_varpitch;
    int offset_width, offset_height;

    HBITMAP caretbm;
    int caret_x, caret_y;

    Ldisc *ldisc;
    Backend *backend;

    cmdline_get_passwd_input_state cmdline_get_passwd_state;

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
    enum BoldMode bold_font_mode;

    bool bold_colours;
    enum UnderlineMode und_mode;
    int descent, font_strikethrough_y;

    COLORREF colours[OSC4_NCOLOURS];
    HPALETTE pal;
    LPLOGPALETTE logpal;
    bool tried_pal;
    COLORREF colorref_modifier;

    bool send_raw_mouse;
    int wheel_accumulator;

    bool pointer_indicates_raw_mouse;

    BusyStatus busy_status;

    wchar_t *window_name, *icon_name;

    int alt_numberpad_accumulator;
    int compose_state;
    int compose_char;
    WPARAM compose_keycode;

    HDC wintw_hdc;

    bool need_backend_resize;

    bool ignore_clip;
    wchar_t pending_surrogate;

    HICON trust_icon;
    eventlog_stuff eventlogstuff;

    int session_id;
    const char *session_name;
    bool remote_closed;
    bool delete_session;
    int remote_exitcode;
    int tab_index;
    bool cursor_visible;
    bool cursor_forced_visible;
    struct {
      bool was_zoomed;
      int font_width;
      int font_height;
    } resize_either;
    bool term_palette_init;
    int font_dpi;
    struct {
      wchar_t *pattern;
      int pattern_buffer_len;
      int pattern_len;
      bool ignore_case;
      bool whole_word;
      bool data_arrived;
      bool update_finddlg_pending;
    } find;
};
