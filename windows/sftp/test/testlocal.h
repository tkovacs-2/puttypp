#ifndef TESTLOCAL_H
#define TESTLOCAL_H

#include "putty.h"
#include "sftpbe.h"
#include "testremote.h"

typedef struct TestOutput {
    const char **lines;
    size_t size, capacity;
    char *pending_line;
    size_t pending_line_len;
} TestOutput;

typedef enum {
    SF_NONE,
    SF_NOTIFY_REMOTE_EXIT,
    SF_CONNECTION_FATAL
} SeatFunction;

typedef struct TestLocal {
    Seat testseat;
    Backend *sftp;
    TestOutput output;
    TestOutput error;
    SeatFunction called_seat_function;
    bool allow_cli_output;
} TestLocal;

void testlocal_init(TestLocal *tl, TestRemote *tr, const char *line_codepage);
void testlocal_uninit(TestLocal *tl);

void testlocal_execute(TestLocal *tl, const char *command);

void testlocal_add_file(TestLocal *tl, const char *name, size_t size);
void testlocal_add_dir(TestLocal *tl, const char *name);
bool testlocal_check_file(TestLocal *tl, const char *name);
bool testlocal_check_dir(TestLocal *tl, const char *name);

size_t testlocal_check_size(TestLocal *tl, const char *name);
bool testlocal_check_create_size(TestLocal *tl, const char *name, size_t size);

void testlocal_allow_cli_output(TestLocal *tl, bool allow);
void testlocal_clear_output(TestLocal *tl);

const char *testlocal_find_output(TestOutput *o, const char *pattern, bool exact_match);
bool testlocal_empty_output(TestOutput *o);

#endif
