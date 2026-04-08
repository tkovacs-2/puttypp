#include "putty.h"
#include "terminal_public.h"
#include "find.h"
#include "finditerator.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

static void print_termlines_content(Terminal *term) {
    printf("Disptop %d: cursor %d,%d\n", term->disptop, term->curs.y, term->curs.x);
    for (int r = 0; r < term->rows; r++) {
        termline *line = term_lineptr(term, r + term->disptop);
        printf("Row %d: ", r);
        for (int c = 0; c < line->cols; c++) {
            printf("%02x ", (unsigned int)(line->chars[c].chr & 0xFF));
        }
        printf("\n");
        term_unlineptr(line);
    }
    printf("\n");
}

static void print_find_match_mask(const FindMatchMask *mask)
{
    printf("FindMatchMask (%d rows x %d cols, dirty=%s):\n",
           mask->rows, mask->cols, mask->dirty ? "true" : "false");
    for (int r = 0; r < mask->rows; r++) {
        for (int c = 0; c < mask->cols; c++) {
            unsigned char v = mask->cells[r * mask->cols + c];
            printf("%c", v ? '1' : '0');
        }
        printf("\n");
    }
}


static int line_fill_ascii(Terminal *term, int y, const char *s)
{
    assert(*s);
    termline *line = term_lineptr(term, y);
    int col = 0;
    while (*s) {
        if (col == term->cols) {
            line->lattr |= LATTR_WRAPPED;
            term_unlineptr(line);
            y++;
            line = term_lineptr(term, y);
            col = 0;
        }
        termchar *c = &line->chars[col++];
        c->chr = (unsigned char)*s++;
        c->attr &= ~ATTR_ERASE;
    }
    term_unlineptr(line);
    return y+1;
}

static void init_term_lines(Terminal *term, int rows, int cols)
{
    term_size(term, rows, cols, 500);
    term_pwron(term, true);
    term_clrsb(term);
    for (int r = 0; r < term->rows; r++) {
        char buf[2] = { (char)('a' + r), '\0' };
        line_fill_ascii(term, r, buf);
    }
    term->curs.y = term->rows - 1;
    term->curs.x = 0;
}

static void finalize_term_lines(Terminal *term, int rows, int scroll_where)
{
    term_size(term, rows, term->cols, 500);
    term_scroll(term, 0, scroll_where);
}

static int mask_cells_ok(const FindMatchMask *m, const unsigned char *expected,
                         bool expect_dirty)
{
    if (memcmp(m->cells, expected,
               (size_t)m->rows * (size_t)m->cols) != 0 ||
        m->dirty != expect_dirty) {
        print_find_match_mask(m);
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}

static int check_find_above_display(Terminal *term, const wchar_t *pattern, int pattern_len,
                                    bool expect_ok, int expect_row)
{
    int row = -999;
    bool ok = find_above_display(term, pattern, pattern_len, false, false, &row);
    if (ok != expect_ok) {
        printf("FAIL: ok=%d expect_ok=%d\n", (int)ok, (int)expect_ok);
        return 1;
    }
    if (expect_ok && row != expect_row) {
        printf("FAIL: row=%d expect_row=%d\n", row, expect_row);
        return 1;
    }
    printf("PASS\n");
    return 0;
}

static int check_find_below_display(Terminal *term, const wchar_t *pattern, int pattern_len,
                                    bool expect_ok, int expect_row)
{
    int row = -999;
    bool ok = find_below_display(term, pattern, pattern_len, false, false, &row);
    if (ok != expect_ok) {
        printf("FAIL: ok=%d expect_ok=%d\n", (int)ok, (int)expect_ok);
        return 1;
    }
    if (expect_ok && row != expect_row) {
        printf("FAIL: row=%d expect_row=%d\n", row, expect_row);
        return 1;
    }
    printf("PASS\n");
    return 0;
}


int test_find(Terminal *term)
{
    int failures = 0;

    {
        printf("\n--- find_match_mask_init: dimensions, dirty=false, all zero ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 2, 3);
        if (mask.rows != 2 || mask.cols != 3) {
            printf("FAIL (struct fields)\n");
            failures++;
        } else {
            unsigned char z[6] = {0};
            failures += mask_cells_ok(&mask, z, false);
        }
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: partial first row ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 2, 4);
        FindIterator a = { 0, 1 };
        FindIterator b = { 0, 4 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[8] = {0, 1, 1, 1, 0, 0, 0, 0};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: span two rows ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 2, 4);
        FindIterator a = { 0, 2 };
        FindIterator b = { 1, 1 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[8] = {0, 0, 1, 1, 1, 1, 0, 0};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: empty range leaves dirty=false ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 4);
        FindIterator a = { 0, 2 };
        FindIterator b = { 0, 2 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char z[4] = {0, 0, 1, 0};
        failures += mask_cells_ok(&mask, z, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_clear: zeros cells and dirty ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 3);
        FindIterator a = { 0, 0 };
        FindIterator b = { 0, 2 };
        find_match_mask_set_range(&mask, &a, &b);
        find_match_mask_clear(&mask);
        unsigned char z[3] = {0};
        failures += mask_cells_ok(&mask, z, false);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: clamp Le to grid ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 3);
        FindIterator a = { -3, 5 };
        FindIterator b = { 9, 9 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[3] = {1, 1, 1};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: clamp negative Ls to 0 ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 2, 4);
        FindIterator a = { -1, 0 };
        FindIterator b = { 0, 1 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[8] = {1, 1, 0, 0, 0, 0, 0, 0};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: start col is over columns and end is over last row ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 4);
        FindIterator a = { 0, 5 };
        FindIterator b = { 1, 2 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[4] = {0, 0, 0, 1};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: start col past line snaps to last col ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 4);
        FindIterator a = { 0, 5 };
        FindIterator b = { 0, 6 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[4] = {0, 0, 0, 1};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_match_mask_set_range: start and end is over last row ---\n");
        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, 1, 4);
        FindIterator a = { 2, 0 };
        FindIterator b = { 2, 3 };
        find_match_mask_set_range(&mask, &a, &b);
        unsigned char exp[4] = {0};
        failures += mask_cells_ok(&mask, exp, false);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_display: 6x5 -> 4x5 scrollback, match on new row 0 ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 0, "abcXYZdXYZ");
        line_fill_ascii(term, 6, "XYZaXYZe");
        finalize_term_lines(term, 4, -2);

        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp[20] = {0};
        failures += mask_cells_ok(&mask, exp, false);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_display: horizontally resized terminal ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 1, "b  XYZ");
        line_fill_ascii(term, 3, "dXYZXYZ");
        line_fill_ascii(term, 5, "f  XYZ");
        finalize_term_lines(term, 4, -2);

        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp[] = {1, 0, 0, 0, 0,
                               0, 1, 1, 1, 1,
                               1, 1, 0, 0, 0,
                               0, 0, 0, 1, 1};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);

        term_size(term, term->rows, term->cols+1, 500);
        term_scroll(term, 0, -2);
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp_grow[] = {1, 0, 0, 0, 0, 0,
                                      0, 1, 1, 1, 1, 1,
                                      1, 1, 0, 0, 0, 0,
                                      0, 0, 0, 1, 1, 1};
        failures += mask_cells_ok(&mask, exp_grow, true);
        find_match_mask_free(&mask);

        term_size(term, term->rows, term->cols-3, 500);
        term_scroll(term, 0, -2);
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp_shrink[] = {1, 0, 0,
                                      0, 1, 1,
                                      1, 1, 0,
                                      0, 0, 1};
        failures += mask_cells_ok(&mask, exp_shrink, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_display: empty terminal ---\n");
        term_size(term, 8, 5, 500);
        term_pwron(term, true);
        term_clrsb(term);
        term->curs.y = term->rows - 1;
        term->curs.x = 0;
        finalize_term_lines(term, 4, -2);

        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp[20] = {0};
        failures += mask_cells_ok(&mask, exp, false);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_display: trusted termline shifts highlight by TRUST_SIGIL_WIDTH ---\n");
        init_term_lines(term, 3, 8);
        line_fill_ascii(term, 0, "XYZ");
        term_lineptr(term, 0)->trusted = true;

        FindMatchMask mask;
        find_match_mask_init(&mask);
        find_match_mask_alloc(&mask, term->rows, term->cols);
        find_display(term, L"XYZ", 3, false, false, &mask);
        unsigned char exp[] = {0, 0, 0, 1, 1, 1, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, 0, 0};
        failures += mask_cells_ok(&mask, exp, true);
        find_match_mask_free(&mask);
    }

    {
        printf("\n--- find_above_display: wrapped line fully above, both rows contain XYZ ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 0, "xxXYZyyXYZ");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, true, -4);
    }

    {
        printf("\n--- find_above_display: wrapped line fully above, XYZ on line break ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 0, "abcXYZdefg");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, true, -4);
    }

    {
        printf("\n--- find_above_display: wrapped line partly above display, both rows contain XYZ ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 4, "xxXYZyyXYZ");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, true, -1);
    }

    {
        printf("\n--- find_above_display: wrapped line partly above, XYZ on line break ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 4, "abcXYZdefg");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, true, -1);
    }

    {
        printf("\n--- find_above_display: no match above display area ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 5, "XYZ");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, false, 0);
    }

    {
        printf("\n--- find_above_display: XYZ on two separate lines above display ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 0, "aaXYZ");
        line_fill_ascii(term, 1, "bbXYZ");
        finalize_term_lines(term, 3, 0);
        failures += check_find_above_display(term, L"XYZ", 3, true, -4);
    }

    {
        printf("\n--- find_below_display: wrapped line fully below, both rows contain XYZ ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 3, "xxXYZyyXYZ");
        finalize_term_lines(term, 3, -5);
        failures += check_find_below_display(term, L"XYZ", 3, true, 3);
    }

    {
        printf("\n--- find_below_display: wrapped line fully below, XYZ on line break ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 3, "abcXYZdefg");
        finalize_term_lines(term, 4, -5);
        failures += check_find_below_display(term, L"XYZ", 3, true, 4);
    }

    {
        printf("\n--- find_below_display: wrapped line partly below display, both rows contain XYZ ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 2, "xxXYZyyXYZ");
        finalize_term_lines(term, 3, -5);
        failures += check_find_below_display(term, L"XYZ", 3, true, 3);
    }

    {
        printf("\n--- find_below_display: wrapped line partly below, XYZ on line break ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 2, "abcXYZdefg");
        finalize_term_lines(term, 3, -5);
        failures += check_find_below_display(term, L"XYZ", 3, true, 3);
    }

    {
        printf("\n--- find_below_display: no match below display area ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 0, "XYZ");
        finalize_term_lines(term, 3, -5);
        failures += check_find_below_display(term, L"XYZ", 3, false, 0);
    }

    {
        printf("\n--- find_below_display: XYZ on two separate lines below display ---\n");
        init_term_lines(term, 8, 5);
        line_fill_ascii(term, 3, "aaXYZ");
        line_fill_ascii(term, 4, "bbXYZ");
        finalize_term_lines(term, 3, -5);
        failures += check_find_below_display(term, L"XYZ", 3, true, 3);
    }

    return failures;
}
