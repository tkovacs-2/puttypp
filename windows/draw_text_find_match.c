static bool match_mask_check(const FindMatchMask *mask, int row, int col)
{
    assert(mask->cells);
    assert(row >= 0 && row < mask->rows);
    assert(col >= 0 && col < mask->cols);
    return mask->cells[(size_t)row * (size_t)mask->cols + (size_t)col];
}

static int get_utf16_text_step(const wchar_t *text, int len, int i)
{
    if (i + 1 < len && IS_SURROGATE_PAIR(text[i], text[i + 1])) {
        return 2;
    }
    return 1;
}

static unsigned long find_highlight_attr(unsigned long attr)
{
    return (attr & ~(ATTR_REVERSE | ATTR_BLINK | ATTR_DIM | ATTR_COLOURS)) |
           (OSC4_COLOUR_black << ATTR_FGSHIFT) | (OSC4_COLOUR_yellow << ATTR_BGSHIFT);
}

static void wintw_draw_text_find_match(
    TermWin *tw, int x, int y, wchar_t *text, int len,
    unsigned long attr, int lattr, truecolour tc)
{
    static truecolour match_tc = {{true, 0, 0, 0}, {true, 255, 241, 0}};

#if 0
    if (attr & ATTR_ERASE) {
        attr = (attr & ~ATTR_BGMASK) | (OSC4_COLOUR_red << ATTR_BGSHIFT);
    }
#endif

    if (!find_match_mask.dirty || (attr & TDATTR_MASK & (~TATTR_COMBINING))) {
        wintw_draw_text(tw, x, y, text, len, attr, lattr, tc);
        return;
    }
    if (attr & TATTR_COMBINING) {
        if (match_mask_check(&find_match_mask, y, x)) {
            wintw_draw_text(tw, x, y, text, len, find_highlight_attr(attr), lattr, match_tc);
        } else {
            wintw_draw_text(tw, x, y, text, len, attr, lattr, tc);
        }
        return;
    }

    int col_step = (attr & ATTR_WIDE) ? 2 : 1;
    int i = 0;
    int col = x;
    int run_start = 0;
    int run_col = x;
    bool run_match = false;

    while (i < len) {
        bool match = match_mask_check(&find_match_mask, y, col);
        int text_step = get_utf16_text_step(text, len, i);
        assert(!match || text_step == 1 || match_mask_check(&find_match_mask, y, col+1));
        if (match != run_match) {
            if (i > 0) {
                wintw_draw_text(tw, run_col, y, text + run_start, i - run_start,
                                run_match ? find_highlight_attr(attr) : attr, lattr,
                                run_match ? match_tc : tc);
                run_start = i;
                run_col = col;
            }
            run_match = match;
        }
        col += col_step;
        i += text_step;
    }
    wintw_draw_text(tw, run_col, y, text + run_start, len - run_start,
                    run_match ? find_highlight_attr(attr) : attr, lattr,
                    run_match ? match_tc : tc);
}
