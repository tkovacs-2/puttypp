#include "putty.h"
#include "terminal_public.h"
#include "finditerator.h"
#include "find.h"
#include "kmp.h"
#include "termwin_stub.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int test_kmp(Terminal *term);
int test_find(Terminal *term);

const char * const appname = "";
const char commitid[4] = {0};
const struct BackendVtable *const backends[] = { NULL };

void modalfatalbox(const char *, ...) {
    exit(1);
}

void nonfatal(const char *, ...) {
    exit(1);
}

static Terminal *new_trem_for_test(struct unicode_data *ucsdata)
{
    Conf *conf = conf_new();
    do_defaults("", conf);
    memset(ucsdata, 0, sizeof *ucsdata);
    Terminal *term = term_init(conf, ucsdata, &stub_termwin);
    term->ldisc = NULL;
    term->basic_erase_char.attr |= ATTR_ERASE;
    conf_free(conf);
    return term;
}

int main()
{
    struct unicode_data ucsdata;
    Terminal *term = new_trem_for_test(&ucsdata);

    int failures = 0;
    failures += test_kmp(term);
    failures += test_find(term);

    term_free(term);
    printf("\n=== Summary: %d test(s) failed ===\n", failures);
    return failures ? 1 : 0;
}
