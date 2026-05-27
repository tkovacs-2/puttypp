#ifndef TESTASSERT_H
#define TESTASSERT_H

#include <stdio.h>

extern int assert_fail_count;

#define DEFINE_ASSERT_FAIL_COUNTER \
int assert_fail_count = 0;

#define ASSERT_TRUE(expression) \
if (!(expression)) { \
    assert_fail_count++; \
    printf("FAILED '%s' is not true. %s:%d\n", #expression, __FILE__, __LINE__); \
}

#define ASSERT_FALSE(expression) \
if (expression) { \
    assert_fail_count++; \
    printf("FAILED '%s' is not false. %s:%d\n", #expression, __FILE__, __LINE__); \
}

#define ASSERT_CHECK_FAIL_COUNT() \
if (assert_fail_count > 0) { \
    printf("FAILED %d assertions failed.\n", assert_fail_count); \
    return 1; \
} else { \
    printf("SUCCESS all assertions passed.\n"); \
}
#endif
