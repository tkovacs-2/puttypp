#include "utrie.h"
#include <stdbool.h>

enum UCharCategory {
    U_UPPERCASE_LETTER        = 1,
    U_LOWERCASE_LETTER        = 2,
    U_TITLECASE_LETTER        = 3,
    U_MODIFIER_LETTER         = 4,
    U_OTHER_LETTER            = 5,
    U_DECIMAL_DIGIT_NUMBER = 9,
};

#define U_MASK(x) ((uint32_t)1 << (x))

#define U_GC_LU_MASK    U_MASK(U_UPPERCASE_LETTER)
#define U_GC_LL_MASK    U_MASK(U_LOWERCASE_LETTER)
#define U_GC_LT_MASK    U_MASK(U_TITLECASE_LETTER)
#define U_GC_LM_MASK    U_MASK(U_MODIFIER_LETTER)
#define U_GC_LO_MASK    U_MASK(U_OTHER_LETTER)

#define U_GC_ND_MASK    U_MASK(U_DECIMAL_DIGIT_NUMBER)

#define U_GC_L_MASK (U_GC_LU_MASK|U_GC_LL_MASK|U_GC_LT_MASK|U_GC_LM_MASK|U_GC_LO_MASK)

#define GET_CATEGORY(props) ((props)&0x1f)
#define CAT_MASK(props) U_MASK(GET_CATEGORY(props))
#define GET_PROPS(c, result) ((result) = UTRIE2_GET16(&propsTrie, c))

#define INCLUDED_FROM_UCHAR_C
#define nullptr 0
#include "uchar_props_data.h"
#undef nullptr

UBool u_isalnum(UChar32 c) {
    uint32_t props;
    GET_PROPS(c, props);
    return (CAT_MASK(props)&(U_GC_L_MASK|U_GC_ND_MASK))!=0;
}
