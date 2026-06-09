
typedef struct terminal_tag Terminal;
static void set_erase_char(Terminal *term);

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

static void set_erase_char(Terminal *term)
{
    set_erase_char_original(term);
    term->erase_char.attr |= term->basic_erase_char.attr & ~ATTR_COLOURS;
}
