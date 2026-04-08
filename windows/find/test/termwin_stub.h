#ifndef TERMWIN_STUB_H
#define TERMWIN_STUB_H

#include "putty.h"

static bool stub_setup_draw_ctx(TermWin *) { return false; }
static void stub_draw_text(TermWin *, int , int , wchar_t *, int , unsigned long , int , truecolour ) {}
static void stub_draw_cursor(TermWin *, int , int , wchar_t *, int , unsigned long , int , truecolour ) {}
static void stub_draw_trust_sigil(TermWin *, int , int ) {}
static int stub_char_width(TermWin *, int ) { return 1; }
static void stub_free_draw_ctx(TermWin *) {}
static void stub_set_cursor_pos(TermWin *, int , int ) {}
static void stub_set_raw_mouse_mode(TermWin *, bool ) {}
static void stub_set_raw_mouse_mode_pointer(TermWin *, bool ) {}
static void stub_set_scrollbar(TermWin *, int , int , int ) {}
static void stub_bell(TermWin *, int ) {}
static void stub_clip_write(TermWin *, int , wchar_t *, int *, truecolour *, int , bool ) {}
static void stub_clip_request_paste(TermWin *, int ) {}
static void stub_refresh(TermWin *) {}
static void stub_request_resize(TermWin *, int , int ) {}
static void stub_set_title(TermWin *, const char *, int ) {}
static void stub_set_icon_title(TermWin *, const char *, int ) {}
static void stub_set_minimised(TermWin *, bool ) {}
static void stub_set_maximised(TermWin *, bool ) {}
static void stub_move(TermWin *, int , int ) {}
static void stub_set_zorder(TermWin *, bool ) {}
static void stub_palette_set(TermWin *, unsigned , unsigned , const rgb *) {}
static void stub_palette_get_overrides(TermWin *, Terminal *) {}
static void stub_unthrottle(TermWin *, size_t ) {}

static const TermWinVtable stub_termwin_vtable = {
    stub_setup_draw_ctx,
    stub_draw_text,
    stub_draw_cursor,
    stub_draw_trust_sigil,
    stub_char_width,
    stub_free_draw_ctx,
    stub_set_cursor_pos,
    stub_set_raw_mouse_mode,
    stub_set_raw_mouse_mode_pointer,
    stub_set_scrollbar,
    stub_bell,
    stub_clip_write,
    stub_clip_request_paste,
    stub_refresh,
    stub_request_resize,
    stub_set_title,
    stub_set_icon_title,
    stub_set_minimised,
    stub_set_maximised,
    stub_move,
    stub_set_zorder,
    stub_palette_set,
    stub_palette_get_overrides,
    stub_unthrottle,
};

static TermWin stub_termwin = { &stub_termwin_vtable };

#endif
