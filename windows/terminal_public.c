#include "terminal.c"

termline *term_lineptr(Terminal *term, int y) {
    return lineptr(y);
}

void term_unlineptr(termline *line) {
    unlineptr(line);
}

int term_sblines(Terminal *term) {
    return sblines(term);
}
