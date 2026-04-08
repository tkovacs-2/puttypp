#include "putty.h"
#include "kmp.h"
#include "terminal_public.h"
#include "finditerator.h"

#include <wchar.h>
#include <stdio.h>
#include <stdbool.h>

#define MATCH_COLLECTOR_CAPACITY 256

typedef struct Match {
    int start_row, start_col;
    int end_row, end_col;
} Match;

typedef struct {
    Match matches[MATCH_COLLECTOR_CAPACITY];
    int count;
} MatchCollector;

static bool print_match(FindIterator *match_start, FindIterator *match_end, void *ctx)
{
    MatchCollector *mc = (MatchCollector *)ctx;
    if (mc->count >= MATCH_COLLECTOR_CAPACITY)
        return false;
    mc->matches[mc->count].start_row = match_start->row;
    mc->matches[mc->count].start_col = match_start->col;
    mc->matches[mc->count].end_row = match_end->row;
    mc->matches[mc->count].end_col = match_end->col;
    mc->count++;
    return true;
}

static int matches_equal(const Match *a, const Match *b)
{
    return a->start_row == b->start_row && a->start_col == b->start_col &&
           a->end_row == b->end_row && a->end_col == b->end_col;
}

static int run_test(const char *name, const unsigned long *haystack, int hlen,
                    const wchar_t *needle, int nlen,
                    const Match *expected, int expected_count,
                    int cols, bool ignore_case, bool whole_word,
                    Terminal *term)
{
    printf("\n--- %s ---\n", name);

    if (cols == 0) {
        term_size(term, 1, hlen, 0);
    } else {
        int rows = (hlen+cols-1)/cols;
        term_size(term, rows, cols, 0);
    }
    term_pwron(term, true);
    term_clrsb(term);

    for (int row = 0; row < term->rows; row++) {
        int start = row*term->cols;
        int len_on_line = hlen-start;
        if (len_on_line > term->cols) {
            len_on_line = term->cols;
        }
        termline *ln = term_lineptr(term, row);
        if (row+1 < term->rows) {
            ln->lattr |= LATTR_WRAPPED;
        }
        for (int i = 0; i < len_on_line; i++) {
            termchar *c = &ln->chars[i];
            c->chr = haystack[start + i];
            c->attr &= ~ATTR_ERASE;
        }
        term_unlineptr(ln);
    }

    KmpContext *ctx = kmp_prepare_context(needle, nlen, ignore_case, whole_word);
    MatchCollector mc = {0};

    FindIterator it;
    find_iterator_init(term, &it, 0);
    find_iterator_load(&it);
    kmp_search(&it, ctx, print_match, &mc);
    find_iterator_unload(&it);

    kmp_free_context(ctx);

    printf("Total matches: %d (expected %d)\n", mc.count, expected_count);
    if (mc.count != expected_count) {
        printf("FAIL (count)\n");
        return 1;
    }
    bool match_failed = false;
    for (int i = 0; i < expected_count; i++) {
        if (!matches_equal(&mc.matches[i], &expected[i])) {
            printf("FAIL at match %d: got [%d:%d,%d:%d] expected [%d:%d,%d:%d]\n",
                    i,
                    mc.matches[i].start_row, mc.matches[i].start_col,
                    mc.matches[i].end_row, mc.matches[i].end_col,
                    expected[i].start_row, expected[i].start_col,
                    expected[i].end_row, expected[i].end_col);
            match_failed = true;
        }
    }
    if (match_failed) {
        return 1;
    }
    printf("PASS\n");
    return 0;
}

int test_kmp(Terminal *term)
{
    int failures = 0;

    {
        unsigned long haystack[] = {1,2,3,1,2,3,1,2,1,2,3};
        wchar_t needle[] = {1,2,3};
        Match expected[] = {
            { 0, 0, 0, 2 },
            { 0, 3, 0, 5 },
            { 0, 8, 0, 10 },
        };
        failures += run_test("original {1,2,3} in longer array", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,1,1};
        wchar_t needle[] = {1,1};
        Match expected[] = {
            { 0, 0, 0, 1 },
            { 0, 1, 0, 2 }
        };
        failures += run_test("overlapping runs {1,1} in {1,1,1}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,2,1,2,1};
        wchar_t needle[] = {1,2,1};
        Match expected[] = {
            { 0, 0, 0, 2 },
            { 0, 2, 0, 4 },
        };
        failures += run_test("overlapping prefix/suffix {1,2,1} in {1,2,1,2,1}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {9,9,9,9,9,9,9};
        wchar_t needle[] = {9,9,9};
        Match expected[] = {
            { 0, 0, 0, 2 },
            { 0, 1, 0, 3 },
            { 0, 2, 0, 4 },
            { 0, 3, 0, 5 },
            { 0, 4, 0, 6 },
        };
        failures += run_test("many overlaps {9,9,9} in seven 9s", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {4,4,4,4,4};
        wchar_t needle[] = {4};
        Match expected[] = {
            { 0, 0, 0, 0 },
            { 0, 1, 0, 1 },
            { 0, 2, 0, 2 },
            { 0, 3, 0, 3 },
            { 0, 4, 0, 4 },
        };
        failures += run_test("singleton needle, all positions", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {3,1,4,1,5};
        wchar_t needle[] = {3,1,4,1,5};
        Match expected[] = {
            { 0, 0, 0, 4 },
        };
        failures += run_test("needle equals haystack", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,2,3,4};
        wchar_t needle[] = {9,9};
        failures += run_test("no match", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,2};
        wchar_t needle[] = {1,2,3,4};
        failures += run_test("needle longer than haystack", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, false, term);
    }

    {
        unsigned long haystack[] = {3,4,1,1,1,2,3,4};
        wchar_t needle[] = {1,1,2};
        Match expected[] = {
            { 0, 3, 0, 5 },
        };
        failures += run_test("{1,1,2} in {3,4,1,1,1,2,3,4}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,1,2,1,1,2,1,1};
        wchar_t needle[] = {1,1,2,1};
        Match expected[] = {
            { 0, 0, 0, 3 },
            { 0, 3, 0, 6 },
        };
        failures += run_test("{1,1,2,1} in {1,1,2,1,1,2,1,1}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = {3,4,1,1,1,2,1,1,2};
        wchar_t needle[] = {1,1,2,1};
        Match expected[] = {
            { 0, 3, 0, 6 },
        };
        failures += run_test("{1,1,2,1} in {3,4,1,1,1,2,1,1,2}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 'a', 0x1F600u, 'b' };
        wchar_t needle[] = { (wchar_t)0xD83D, (wchar_t)0xDE00 };
        Match expected[] = {
            { 0, 1, 0, 1 },
        };
        failures += run_test("needle UTF-16 pair -> U+1F600 vs haystack scalars", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0x1F600u, 0x41, 0x1F600u };
        wchar_t needle[] = { (wchar_t)0xD83D, (wchar_t)0xDE00 };
        Match expected[] = {
            { 0, 0, 0, 0 },
            { 0, 2, 0, 2 },
        };
        failures += run_test("two U+1F600 scalars, needle as one surrogate pair", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0x41u, 0x1F604u, 0x5Au };
        wchar_t needle[] = {
            L'A', (wchar_t)0xD83D, (wchar_t)0xDE04, L'Z'
        };
        Match expected[] = {
            { 0, 0, 0, 2 },
        };
        failures += run_test("BMP + surrogate pair + BMP needle vs all scalars", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0xDB3Du };
        wchar_t needle[] = { (wchar_t)0xDB3D };
        Match expected[] = {
            { 0, 0, 0, 0 },
        };
        failures += run_test("lone high surrogate wchar_t preserved as scalar", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0xDE00u };
        wchar_t needle[] = { (wchar_t)0xDE00 };
        Match expected[] = {
            { 0, 0, 0, 0 },
        };
        failures += run_test("lone low surrogate wchar_t preserved as scalar", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0xDB3Du, (unsigned long)L'X' };
        wchar_t needle[] = { (wchar_t)0xDB3D, L'X' };
        Match expected[] = {
            { 0, 0, 0, 1 },
        };
        failures += run_test("high surrogate + BMP when next unit is not low", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 0xDB3Du, 0xDE00u };
        wchar_t needle[] = { (wchar_t)0xDB3D, (wchar_t)0xDE00 };
        failures += run_test("haystack UTF-16 units vs needle merged to scalar (no match)", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, false, term);
    }

    {
        unsigned long haystack[] = {1,2,3,1, 2,3,1,2, 1,2,3};
        wchar_t needle[] = {1,2,3};
        Match expected[] = {
            { 0, 0, 0, 2 },
            { 0, 3, 1, 1 },
            { 2, 0, 2, 2 },
        };
        failures += run_test("wrapped cols=4: {1,2,3} in longer array", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             4, false, false, term);
    }

    {
        unsigned long haystack[] = {9,9,9,2, 3,9,9,9};
        wchar_t needle[] = {2,3};
        Match expected[] = {
            { 0, 3, 1, 0 },
        };
        failures += run_test("wrapped cols=4: needle {2,3} across line break", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             4, false, false, term);
    }

    {
        unsigned long haystack[] = {9,1,2, 3,9};
        wchar_t needle[] = {1,2,3};
        Match expected[] = {
            { 0, 1, 1, 0 },
        };
        failures += run_test("wrapped cols=3: {1,2,3} spanning two termlines", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             3, false, false, term);
    }

    {
        unsigned long haystack[] = {1,7,8,2, 3,7,8,4};
        wchar_t needle[] = {7,8};
        Match expected[] = {
            { 0, 1, 0, 2 },
            { 1, 1, 1, 2 },
        };
        failures += run_test("wrapped cols=4: {7,8} on line 1 and line 2 separately", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             4, false, false, term);
    }

    {
        unsigned long haystack[] = {1,7,2,2,2,4,2,5,5,5,5,5};
        wchar_t needle[] = {2,2,2};
        Match expected[] = {
            { 0, 2, 0, 4 },
        };
        failures += run_test("{2,2,2} in {1,7,2,2,2,4,2,5,5,5,5,5}", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 'H', 'e', 'l', 'l', 'o' };
        wchar_t needle[] = { 'H', 'E', 'L', 'L', 'O' };
        Match expected[] = {
            { 0, 0, 0, 4 },
        };
        failures += run_test("ignore_case: uppercase needle matches mixed-case haystack", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, true, false, term);
    }

    {
        unsigned long haystack[] = { 'H', 'e', 'l', 'l', 'o' };
        wchar_t needle[] = { 'h', 'e', 'l', 'l', 'o' };
        failures += run_test("case sensitive: lower needle does not match leading upper case", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, false, term);
    }

    {
        unsigned long haystack[] = { 'x', 'A', 'b', 'A', 'x' };
        wchar_t needle[] = { 'a', 'b', 'a' };
        Match expected[] = {
            { 0, 1, 0, 3 },
        };
        failures += run_test("ignore_case: AbA matches aba (columns 1..3)", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, true, false, term);
    }

    {
        unsigned long haystack[] = { ' ', 'a', ' ' };
        wchar_t needle[] = { 'a' };
        Match expected[] = {
            { 0, 1, 0, 1 },
        };
        failures += run_test("whole_word: single letter bounded by spaces", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, true, term);
    }

    {
        unsigned long haystack[] = { 'x', 'a' };
        wchar_t needle[] = { 'a' };
        Match expected[] = {
            { 0, 1, 0, 1 },
        };
        failures += run_test("substring: a in xa without whole_word", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             expected, (int)(sizeof expected / sizeof expected[0]),
                             0, false, false, term);
    }

    {
        unsigned long haystack[] = { 'x', 'a' };
        wchar_t needle[] = { 'a' };
        failures += run_test("whole_word: no match when letter is prefixed by word char", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, true, term);
    }

    {
        unsigned long haystack[] = { 'a', 'x' };
        wchar_t needle[] = { 'a' };
        failures += run_test("whole_word: no match when letter is suffixed by word char", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, true, term);
    }

    {
        unsigned long haystack[] = { 'a', 'a', 'a' };
        wchar_t needle[] = { 'a', 'a' };
        failures += run_test("whole_word: no standalone aa inside aaa", haystack,
                             (int)(sizeof haystack / sizeof haystack[0]), needle,
                             (int)(sizeof needle / sizeof needle[0]),
                             NULL, 0, 0, false, true, term);
    }

    return failures;
}