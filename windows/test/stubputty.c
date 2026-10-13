
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
#define CONF_host 13
#define CONF_warn_on_close 14
#define CONF_line_codepage 15
#define CONF_font_quality 16
#define CONF_vtmode 17
#define CONF_bold_style 18

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

typedef struct Rgb {
    unsigned char r, g, b;
} Rgb;

typedef struct Conf {
    ResizeAction resize_action;
    int width;
    int height;
    bool scrollbar;
    bool scrollbar_in_fullscreen;
    FontSpec fontspec;
    int window_border;
    bool sunken_edge;

    int protocol;
    char host[16];

    Rgb colors[CONF_NCOLOURS];
} Conf;

int conf_get_int(Conf *conf, int key) {
    switch (key) {
        case CONF_resize_action:
            return conf->resize_action;
        case CONF_window_border:
            return conf->window_border;
        case CONF_height:
            return conf->height;
        case CONF_width:
            return conf->width;
        case CONF_savelines:
            return 1;
        case CONF_protocol:
            return conf->protocol;
        case CONF_font_quality:
            return 0;
        case CONF_vtmode:
            return 0;
        case CONF_bold_style:
            return 0;
    }
    assert(false);
}

void conf_set_int(Conf *conf, int key, int value) {
    switch (key) {
        case CONF_resize_action:
            conf->resize_action = value;
            break;
        case CONF_window_border:
            conf->window_border = value;
            break;
        case CONF_height:
            conf->height = value;
            break;
        case CONF_width:
            conf->width = value;
            break;
        case CONF_logtype:
            break;
        default:
            assert(false);
    }
}

bool conf_get_bool(Conf *conf, int key) {
    switch (key) {
        case CONF_scrollbar:
            return conf->scrollbar;
        case CONF_scrollbar_in_fullscreen:
            return conf->scrollbar_in_fullscreen;
        case CONF_sunken_edge:
            return conf->sunken_edge;
        case CONF_warn_on_close:
            return true;
        case CONF_win_name_always:
            return false;
    }
    assert(false);
}

void conf_set_bool(Conf *conf, int key, bool value) {
    switch (key) {
        case CONF_scrollbar:
            conf->scrollbar = value;
            break;
        case CONF_scrollbar_in_fullscreen:
            conf->scrollbar_in_fullscreen = value;
            break;
        case CONF_sunken_edge:
            conf->sunken_edge = value;
            break;
        default:
            assert(false);
    }
}

FontSpec *conf_get_fontspec(Conf *conf, int key) {
    switch (key) {
        case CONF_font:
            return &conf->fontspec;
    }
    assert(false);
}

const char *conf_get_str(Conf *conf, int key) {
    switch (key) {
        case CONF_host:
            return conf->host;
        case CONF_line_codepage:
            return "ascii";
    }
    assert(false);
}

void conf_set_str(Conf *conf, int key, const char *value) {
    switch (key) {
        case CONF_host:
            strncpy(conf->host, value, sizeof(conf->host)-1);
            conf->host[sizeof(conf->host)-1] = '\0';
            break;
        default:
            assert(false);
    }
}

Conf *conf_new() {
    return (Conf *)malloc(sizeof(Conf));
}

Conf *conf_copy(Conf *conf) {
    Conf *new_conf = conf_new();
    *new_conf = *conf;
    return new_conf;
}

void conf_free(Conf *conf) {
    free(conf);
}

#define ATTR_ERASE 0x80000000

typedef struct {
    int y, x;
} pos;

typedef struct termchar {
    unsigned long attr;
} termchar;

typedef struct rgb {
    unsigned char r, g, b;
} rgb;

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
    TermWin *win;
    Ldisc *ldisc;
    rgb palette[1];
    bool win_palette_pending;
    termchar basic_erase_char;
    termchar erase_char;
} Terminal;

static void term_size(Terminal *term, int rows, int cols, int) {
    term->cols = cols;
    term->rows = rows;
}

static void term_set_focus(Terminal *term, bool has_focus) {
    term->has_focus = has_focus;
}

static Terminal *term_init(Conf *, struct unicode_data *, TermWin *) {
    return calloc(1, sizeof(Terminal));
}

static void term_free(Terminal *term) {
    free(term);
}

static void term_provide_logctx(Terminal *, LogContext *) {
}

static void term_update_sbar(Terminal *) {
}

static void term_invalidate(Terminal *) {
}

static void term_scroll(Terminal *, int, int) {
}

static void term_update(Terminal *term) {
}

static void term_notify_minimised(Terminal *, bool) {
}

#define snewn(n, type) safemalloc(n, sizeof(type))
#define smalloc malloc
#define sfree free

static void *safemalloc(size_t n, size_t size) {
    return malloc(n * size);
}

static char *dupstr(const char *str) {
    return strdup(str);
}

static wchar_t *dup_mb_to_wc(int, const char *str) {
    size_t len = strlen(str)+1;
    wchar_t *wc = (wchar_t *)malloc(len * sizeof(wchar_t));
    for (size_t i = 0; i < len; i++) {
        wc[i] = str[i];
    }
    return wc;
}

static char *dupprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *str = (char *)malloc(1024);
    vsprintf(str, fmt, args);
    va_end(args);
    return str;
}

static bool conf_launchable(Conf *conf) {
    return conf_get_str(conf, CONF_host)[0] != 0;
}
