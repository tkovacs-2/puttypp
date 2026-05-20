#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

extern int32_t ucase_fold(int32_t c);
extern bool u_isalnum(int32_t c);

static int check_fold(int32_t c, int32_t expected) {
    int32_t r = ucase_fold(c);
    printf("ucase_fold(U+%04" PRIX32 ") -> U+%04" PRIX32 "  %s\n", c, r, r == expected ? "PASS" : "FAIL");
    return r != expected;
}

static int check_isalnum(int32_t c, bool expected) {
    bool r = u_isalnum(c);
    printf("u_isalnum(U+%04" PRIX32 ") -> %s  %s\n", c, r ? "true" : "false", r == expected ? "PASS" : "FAIL");
    return r != expected;
}

int test_unicode() {
    int failures = 0;
    failures += check_fold('A', 'a');
    failures += check_fold('I', 'i');
    failures += check_fold(0x130, 0x130);
    failures += check_fold(0x41B, 0x43B);
    failures += check_fold(0x394, 0x3B4);
    failures += check_fold(0x1E4E, 0x1E4F);
    failures += check_fold(0x1F3A, 0x1F32);
    failures += check_fold(0x1F604, 0x1F604);
    failures += check_fold(0x110000, 0x110000);
    failures += check_isalnum('A', true);
    failures += check_isalnum('I', true);
    failures += check_isalnum(0x130, true);
    failures += check_isalnum(0x41B, true);
    failures += check_isalnum(0x394, true);
    failures += check_isalnum(0x1E4E, true);
    failures += check_isalnum(0x1F3A, true);
    failures += check_isalnum(0x1F604, false);
    failures += check_isalnum(0x110000, false);
    failures += check_isalnum(0x203c, false);
    failures += check_isalnum('_', false);
    failures += check_isalnum('1', true);
    return failures;
}
