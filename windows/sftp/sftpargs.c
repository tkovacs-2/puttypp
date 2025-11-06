#include "sftpargs.h"
#include "puttymem.h"

void sftpargs_parse(char *cmdline, SftpArgs *args, bool completion)
{
    char *p = cmdline;
    char **words = NULL;
    int nwords = 0;
    size_t wordssize = 0;

    while (*p && (*p == ' '))
        p++;

    if (*p == '#') {
        /*
        * Special case: comment. Entire line is ignored.
        */
    } else {
        /* Parse the command line into words. The syntax is:
        *  - double quotes are removed, but cause spaces within to be
        *    treated as non-separating.
        *  - a double-doublequote pair is a literal double quote, inside
        *    _or_ outside quotes. Like this:
        *
        *      firstword "second word" "this has ""quotes"" in" and""this""
        *
        * becomes
        *
        *      >firstword<
        *      >second word<
        *      >this has "quotes" in<
        *      >and"this"<
        */
        char *p0 = p;
        char *q, *r;
        bool quoting;
        while (1) {
            /* skip whitespace */
            while (*p && (*p == ' '))
                p++;
            /* terminate loop */
            if (!*p)
                break;
            /* mark start of word */
            q = r = p;                 /* q sits at start, r writes word */
            quoting = false;
            while (*p) {
                if (!quoting && (*p == ' '))
                    break;                     /* reached end of word */
                else if (*p == '"' && p[1] == '"')
                    p += 2, *r++ = '"';    /* a literal quote */
                else if (*p == '"')
                    p++, quoting = !quoting;
                else
                    *r++ = *p++;
            }
            if (*p)
                p++;                   /* skip over the whitespace */
            *r = '\0';
            sgrowarray(words, wordssize, nwords);
            words[nwords++] = q;
        }
        if (completion && (p == p0 || p[-1] == ' ' || p[-1] == 0)) {
            sgrowarray(words, wordssize, nwords);
            words[nwords++] = p;
        }
    }
    args->argv = (const char * const *)words;
    args->argc = nwords;
    args->cmdline = cmdline;
}

void sftpargs_free(SftpArgs *args)
{
    sfree(args->cmdline);
    sfree((void *)args->argv);
    args->argv = NULL;
    args->argc = 0;
    args->cmdline = NULL;
}
