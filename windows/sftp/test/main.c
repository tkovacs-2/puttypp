#include "testassert.h"
#include <stdio.h>
#include <stdlib.h>

void testsuite_sftpbe();
void testsuite_sftpbe_unicode();
void testsuite_linenoise();
void free_reverse_mappings();

DEFINE_ASSERT_FAIL_COUNTER;

void console_print_error_msg(const char *prefix, const char *msg)
{
    fputs(prefix, stderr);
    fputs(": ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

void cleanup_exit(int code)
{
    exit(code);
}

static void testsuite_memleak_gdb()
{
    // no memory leaks should be reported for below lines
    void *p1 = malloc(32);
    void *p2 = realloc(realloc(NULL, 64), 256);
    free(p1);
    p2 = realloc(p2, 0);

    // memory leaks should be reported for below lines
    p1 = malloc(32);
    p2 = realloc(malloc(16), 256);
}

int main()
{
//    testsuite_memleak_gdb();
    testsuite_sftpbe();
    testsuite_sftpbe_unicode();
    testsuite_linenoise();

    free_reverse_mappings();
    ASSERT_CHECK_FAIL_COUNT();
    return 0;
}
