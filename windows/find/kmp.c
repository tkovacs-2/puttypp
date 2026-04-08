#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <wctype.h>
#include "kmp.h"

#include <stdio.h>

typedef struct KmpContext {
    bool ignore_case;
    bool whole_word;
    unsigned long *ucs;
    int ucs_len;
    int *lps;
} KmpContext;

static unsigned long ucs_to_lower(unsigned long ucs) {
    if (ucs <  0x10000) {
        return towlower((wint_t)ucs);
    }
    return ucs;
}

static bool is_word_ucs(unsigned long ucs) {
    if (ucs == 0) {
        return false;
    }
    if (ucs == (unsigned long)L'_') {
        return true;
    }
    if (ucs < 0x10000) {
        return iswalnum((wint_t)ucs) != 0;
    }
    /* Supplementary planes: treat as word characters for boundary checks */
    return true;
}

static int wcs_to_ucs(const wchar_t *src, int src_len, bool ignore_case, unsigned long *ucs_out) {
    int i = 0, j = 0;
    while (i < src_len) {
        unsigned long ucs = src[i++];
        if ((ucs & 0xFC00) == 0xD800 && (i < src_len)) {
            unsigned long low = src[i];
            if ((low & 0xFC00) == 0xDC00) {
                unsigned long high = ucs;
                ucs = (((high - 0xD800) << 10) | (low - 0xDC00)) + 0x10000;
                i++;
            }
        }
        ucs_out[j++] = ignore_case ? ucs_to_lower(ucs) : ucs;
    }
    return j;
}

static void compute_lps(const unsigned long *needle, int nlen, int *lps) {
    int len = 0;
    lps[0] = 0;
    int i = 1;
    while (i < nlen) {
        if (needle[i] == needle[len]) {
            len++;
            lps[i] = len;
            i++;
        } else {
            if (len != 0) {
                len = lps[len - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
}

static bool print_match(FindIterator *match_start, FindIterator *match_end,
                        int *match_start_distance,
                        KmpContext *ctx,
                        unsigned long match_prev_ucs,
                        unsigned long match_next_ucs,
                        KmpResult *result, void *result_ctx) {
    int correction = *match_start_distance-ctx->ucs_len;
    if (correction > 0) {
        find_iterator_load(match_start);
        for (int i = 1; i < correction; i++) {
            find_iterator_next(match_start);
        }
        if (ctx->whole_word) {
            match_prev_ucs = find_iterator_get_chr(match_start);
        }
        find_iterator_next(match_start);
        find_iterator_unload(match_start);
        *match_start_distance -= correction;
    }
    if (ctx->whole_word && (is_word_ucs(match_prev_ucs) || is_word_ucs(match_next_ucs))) {
        return true;
    }
    return result(match_start, match_end, result_ctx);
}

KmpContext *kmp_prepare_context(const wchar_t *wstr, int wlen, bool ignore_case, bool whole_word) {
    if (wlen == 0) {
        return NULL;
    }
    void *p = malloc(sizeof(KmpContext)+(sizeof(unsigned long)+sizeof(int))*wlen);
    KmpContext *context = (KmpContext *)p;
    context->ignore_case = ignore_case;
    context->whole_word = whole_word;
    p += sizeof(KmpContext);
    context->ucs = (unsigned long *)p;
    context->ucs_len = wcs_to_ucs(wstr, wlen, ignore_case, context->ucs);
    context->lps = (int *)p + context->ucs_len;
    compute_lps(context->ucs, context->ucs_len, context->lps);
    return context;
}

void kmp_free_context(KmpContext *context) {
    free(context);
}

void kmp_search(FindIterator *haystack, KmpContext *ctx, KmpResult *result, void *result_ctx) {
    if (ctx == NULL) {return;}

    int j = 0;
    FindIterator match_start;
    bool match_start_valid = false;
    int match_start_distance = 0;

    unsigned long match_prev_ucs = 0;
    unsigned long ucs = 0;
    unsigned long prev_ucs = 0;

    while (find_iterator_get(haystack) != NULL) {
        prev_ucs = ucs;
        ucs = find_iterator_get_chr(haystack);
        if ((ctx->ignore_case ? ucs_to_lower(ucs) : ucs) == ctx->ucs[j]) {
            if (j == 0) {
                find_iterator_mark(haystack, &match_start);
                match_prev_ucs = prev_ucs;
                match_start_valid = true;
                match_start_distance = 0;
            }
            j++;
            assert(match_start_valid);
            match_start_distance++;
            if (j == ctx->ucs_len) {
                FindIterator match_end;
                find_iterator_mark(haystack, &match_end);
                find_iterator_next(haystack);
                if (!print_match(&match_start, &match_end, &match_start_distance, ctx,
                    match_prev_ucs, find_iterator_get(haystack) != NULL ? find_iterator_get_chr(haystack) : 0,
                    result, result_ctx)) {
                    break;
                }
                j = ctx->lps[j - 1];
                if (j == 0) {
                    match_start_valid = false;
                }
            } else {
                find_iterator_next(haystack);
            }
        } else {
            if (j != 0) {
                assert(match_start_valid);
                j = ctx->lps[j - 1];
            } else {
                match_start_valid = false;
                find_iterator_next(haystack);
            }
        }
    }
}
