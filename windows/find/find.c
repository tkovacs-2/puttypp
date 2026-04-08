#include "putty.h"
#include "terminal_public.h"
#include "finditerator.h"
#include "find.h"
#include "kmp.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool found;
    int row;
} FindAboveDisplayMatch;

typedef struct {
    bool found;
    int row;
    int term_rows;
} FindBelowDisplayMatch;

void find_match_mask_init(FindMatchMask *mask) {
    mask->rows = 0;
    mask->cols = 0;
    mask->cells = NULL;
    mask->dirty = false;
}

void find_match_mask_alloc(FindMatchMask *mask, int rows, int cols) {
    assert(rows >= 1 && cols >= 1);
    if (mask->rows == rows && mask->cols == cols && mask->cells != NULL) {
        find_match_mask_clear(mask);
    } else {
        mask->rows = rows;
        mask->cols = cols;
        mask->dirty = false;
        size_t n = (size_t)rows * (size_t)cols;
        mask->cells = srealloc(mask->cells, n);
        memset(mask->cells, 0, n);
    }
}

void find_match_mask_free(FindMatchMask *mask) {
    sfree(mask->cells);
    mask->cells = NULL;
    mask->dirty = false;
}

void find_match_mask_clear(FindMatchMask *mask) {
    assert(mask && mask->cells != NULL);
    if (mask->dirty) {
        memset(mask->cells, 0, (size_t)mask->rows * (size_t)mask->cols);
        mask->dirty = false;
    }
}

void find_match_mask_set_range(FindMatchMask *mask, FindIterator *match_start, FindIterator *match_end) {
    assert(mask && match_start && match_end);
    assert(mask->rows >= 1 && mask->cols >= 1);
    assert(mask->cells != NULL);

    int mr = mask->rows;
    int mc = mask->cols;
    int sr = match_start->row, sc = match_start->col + match_start->shift;
    int er = match_end->row, ec = match_end->col + match_end->shift;
    if (sc >= mask->cols) {
        sc = mask->cols-1;
    }
    if (ec >= mask->cols) {
        ec = mask->cols-1;
    }
    int Ls = sr * mc + sc;
    int Le = er * mc + ec;
    assert(Le >= Ls);
    int Lmax = mr * mc - 1;
    if (Ls > Lmax || Le < 0) {
        return;
    }
    if (Ls < 0) {
        Ls = 0;
    }
    if (Le > Lmax) {
        Le = Lmax;
    }
    memset(mask->cells+Ls, 1, (size_t)(Le-Ls)+1);
    mask->dirty = true;
}

static bool set_match_mask(FindIterator *match_start, FindIterator *match_end, void *ctx) {
    FindMatchMask *mask = (FindMatchMask *)ctx;
    find_match_mask_set_range(mask, match_start, match_end);
    return true;
}

static bool set_above_display_match(FindIterator *match_start, FindIterator *match_end, void *ctx) {
    FindAboveDisplayMatch *result = (FindAboveDisplayMatch *)ctx;
    if (match_end->row < 0) {
        result->row = match_end->row;
        result->found = true;
    } else if (match_start->row < 0) {
        result->row = match_start->row;
        result->found = true;
        return false;
    } else {
        if (result->found) {
            return false;
        }
    }
    return true;
}

static bool set_below_display_match(FindIterator *match_start, FindIterator *match_end, void *ctx) {
    FindBelowDisplayMatch *result = (FindBelowDisplayMatch *)ctx;
    if (match_start->row >= result->term_rows) {
        result->row = match_start->row;
        result->found = true;
        return false;
    } else if (match_end->row >= result->term_rows) {
        result->row = match_end->row;
        result->found = true;
        return false;
    }
    return true;
}

void find_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, FindMatchMask *mask) {
    KmpContext *ctx = kmp_prepare_context(pattern, pattern_len, ignore_case, whole_word);
    FindIterator iter;
    find_iterator_init(term, &iter, 0);
    find_iterator_wrapup(&iter);
    while (iter.row < term->rows) {
        find_iterator_load(&iter);
        kmp_search(&iter, ctx, set_match_mask, mask);
    }
    kmp_free_context(ctx);
}

bool find_above_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, int *row) {
    KmpContext *ctx = kmp_prepare_context(pattern, pattern_len, ignore_case, whole_word);
    FindAboveDisplayMatch result = {false, 0};
    FindIterator iter;
    find_iterator_init(term, &iter, -1);
    while (true) {
        find_iterator_wrapup(&iter);
        int r = iter.row;
        find_iterator_load(&iter);
        if (find_iterator_get(&iter) == NULL) {break;}
        kmp_search(&iter, ctx, set_above_display_match, &result);
        if (result.found) {
            find_iterator_unload(&iter);
            *row = result.row;
            break;
        }
        iter.row = --r;
    }
    kmp_free_context(ctx);
    return result.found;
}

bool find_below_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, int *row) {
    KmpContext *ctx = kmp_prepare_context(pattern, pattern_len, ignore_case, whole_word);
    FindBelowDisplayMatch result = {false, 0, term->rows};
    FindIterator iter;
    find_iterator_init(term, &iter, term->rows);
    find_iterator_wrapup(&iter);
    while (true) {
        find_iterator_load(&iter);
        if (find_iterator_get(&iter) == NULL) {break;}
        kmp_search(&iter, ctx, set_below_display_match, &result);
        if (result.found) {
            find_iterator_unload(&iter);
            *row = result.row;
            break;
        }
    }
    kmp_free_context(ctx);
    return result.found;
}
