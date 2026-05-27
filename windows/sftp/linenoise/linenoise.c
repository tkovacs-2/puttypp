/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "linenoise.h"

static inline int utf8_is_continuation(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

static size_t utf8_seqlen(unsigned char c) {
    if (c < 0x80u)
        return 1;
    if ((c & 0xE0u) == 0xC0u)
        return 2;
    if ((c & 0xF0u) == 0xE0u)
        return 3;
    if ((c & 0xF8u) == 0xF0u)
        return 4;
    return 0;
}

static size_t utf8_prev_start(const unsigned char *s, size_t pos) {
    assert(pos > 0);
    size_t p = pos - 1;
    while (p > 0 && utf8_is_continuation(s[p])) {
        p--;
    }
    return p;
}

static size_t utf8_seqlen_at(const unsigned char *s, size_t pos, size_t len) {
    assert(pos < len);
    size_t n = utf8_seqlen(s[pos]);
    assert(n > 0 && pos + n <= len);
    return n;
}

static int utf8_test_char(const unsigned char *s) {
    unsigned int uc;
    unsigned char c0, c1, c2, c3;
    size_t n;

    c0 = s[0];
    n = utf8_seqlen(c0);
    if (n == 0) {return 0;}
    if (n == 1) {
        uc = c0;
    } else {
        c1 = s[1];
        if (!utf8_is_continuation(c1)) {return 0;}
        if (n == 2) {
            uc = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
            if (uc < 0x80u) {return 0;}
        } else {
            c2 = s[2];
            if (!utf8_is_continuation(c2)) {return 0;}
            if (n == 3) {
                uc = ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
                if (uc < 0x800u || (uc >= 0xD800u && uc <= 0xDFFFu)) {return 0;}
            } else {
                c3 = s[3];
                if (!utf8_is_continuation(c3)) {return 0;}
                uc = ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
                if (uc < 0x10000u || uc > 0x10FFFFu) {return 0;}
            }
        }
    }
    return 1;
}

static size_t utf8_decode_char(const unsigned char *s, unsigned int *uc_out) {
    unsigned int uc;
    unsigned char c0, c1, c2, c3;
    size_t n;

    c0 = s[0];
    n = utf8_seqlen(c0);
    if (n == 1) {
        uc = c0;
    } else {
        c1 = s[1];
        if (n == 2) {
            uc = ((c0 & 0x1Fu) << 6) | (c1 & 0x3Fu);
        } else {
            c2 = s[2];
            if (n == 3) {
                uc = ((c0 & 0x0Fu) << 12) | ((c1 & 0x3Fu) << 6) | (c2 & 0x3Fu);
            } else {
                c3 = s[3];
                uc = ((c0 & 0x07u) << 18) | ((c1 & 0x3Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu);
            }
        }
    }
    *uc_out = uc;
    return n;
}

static int utf8_colspan(const unsigned char *s, size_t len, linenoiseWcWidth wcwidth)
{
    int cols = 0;
    size_t i = 0;
    while (i < len) {
        unsigned int uc;
        size_t step = utf8_decode_char(s+i, &uc);
        if (step == 0)
            break;
        int w = wcwidth(uc);
        if (w < 0) {
            w = 0;
        }
        cols += w;
        i += step;
    }
    return cols;
}

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

#define REFRESH_CLEAN (1<<0)    // Clean the old prompt from the screen
#define REFRESH_WRITE (1<<1)    // Rewrite the prompt on the screen.
#define REFRESH_ALL (REFRESH_CLEAN|REFRESH_WRITE) // Do both.
static void refreshLine(linenoiseState *l);
static void addToHistory(linenoiseState *l);
static void popFromHistory(linenoiseHistory *h);

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("/tmp/lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->oldrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal.
 *
 * Flags is REFRESH_* macros. The function can just remove the old
 * prompt, just write it, or both. */
static void refreshMultiLine(linenoiseState *l, int flags) {
    char seq[64];
    int plen = l->plen;
    int wpos = utf8_colspan((const unsigned char *)l->buf, l->pos, l->wcwidth);
    int wlen = wpos + utf8_colspan((const unsigned char *)l->buf + l->pos, l->len - l->pos, l->wcwidth);
    int rows = (plen+wlen+l->cols-1)/l->cols; /* rows used by current buf. */
    int rpos = (plen+l->oldpos+l->cols)/l->cols; /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = l->oldrows;
    int j;
    struct abuf ab;

    l->oldrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);

    if (flags & REFRESH_CLEAN) {
        if (old_rows-rpos > 0) {
            lndebug("go down %d", old_rows-rpos);
            snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Now for every row clear it, go up. */
        for (j = 0; j < old_rows-1; j++) {
            lndebug("clear+up");
            snprintf(seq,64,"\r\x1b[0K\x1b[1A");
            abAppend(&ab,seq,strlen(seq));
        }
    }

    if (flags & REFRESH_ALL) {
        /* Clean the top line. */
        lndebug("clear");
        snprintf(seq,64,"\r\x1b[0K");
        abAppend(&ab,seq,strlen(seq));
    }

    if (flags & REFRESH_WRITE) {
        /* Write the prompt and the current buffer content */
        abAppend(&ab,l->prompt,strlen(l->prompt));
        abAppend(&ab,l->buf,l->len);

        /* If we are at the very end of the screen with our prompt, we need to
         * emit a newline and move the prompt to the first column. */
        if (l->pos &&
            l->pos == l->len &&
            (wpos+plen) % l->cols == 0)
        {
            lndebug("<newline>");
            abAppend(&ab,"\n",1);
            snprintf(seq,64,"\r");
            abAppend(&ab,seq,strlen(seq));
            rows++;
            if (rows > (int)l->oldrows) l->oldrows = rows;
        }

        /* Move cursor to right position. */
        rpos2 = (plen+wpos+l->cols)/l->cols; /* Current cursor relative row */
        lndebug("rpos2 %d", rpos2);

        /* Go up till we reach the expected positon. */
        if (rows-rpos2 > 0) {
            lndebug("go-up %d", rows-rpos2);
            snprintf(seq,64,"\x1b[%dA", rows-rpos2);
            abAppend(&ab,seq,strlen(seq));
        }

        /* Set column. */
        col = (plen+wpos) % (int)l->cols;
        lndebug("set col %d", 1+col);
        if (col)
            snprintf(seq,64,"\r\x1b[%dC", col);
        else
            snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
    }

    lndebug("\n");
    l->oldpos = wpos;

    if (l->ofd(ab.b,ab.len, l->cb_ctx) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Utility function to avoid specifying REFRESH_ALL all the times. */
static void refreshLine(linenoiseState *l) {
    refreshMultiLine(l,REFRESH_ALL);
}

/* Hide the current line, when using the multiplexing API. */
void linenoiseHide(linenoiseState *l) {
    refreshMultiLine(l,REFRESH_CLEAN);
}

/* Show the current line, when using the multiplexing API. */
void linenoiseShow(linenoiseState *l) {
    refreshMultiLine(l,REFRESH_WRITE);
}

static void editInsert(linenoiseState *l, unsigned char c) {
    unsigned char tmp[4];
    size_t n = utf8_seqlen(c);
    assert(n > 0);
    tmp[0] = c;
    if (n > 1) {
        size_t nread = l->ifd(tmp+1, n-1, l->cb_ctx);
        assert(nread == n-1);
    }
    assert(utf8_test_char(tmp));
    if (l->len + n > l->buflen) {
        return;
    }
    if (l->len != l->pos) {
        memmove(l->buf + l->pos + n, l->buf + l->pos, l->len - l->pos);
    }
    memcpy(l->buf + l->pos, tmp, n);
    l->pos += n;
    l->len += n;
    l->buf[l->len] = '\0';
    refreshLine(l);
    return;
}

/* Move cursor on the left. */
static void editMoveLeft(linenoiseState *l) {
    if (l->pos > 0) {
        l->pos = utf8_prev_start((const unsigned char *)l->buf, l->pos);
        refreshLine(l);
    }
}

/* Move cursor on the right. */
static void editMoveRight(linenoiseState *l) {
    if (l->pos < l->len) {
        l->pos += utf8_seqlen_at((const unsigned char *)l->buf, l->pos, l->len);
        refreshLine(l);
    }
}

/* Move cursor to the start of the line. */
static void editMoveHome(linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
static void editMoveEnd(linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
static void editHistoryNext(linenoiseState *l, int dir) {
    linenoiseHistory *h = l->history;
    if (h->history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(h->history[h->history_len - 1 - l->history_index]);
        h->history[h->history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entrys */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= h->history_len) {
            l->history_index = h->history_len-1;
            return;
        }
        strncpy(l->buf,h->history[h->history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
static void editDelete(linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        size_t n = utf8_seqlen_at((const unsigned char *)l->buf, l->pos, l->len);
        memmove(l->buf + l->pos, l->buf + l->pos + n, l->len - l->pos - n + 1);
        l->len -= n;
        refreshLine(l);
    }
}

/* Backspace implementation. */
static void editBackspace(linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        size_t st = utf8_prev_start((const unsigned char *)l->buf, l->pos);
        size_t n = l->pos - st;
        memmove(l->buf + st, l->buf + l->pos, l->len - l->pos + 1);
        l->pos = st;
        l->len -= n;
        refreshLine(l);
    }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
static void editDeletePrevWord(linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;

    while (l->pos > 0) {
        size_t st = utf8_prev_start((const unsigned char *)l->buf, l->pos);
        if (l->pos - st == 1 && l->buf[st] == ' ')
            l->pos = st;
        else
            break;
    }
    while (l->pos > 0) {
        size_t st = utf8_prev_start((const unsigned char *)l->buf, l->pos);
        if (l->pos - st == 1 && l->buf[st] == ' ')
            break;
        l->pos = st;
    }
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}

/* This function is part of the multiplexed API of Linenoise, that is used
 * in order to implement the blocking variant of the API but can also be
 * called by the user directly in an event driven program. It will:
 *
 * 1. Initialize the linenoise state passed by the user.
 * 2. Put the terminal in RAW mode.
 * 3. Show the prompt.
 * 4. Return control to the user, that will have to call linenoiseEditFeed()
 *    each time there is some data arriving in the standard input.
 *
 * The user can also call linenoiseEditHide() and linenoiseEditShow() if it
 * is required to show some input arriving asyncronously, without mixing
 * it with the currently edited line.
 *
 * When linenoiseEditFeed() returns non-NULL, the user finished with the
 * line editing session (pressed enter CTRL-D/C): in this case the caller
 * needs to call linenoiseEditStop() to put back the terminal in normal
 * mode. This will not destroy the buffer, as long as the linenoiseState
 * is still valid in the context of the caller.
 *
 * The function returns 0 on success, or -1 if writing to standard output
 * fails. If stdin_fd or stdout_fd are set to -1, the default is to use
 * STDIN_FILENO and STDOUT_FILENO.
 *
 * wcwidth is called for each decoded Unicode scalar when computing line wrap
 * and cursor column (see linenoiseWcWidth). It may be NULL (width 1 per scalar).
 */
int linenoiseEditStart(linenoiseState *l, linenoiseRead read_cb, linenouseWrite write_cb, void *cb_ctx, const char *prompt, int cols, linenoiseHistory *history, linenoiseWcWidth wcwidth) {
    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l->in_completion = 0;
    l->ifd = read_cb;
    l->ofd = write_cb;
    l->cb_ctx = cb_ctx;
    l->wcwidth = wcwidth;
    l->buf = l->t;
    l->buflen = sizeof(l->t);
    l->prompt = prompt;
    l->plen = strlen(prompt);
    l->oldpos = l->pos = 0;
    l->len = 0;
    l->cols = cols;
    l->oldrows = 0;
    l->history_index = 0;
    /* Buffer starts empty. */
    l->buf[0] = '\0';
    l->buflen--; /* Make sure there is always space for the nulterm */

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    l->history = history;
    addToHistory(l);

    if (l->ofd(prompt,l->plen, l->cb_ctx) == -1) return -1;
    return 0;
}

/* This function is part of the multiplexed API of linenoise, see the top
 * comment on linenoiseEditStart() for more information. Call this function
 * each time there is some data to read from the standard input file
 * descriptor. In the case of blocking operations, this function can just be
 * called in a loop, and block.
 *
 * The function returns linenoiseEditMore to signal that line editing is still
 * in progress, that is, the user didn't yet pressed enter / CTRL-D. Otherwise
 * the function returns the pointer to the heap-allocated buffer with the
 * edited line, that the user should free with linenoiseFree().
 *
 * On special conditions, NULL is returned and errno is populated:
 *
 * EAGAIN if the user pressed Ctrl-C
 * ENOENT if the user pressed Ctrl-D
 *
 * Some other errno: I/O error.
 */
 linenoiseFeedResult linenoiseEditFeed(linenoiseState *l) {
    char c;
    int nread;
    char seq[3];

    nread = l->ifd(&c,1, l->cb_ctx);
    if (nread <= 0) return LINENOISEFEED_MORE;

    if (c == TAB) {
        if (l->in_completion) {return LINENOISEFEED_COMPLETION_AGAIN;}
        l->in_completion = 1;
        return LINENOISEFEED_COMPLETION;
    }
    l->in_completion = 0;

    switch(c) {
    case ENTER:    /* enter */
        popFromHistory(l->history);
        addToHistory(l);
        editMoveEnd(l);
        return LINENOISEFEED_FINISH;
    case CTRL_C:     /* ctrl-c */
        popFromHistory(l->history);
        return LINENOISEFEED_CANCEL;
    case BACKSPACE:   /* backspace */
    case CTRL_H:     /* ctrl-h */
        editBackspace(l);
        break;
    case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                        line is empty, act as end-of-file. */
        if (l->len > 0) {
            editDelete(l);
        } else {
            popFromHistory(l->history);
            return LINENOISEFEED_EXIT;
        }
        break;
    case CTRL_T:    /* ctrl-t, swap previous codepoint with current. */
        if (l->pos > 0 && l->pos < l->len) {
            size_t p1 = l->pos;
            size_t p0 = utf8_prev_start((const unsigned char *)l->buf, p1);
            size_t n0 = p1 - p0;
            size_t n1 = utf8_seqlen_at((const unsigned char *)l->buf, p1, l->len);
            char t[8];
            if (n0 > 0 && n1 > 0 && n0 + n1 <= sizeof(t)) {
                memcpy(t, l->buf + p1, n1);
                memcpy(t + n1, l->buf + p0, n0);
                memcpy(l->buf + p0, t, n0 + n1);
                l->pos = p0 + n0 + n1;
                refreshLine(l);
            }
        }
        break;
    case CTRL_B:     /* ctrl-b */
        editMoveLeft(l);
        break;
    case CTRL_F:     /* ctrl-f */
        editMoveRight(l);
        break;
    case CTRL_P:    /* ctrl-p */
        editHistoryNext(l, LINENOISE_HISTORY_PREV);
        break;
    case CTRL_N:    /* ctrl-n */
        editHistoryNext(l, LINENOISE_HISTORY_NEXT);
        break;
    case ESC:    /* escape sequence */
        /* Read the next two bytes representing the escape sequence.
         * Use two calls to handle slow terminals returning the two
         * chars at different times. */
        if (l->ifd(seq,1, l->cb_ctx) == -1) break;
        if (l->ifd(seq+1,1, l->cb_ctx) == -1) break;

        /* ESC [ sequences. */
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                /* Extended escape, read additional byte. */
                if (l->ifd(seq+2,1, l->cb_ctx) == -1) break;
                if (seq[2] == '~') {
                    switch(seq[1]) {
                    case '1':
                        editMoveHome(l);
                        break;
                    case '3': /* Delete key. */
                        editDelete(l);
                        break;
                    case '4':
                        editMoveEnd(l);
                        break;
                    }
                }
            } else {
                switch(seq[1]) {
                case 'A': /* Up */
                    editHistoryNext(l, LINENOISE_HISTORY_PREV);
                    break;
                case 'B': /* Down */
                    editHistoryNext(l, LINENOISE_HISTORY_NEXT);
                    break;
                case 'C': /* Right */
                    editMoveRight(l);
                    break;
                case 'D': /* Left */
                    editMoveLeft(l);
                    break;
                case 'H': /* Home */
                    editMoveHome(l);
                    break;
                case 'F': /* End*/
                    editMoveEnd(l);
                    break;
                }
            }
        }

        /* ESC O sequences. */
        else if (seq[0] == 'O') {
            switch(seq[1]) {
            case 'H': /* Home */
                editMoveHome(l);
                break;
            case 'F': /* End*/
                editMoveEnd(l);
                break;
            }
        }
        break;
    default:
        editInsert(l, (unsigned char)c);
        break;
    case CTRL_U: /* Ctrl+u, delete the whole line. */
        l->buf[0] = '\0';
        l->pos = l->len = 0;
        refreshLine(l);
        break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
        l->buf[l->pos] = '\0';
        l->len = l->pos;
        refreshLine(l);
        break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
        editMoveHome(l);
        break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
        editMoveEnd(l);
        break;
    case CTRL_W: /* ctrl+w, delete previous word */
        editDeletePrevWord(l);
        break;
    }
    return LINENOISEFEED_MORE;
}

/* ================================ History ================================= */

void linenoiseInitHistory(linenoiseHistory *h) {
    h->history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
    h->history_len = 0;
    h->history = NULL;
}

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
void linenoiseFreeHistory(linenoiseHistory *h) {
    if (h->history) {
        int j;

        for (j = 0; j < h->history_len; j++)
            free(h->history[j]);
        free(h->history);
    }
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
static void addToHistory(linenoiseState *l) {
    linenoiseHistory *h = l->history;
    const char *line = l->buf;
    char *linecopy;

    if (h->history_max_len == 0) return;

    /* Initialization on first call. */
    if (h->history == NULL) {
        h->history = malloc(sizeof(char*)*h->history_max_len);
        if (h->history == NULL) return;
        memset(h->history,0,(sizeof(char*)*h->history_max_len));
    }

    /* Don't add duplicated lines. */
    if (h->history_len && !strcmp(h->history[h->history_len-1], line)) return;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if (!linecopy) return;
    if (h->history_len == h->history_max_len) {
        free(h->history[0]);
        memmove(h->history,h->history+1,sizeof(char*)*(h->history_max_len-1));
        h->history_len--;
    }
    h->history[h->history_len] = linecopy;
    h->history_len++;
}

static void popFromHistory(linenoiseHistory *h) {
    if (h->history_len > 0) {
        h->history_len--;
        free(h->history[h->history_len]);
    }
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(linenoiseHistory *h, int len) {
    char **new;

    if (len < 1) return 0;
    if (h->history) {
        int tocopy = h->history_len;

        new = malloc(sizeof(char*)*len);
        if (new == NULL) return 0;

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(h->history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memcpy(new,h->history+(h->history_len-tocopy), sizeof(char*)*tocopy);
        free(h->history);
        h->history = new;
    }
    h->history_max_len = len;
    if (h->history_len > h->history_max_len)
        h->history_len = h->history_max_len;
    return 1;
}

void linenoiseChangeColumns(linenoiseState *l, int cols) {
    l->cols = cols;
    refreshLine(l);
}

char *linenoiseCopyLine(linenoiseState *l, int until_cursor) {
    size_t len = until_cursor ? l->pos : l->len;
    char *line = malloc(len + 1);
    memcpy(line, l->buf, len);
    line[len] = 0;
    return line;
}

int linenoiseUtf8Colspan(const char *s, size_t len, linenoiseWcWidth wcwidth) {
    return utf8_colspan((const unsigned char *)s, len, wcwidth);
}
