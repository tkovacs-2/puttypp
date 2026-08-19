#ifndef FINDDLG_H
#define FINDDLG_H

#include <windows.h>
#include <stdbool.h>

#define FINDDLG_NOTIFY_ID 1
#define FINDDLG_EDIT_CHANGED 1
#define FINDDLG_EDIT_ENTER 2
#define FINDDLG_UP 3
#define FINDDLG_DOWN 4
#define FINDDLG_CLOSE 5
#define FINDDLG_IGNORE_CASE 6
#define FINDDLG_WHOLE_WORD 7

typedef struct FindDlg {
    HWND hwnd;
    int frame_top_offset;
    bool disable_notification;
    HFONT hfont;
    HWND last_focus;
    SIZE base_units;
    SIZE window_size;
    SIZE compact_window_size;
    bool compact;
    bool hidden;
    POINT pin_offset;
} FindDlg;

void finddlg_init(FindDlg *finddlg);
void finddlg_uninit(FindDlg *finddlg);
void finddlg_show(FindDlg *finddlg, const RECT *parent_rect, WCHAR *pattern, bool activate, bool ignore_case, bool whole_word);
void finddlg_hide(FindDlg *finddlg);
void finddlg_adjust_window(FindDlg *finddlg, const RECT *parent_rect);
HDWP finddlg_pin_window(FindDlg *finddlg, HDWP hdwp);
int finddlg_get_text(FindDlg *finddlg, WCHAR *buffer, int buffer_chars);
bool finddlg_get_ignore_case(FindDlg *finddlg);
bool finddlg_get_whole_word(FindDlg *finddlg);
bool finddlg_is_dialog_message(MSG *msg);
void finddlg_dpi_changed(FindDlg *finddlg);

#endif
