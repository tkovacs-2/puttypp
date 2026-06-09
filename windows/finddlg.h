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

void finddlg_create(WCHAR *pattern, bool activate, bool ignore_case, bool whole_word);
void finddlg_destroy();
void finddlg_pin_to_frame(int top_offset);
void finddlg_size_to_frame(int top_offset);
int finddlg_get_text(WCHAR *buffer, int buffer_chars);
bool finddlg_get_ignore_case();
bool finddlg_get_whole_word();
bool finddlg_is_dialog_message(MSG *msg);

#endif
