#ifndef STUBDIALOG_H
#define STUBDIALOG_H

#include <stdbool.h>
#include <windows.h>

typedef struct Conf Conf;

typedef struct ConfDlgParam {
    Conf *conf;
    const char **session_name;
    bool reconfig;
} ConfDlgParam;

INT_PTR CALLBACK ConfDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef enum TermRemoteAction {
    TERM_REMOTE_RESIZE,
    TERM_REMOTE_MAXIMISE,
    TERM_REMOTE_RESTORE,
    TERM_REMOTE_MINIMISE,
    TERM_REMOTE_MOVE,
    TERM_REMOTE_Z_TOP,
    TERM_REMOTE_Z_BOTTOM,
} TermRemoteAction;

typedef struct TermRemoteResult {
    TermRemoteAction action;
    bool delayed;
    union {
        struct {
            int width;
            int height;
        } resize;
        struct {
            int x;
            int y;
        } move;
    } param;
} TermRemoteResult;

bool do_term_remote(HWND hwnd, TermRemoteResult *result);

#endif
