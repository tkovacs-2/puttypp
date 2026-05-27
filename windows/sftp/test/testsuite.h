#ifndef TESTSUITE_H
#define TESTSUITE_H

#define BEGIN_TESTSUITE(suite_name, ...) \
struct { \
    void (*function)(__VA_ARGS__); \
    const char *name; \
} suite_name[] = {

#define END_TESTSUITE() \
{NULL, NULL} \
};

#define ADD_TESTCASE(function) \
{function, #function},

#endif
