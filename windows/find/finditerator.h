#ifndef FINDITERATOR_H
#define FINDITERATOR_H

 /* erase character, if ATTR_* in putty.h is updated this also need to revise */
#define ATTR_ERASE 0x4000000UL

typedef struct termchar termchar;
typedef struct termline termline;
typedef struct terminal_tag Terminal;

typedef struct FindIterator {
    int row;
    int col;
    int shift;
    Terminal *term;
    termline *line;
    termchar *current;
    termchar *next_non_erase;
} FindIterator;

void find_iterator_init(Terminal *term, FindIterator *iter, int row);
void find_iterator_mark(FindIterator *iter, FindIterator *mark);

termchar *find_iterator_get(FindIterator *iter);
unsigned long find_iterator_get_chr(FindIterator *iter);

bool find_iterator_load(FindIterator *iter);
void find_iterator_wrapup(FindIterator *iter);
void find_iterator_unload(FindIterator *iter);

void find_iterator_next(FindIterator *iter);

#endif
