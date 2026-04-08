#ifndef KMP_H
#define KMP_H

#include <wchar.h>
#include "finditerator.h"

/* return true to continue, false to stop searching */
typedef bool KmpResult(FindIterator *match_start, FindIterator *match_end, void *ctx);
typedef struct KmpContext KmpContext;

KmpContext *kmp_prepare_context(const wchar_t *wstr, int wlen, bool ignore_case, bool whole_word);
void kmp_free_context(KmpContext *context);
void kmp_search(FindIterator *haystack, KmpContext *ctx, KmpResult *result, void *result_ctx);

#endif
