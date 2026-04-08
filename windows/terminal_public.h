#ifndef TERMINAL_PUBLIC_H
#define TERMINAL_PUBLIC_H
#include "terminal.h"

termline *term_lineptr(Terminal *term, int y);
void term_unlineptr(termline *line);
int term_sblines(Terminal *term);

#endif
