#include "utrie.h"
#include <stdbool.h>

#define UCASE_EXCEPTION 8
#define UCASE_HAS_EXCEPTION(props) ((props)&UCASE_EXCEPTION)
#define UCASE_IS_UPPER_OR_TITLE(props) ((props)&2)

#define UCASE_DELTA_SHIFT 7
#define UCASE_GET_DELTA(props) ((int16_t)(props) >> UCASE_DELTA_SHIFT)

#define UCASE_EXC_SHIFT 4

enum {
    UCASE_EXC_LOWER,
    UCASE_EXC_FOLD,
    UCASE_EXC_UPPER,
    UCASE_EXC_TITLE,
    UCASE_EXC_DELTA,
    UCASE_EXC_5,
    UCASE_EXC_CLOSURE,
    UCASE_EXC_FULL_MAPPINGS,
    UCASE_EXC_ALL_SLOTS
};

#define UCASE_EXC_DOUBLE_SLOTS 0x100

enum {
    UCASE_EXC_NO_SIMPLE_CASE_FOLDING = 0x200,
    UCASE_EXC_DELTA_IS_NEGATIVE = 0x400,
    UCASE_EXC_SENSITIVE = 0x800
};

#define UCASE_EXC_CONDITIONAL_FOLD 0x8000

enum {
    UCASE_IX_INDEX_TOP,
    UCASE_IX_LENGTH,
    UCASE_IX_TRIE_SIZE,
    UCASE_IX_EXC_LENGTH,
    UCASE_IX_UNFOLD_LENGTH,

    UCASE_IX_MAX_FULL_LENGTH=15,
    UCASE_IX_TOP=16
};

struct UCaseProps {
    void *mem;
    const int32_t *indexes;
    const uint16_t *exceptions;
    const uint16_t *unfold;
    struct UTrie2 trie;
    uint8_t formatVersion[4];
};
typedef struct UCaseProps UCaseProps;

#define INCLUDED_FROM_UCASE_CPP
#define nullptr 0
#include "ucase_props_data.h"
#undef nullptr

#define GET_EXCEPTIONS(csp, props) ((csp)->exceptions + ((props) >> UCASE_EXC_SHIFT))

static const uint8_t flagsOffset[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

#define HAS_SLOT(flags, idx) ((flags) & (1 << (idx)))
#define SLOT_OFFSET(flags, idx) flagsOffset[(flags) & ((1 << (idx)) - 1)]

#define UPRV_BLOCK_MACRO_BEGIN do
#define UPRV_BLOCK_MACRO_END while (false)

#define GET_SLOT_VALUE(excWord, idx, pExc16, value) UPRV_BLOCK_MACRO_BEGIN { \
    if (((excWord) & UCASE_EXC_DOUBLE_SLOTS) == 0) { \
        (pExc16) += SLOT_OFFSET(excWord, idx); \
        (value) = *pExc16; \
    } else { \
        (pExc16) += 2 * SLOT_OFFSET(excWord, idx); \
        (value) = *pExc16++; \
        (value) = ((value) << 16) | *pExc16; \
    } \
} UPRV_BLOCK_MACRO_END

/*
 * Case folding is similar to lowercasing.
 * The result may be a simple mapping, i.e., a single code point, or
 * a full mapping, i.e., a string.
 * If the case folding for a code point is the same as its simple (1:1) lowercase mapping,
 * then only the lowercase mapping is stored.
 *
 * Some special cases are hardcoded because their conditions cannot be
 * parsed and processed from CaseFolding.txt.
 *
 * Unicode 3.2 CaseFolding.txt specifies for its status field:

# C: common case folding, common mappings shared by both simple and full mappings.
# F: full case folding, mappings that cause strings to grow in length. Multiple characters are separated by spaces.
# S: simple case folding, mappings to single characters where different from F.
# T: special case for uppercase I and dotted uppercase I
#    - For non-Turkic languages, this mapping is normally not used.
#    - For Turkic languages (tr, az), this mapping can be used instead of the normal mapping for these characters.
#
# Usage:
#  A. To do a simple case folding, use the mappings with status C + S.
#  B. To do a full case folding, use the mappings with status C + F.
#
#    The mappings with status T can be used or omitted depending on the desired case-folding
#    behavior. (The default option is to exclude them.)

 * Unicode 3.2 has 'T' mappings as follows:

0049; T; 0131; # LATIN CAPITAL LETTER I
0130; T; 0069; # LATIN CAPITAL LETTER I WITH DOT ABOVE

 * while the default mappings for these code points are:

0049; C; 0069; # LATIN CAPITAL LETTER I
0130; F; 0069 0307; # LATIN CAPITAL LETTER I WITH DOT ABOVE

 * U+0130 has no simple case folding (simple-case-folds to itself).
 */

/* return the simple case folding mapping for c */
UChar32 ucase_fold(UChar32 c) {
    uint16_t props = UTRIE2_GET16(&ucase_props_singleton.trie, c);
    if (!UCASE_HAS_EXCEPTION(props)) {
        if (UCASE_IS_UPPER_OR_TITLE(props)) {
            c += UCASE_GET_DELTA(props);
        }
    } else {
        const uint16_t *pe = GET_EXCEPTIONS(&ucase_props_singleton, props);
        uint16_t excWord = *pe++;
        int32_t idx;
        if (excWord & UCASE_EXC_CONDITIONAL_FOLD) {
            /* special case folding mappings, hardcoded */
            if (c == 0x49) {
                /* 0049; C; 0069; # LATIN CAPITAL LETTER I */
                return 0x69;
            } else if (c == 0x130) {
                /* no simple case folding for U+0130 */
                return c;
            }
        }
        if ((excWord & UCASE_EXC_NO_SIMPLE_CASE_FOLDING) != 0) {
            return c;
        }
        if (HAS_SLOT(excWord, UCASE_EXC_DELTA) && UCASE_IS_UPPER_OR_TITLE(props)) {
            int32_t delta;
            GET_SLOT_VALUE(excWord, UCASE_EXC_DELTA, pe, delta);
            return (excWord & UCASE_EXC_DELTA_IS_NEGATIVE) == 0 ? c + delta : c - delta;
        }
        if (HAS_SLOT(excWord, UCASE_EXC_FOLD)) {
            idx = UCASE_EXC_FOLD;
        } else if (HAS_SLOT(excWord, UCASE_EXC_LOWER)) {
            idx = UCASE_EXC_LOWER;
        } else {
            return c;
        }
        GET_SLOT_VALUE(excWord, idx, pe, c);
    }
    return c;
}
