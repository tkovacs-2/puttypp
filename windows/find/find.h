#ifndef FIND_H
#define FIND_H

#include <stdbool.h>
#include <wchar.h>

typedef struct terminal_tag Terminal;
typedef struct FindIterator FindIterator;

typedef struct FindMatchMask {
    int rows;
    int cols;
    bool dirty;
    unsigned char *cells; /* row-major: index row * cols + col */
} FindMatchMask;

void find_match_mask_init(FindMatchMask *mask);
void find_match_mask_alloc(FindMatchMask *mask, int rows, int cols);
void find_match_mask_free(FindMatchMask *mask);

void find_match_mask_clear(FindMatchMask *mask);
void find_match_mask_set_range(FindMatchMask *mask, FindIterator *match_start, FindIterator *match_end);

void find_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, FindMatchMask *mask);

bool find_above_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, int *row);
bool find_below_display(Terminal *term, const wchar_t *pattern, int pattern_len, bool ignore_case, bool whole_word, int *row);
#endif
