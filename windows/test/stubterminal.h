#ifndef STUBPUTTY_H
#define STUBPUTTY_H

#include <stdbool.h>
#include <stddef.h>

#define CONF_resize_action 0
#define CONF_window_border 1
#define CONF_height 2
#define CONF_width 3
#define CONF_savelines 4
#define CONF_font 5
#define CONF_scrollbar 6
#define CONF_scrollbar_in_fullscreen 7
#define CONF_sunken_edge 8
#define CONF_logtype 9
#define CONF_protocol 10
#define CONF_win_name_always 11
#define CONF_try_palette 12
#define CONF_host 13
#define CONF_warn_on_close 14
#define CONF_line_codepage 15
#define CONF_font_quality 16
#define CONF_vtmode 17
#define CONF_bold_style 18
#define CONF_hide_mouseptr 19
#define CONF_colours 20

#define CONF_NCOLOURS 22

#define PROT_TELNET 1
#define PROT_SSH 3

typedef enum ResizeAction {
    RESIZE_DISABLED,
    RESIZE_TERM,
    RESIZE_FONT,
    RESIZE_EITHER
} ResizeAction;

typedef struct FontSpec {
    int height;
    char name[2];
    bool isbold;
    int charset;
} FontSpec;

typedef struct Conf Conf;

int conf_get_int(Conf *conf, int key);
void conf_set_int(Conf *conf, int key, int value);
bool conf_get_bool(Conf *conf, int key);
void conf_set_bool(Conf *conf, int key, bool value);
FontSpec *conf_get_fontspec(Conf *conf, int key);
const char *conf_get_str(Conf *conf, int key);
void conf_set_str(Conf *conf, int key, const char *value);
int conf_get_int_int(Conf *conf, int key, int subkey);
Conf *conf_new(void);
Conf *conf_copy(Conf *conf);
void conf_free(Conf *conf);

#define ATTR_ERASE 0x80000000

typedef struct {
    int y, x;
} pos;

typedef struct termchar {
    unsigned long attr;
} termchar;

typedef struct TermWin {
    const void *vt;
} TermWin;

typedef struct Ldisc Ldisc;
typedef struct LogContext LogContext;
struct unicode_data {};

typedef struct Terminal {
    int cols;
    int rows;
    int savelines;
    bool has_focus;
    pos curs;
    Ldisc *ldisc;
    termchar basic_erase_char;
    termchar erase_char;
} Terminal;

void term_size(Terminal *term, int rows, int cols, int);
void term_set_focus(Terminal *term, bool has_focus);
Terminal *term_init(Conf *, struct unicode_data *, TermWin *);
void term_free(Terminal *term);
void term_provide_logctx(Terminal *, LogContext *);
void term_update_sbar(Terminal *);
void term_invalidate(Terminal *);
void term_scroll(Terminal *, int, int);
void term_update(Terminal *term);
void term_notify_minimised(Terminal *, bool);
void term_notify_window_size_pixels(Terminal *term, int, int);

#define snewn(n, type) safemalloc(n, sizeof(type))
#define smalloc malloc
#define sfree free

void *safemalloc(size_t n, size_t size);
char *dupstr(const char *str);
wchar_t *dup_mb_to_wc(int, const char *str);
char *dupprintf(const char *fmt, ...);

bool conf_launchable(Conf *conf);

struct Conf {
    ResizeAction resize_action;
    int width;
    int height;
    bool scrollbar;
    bool scrollbar_in_fullscreen;
    FontSpec fontspec;
    int window_border;
    bool sunken_edge;
    bool try_palette;
    bool hide_mouseptr;

    int protocol;
    char host[16];

    unsigned char colors[CONF_NCOLOURS][3];
};

typedef struct SeatDialogPromptDescriptions SeatDialogPromptDescriptions;
typedef int SeatPromptResult;
typedef struct SeatDialogText SeatDialogText;
typedef int HelpCtx;
typedef struct Filename Filename;
typedef struct Backend Backend;
typedef int cmdline_get_passwd_input_state;
typedef struct SessionSpecial SessionSpecial;
typedef enum BusyStatus {BUSY_NOT, BUSY_WAITING, BUSY_CPU } BusyStatus;

typedef struct Seat {
    const void *vt;
} Seat;

typedef struct LogPolicy {
    const void *vt;
} LogPolicy;

typedef struct BackendVtable {
    char *(*close_warn_text)(Backend *be);
} BackendVtable;

typedef struct Backend {
    const BackendVtable *vt;
} Backend;

#endif
