#include "stubterminal.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

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
      case CONF_try_palette:
        return conf->try_palette;
      case CONF_hide_mouseptr:
        return conf->hide_mouseptr;
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
      case CONF_try_palette:
        conf->try_palette = value;
        break;
      case CONF_hide_mouseptr:
        conf->hide_mouseptr = value;
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

int conf_get_int_int(Conf *conf, int key, int subkey) {
    switch (key) {
      case CONF_colours:
        return ((unsigned char *)conf->colors)[subkey];
    }
    assert(false);
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

void term_size(Terminal *term, int rows, int cols, int) {
    term->cols = cols;
    term->rows = rows;
    term->curs.x = 1;
}

void term_set_focus(Terminal *term, bool has_focus) {
    term->has_focus = has_focus;
}

Terminal *term_init(Conf *, struct unicode_data *, TermWin *) {
    return calloc(1, sizeof(Terminal));
}

void term_free(Terminal *term) {
    free(term);
}

void term_provide_logctx(Terminal *, LogContext *) {
}

void term_update_sbar(Terminal *) {
}

void term_invalidate(Terminal *) {
}

void term_scroll(Terminal *, int, int) {
}

void term_update(Terminal *term) {
}

void term_notify_minimised(Terminal *, bool) {
}

void term_notify_window_size_pixels(Terminal *term, int, int) {
}

#define snewn(n, type) safemalloc(n, sizeof(type))
#define smalloc malloc
#define sfree free

void *safemalloc(size_t n, size_t size) {
    return malloc(n * size);
}

char *dupstr(const char *str) {
    return strdup(str);
}

wchar_t *dup_mb_to_wc(int, const char *str) {
    size_t len = strlen(str)+1;
    wchar_t *wc = (wchar_t *)malloc(len * sizeof(wchar_t));
    for (size_t i = 0; i < len; i++) {
        wc[i] = str[i];
    }
    return wc;
}

char *dupprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char *str = (char *)malloc(1024);
    vsprintf(str, fmt, args);
    va_end(args);
    return str;
}

bool conf_launchable(Conf *conf) {
    return conf_get_str(conf, CONF_host)[0] != 0;
}
