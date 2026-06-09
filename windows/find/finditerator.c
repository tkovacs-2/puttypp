#include "putty.h"
#include "terminal_public.h"
#include "finditerator.h"

static inline bool is_erase_char(termchar *tc) {
    return (tc->attr & ATTR_ERASE) != 0;
}

static bool check_line_end(FindIterator *iter, int col) {
    if (!is_erase_char(iter->line->chars + col)) {
        return false;
    }
    assert(!iter->next_non_erase);
    col++;
    while (col < iter->line->cols) {
        if (is_erase_char(iter->line->chars + col)) {
            col++;
        } else {
            iter->next_non_erase = iter->line->chars + col;
            return false;
        }
    }
    return true;
}

static termline *get_line(Terminal *term, int row) {
    assert(row+term->disptop < term->rows);
    return term_lineptr(term, row+term->disptop);
}

static void unload_line(FindIterator *iter) {
    term_unlineptr(iter->line);
    iter->line = NULL;
    iter->shift = 0;
    iter->current = NULL;
    iter->next_non_erase = NULL;
}

static void set_current_character(FindIterator *iter) {
    iter->current = &iter->line->chars[iter->col];
}

static int advance_column(termline *line, int col) {
    col++;
    if (col < line->cols && line->chars[col].chr == UCSWIDE) {col++;}
    return col;
}

// The assumption is that if the line has LATTR_WRAPPED flag,
// the next one must have some character other then erase.
// If the opposite still happens we will treat the line as a space character in first column.
static bool advance_wrapped_row(Terminal *term, FindIterator *iter) {
    bool wrapped = iter->line->lattr & LATTR_WRAPPED;
    term_unlineptr(iter->line);
    iter->row++;
    iter->col = 0;
    if (wrapped) {
        iter->line = get_line(term, iter->row);
        iter->shift = (iter->line->trusted ? TRUST_SIGIL_WIDTH : 0);
        return true;
    }
    iter->line = NULL;
    iter->shift = 0;
    return false;
}

void find_iterator_init(Terminal *term, FindIterator *iter, int row) {
    iter->term = term;
    iter->row = row;
    iter->shift = 0;
    iter->line = NULL;
    iter->col = 0;
    iter->current = NULL;
    iter->next_non_erase = NULL;
}

void find_iterator_mark(FindIterator *iter, FindIterator *mark) {
    assert(iter->line != NULL);
    mark->term = iter->term;
    mark->row = iter->row;
    mark->shift = iter->shift;
    mark->line = NULL;
    mark->col = iter->col;
    mark->current = NULL;
    mark->next_non_erase = NULL;
}

termchar *find_iterator_get(FindIterator *iter) {
    return iter->current;
}

unsigned long find_iterator_get_chr(FindIterator *iter) {
    assert(iter->current);
    unsigned long uc = iter->current->chr;
    switch (iter->current->chr & CSET_MASK) {
        case CSET_ASCII:
            uc = iter->term->ucsdata->unitab_line[uc & 0xFF];
            break;
        case CSET_LINEDRW:
            uc = iter->term->ucsdata->unitab_xterm[uc & 0xFF];
            break;
        case CSET_SCOACS:
            uc = iter->term->ucsdata->unitab_scoacs[uc & 0xFF];
            break;
    }
    switch (uc & CSET_MASK) {
        case CSET_ACP:
          uc = iter->term->ucsdata->unitab_font[uc & 0xFF];
          break;
        case CSET_OEMCP:
          uc = iter->term->ucsdata->unitab_oemcp[uc & 0xFF];
          break;
    }
    return uc;
}

bool find_iterator_load(FindIterator *iter) {
    assert(iter->line == NULL && iter->current == NULL);
    if (iter->row+iter->term->disptop >= iter->term->rows ||
        iter->row+iter->term->disptop < -term_sblines(iter->term)) {
        return false;
    }
    iter->line = get_line(iter->term, iter->row);
    iter->shift = (iter->line->trusted ? TRUST_SIGIL_WIDTH : 0);
    if (check_line_end(iter, iter->col)) {
        unload_line(iter);
        iter->row++;
        assert(iter->col == 0);
        return true;
    }
    set_current_character(iter);
    return true;
}

void find_iterator_wrapup(FindIterator *iter) {
    assert(iter->line == NULL && iter->current == NULL);
    int r = iter->row;
    int sbtop = -term_sblines(iter->term)-iter->term->disptop;

    while (r > sbtop) {
        termline *prev = get_line(iter->term, r - 1);
        if (!(prev->lattr & LATTR_WRAPPED)) {
            term_unlineptr(prev);
            break;
        }
        term_unlineptr(prev);
        r--;
    }
    iter->row = r;
    iter->col = 0;
}

void find_iterator_unload(FindIterator *iter) {
    if (iter->line) {
        unload_line(iter);
    }
}

void find_iterator_next(FindIterator *iter) {
    termchar *current = iter->current;
    if (current->cc_next) {
        iter->current = current + current->cc_next;
        return;
    }
    if (iter->next_non_erase) {
        iter->col++;
        set_current_character(iter);
        if (iter->current == iter->next_non_erase) {
            iter->next_non_erase = NULL;
        }
        return;
    }

    termline *line = iter->line;
    int col = advance_column(line, iter->col);
    if (col >= line->cols || check_line_end(iter, col)) {
        if (advance_wrapped_row(iter->term, iter)) {
            set_current_character(iter);
        } else {
            iter->current = NULL;
            iter->next_non_erase = NULL;
        }
        return;
    }
    iter->col = col;
    set_current_character(iter);
}
