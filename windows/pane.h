
typedef struct Pane Pane;
typedef struct WinGuiSession WinGuiSession;

Pane *pane_create(const RECT *rect, PointerArraySetIndex set_index_callback);
void pane_destroy(Pane *pane);

void pane_dpi_changed(Pane *pane);
HDWP pane_adjust_window(Pane *pane, const RECT *rect, HDWP hdwp);
HDWP pane_pin_window(Pane *pane, HDWP hdwp);

void pane_select_session(Pane *pane, int index);
int pane_get_active_session_index(Pane *pane);
void pane_sessions_exchanged(Pane *pane, int old_index, int new_index);
int pane_delete_session(Pane *pane, int index);
void pane_insert_session(Pane *pane, int index, const char *title, int image, WinGuiSession *wgf);
void pane_set_session_title(Pane *pane, int index, const char *title);
void pane_set_session_unusable(Pane *pane, int index, bool unusable);
void pane_set_session_notified(Pane *pane, int index);
int pane_get_session_count(Pane *pane);
int pane_import_session(Pane *pane, Pane *source, int index);

void pane_term_hwnd_style(Pane *pane, bool scrollbar, bool sunken_edge);

void pane_set_scrollbar(Pane *pane, int total, int start, int page, bool redraw);
int pane_get_scrollbar_track_pos(Pane *pane);

WinGuiSession *pane_get_active_session(Pane *pane);
WinGuiSession *pane_get_session(Pane *pane, int index);
void pane_clear_active_session(Pane *pane);

Pane *pane_get_from_tab_bar_hwnd(HWND hwnd);
Pane *pane_get_from_term_hwnd(HWND hwnd);
Pane *pane_get_from_finddlg_hwnd(HWND hwnd);
HWND pane_get_term_hwnd(Pane *pane);

void pane_set_focused(Pane *pane, bool focused);

void pane_update_sizetip(Pane *pane, const RECT *rect, const POINT *pos);
void pane_move_sizetip(Pane *pane, const POINT *pos);
void pane_hide_sizetip(Pane *pane);

void pane_get_term_rect(Pane *pane, RECT *rect);
void pane_get_possible_term_rect(Pane *pane, const RECT *pane_rect, RECT *rect);
void pane_get_requied_rect(Pane *pane, const RECT *term_rect, RECT *rect);

void pane_show_finddlg(Pane *pane, WCHAR *pattern, bool activate, bool ignore_case, bool whole_word);
void pane_hide_finddlg(Pane *pane);
int pane_get_finddlg_text(Pane *pane, WCHAR *buffer, int buffer_chars);
bool pane_get_finddlg_ignore_case(Pane *pane);
bool pane_get_finddlg_whole_word(Pane *pane);

void pane_redraw_term(Pane *pane);
