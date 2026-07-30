
typedef struct Pane Pane;
typedef struct WinGuiFrontend WinGuiFrontend;

Pane *pane_create(const RECT *rect, PointerArraySetIndex set_index_callback);
void pane_destroy(Pane *pane);
void pane_dpi_changed(Pane *pane);
HDWP pane_adjust_window(Pane *pane, const RECT *rect, HDWP hdwp);
void pane_select_tab(Pane *pane, int index);
int pane_get_active_tab(Pane *pane);
void pane_tabs_exchanged(Pane *pane, int old_index, int new_index);
int pane_delete_tab(Pane *pane, int index);
void pane_insert_tab(Pane *pane, int index, const char *title, int image, WinGuiFrontend *wgf);
void pane_set_tab_title(Pane *pane, int index, const char *title);
void pane_set_tab_unusable(Pane *pane, int index, bool unusable);
void pane_set_tab_notified(Pane *pane, int index);
void pane_show_scrollbar(Pane *pane, bool show);
void pane_set_scrollbar(Pane *pane, int total, int start, int page, bool redraw);
int pane_get_scrollbar_track_pos(Pane *pane);
WinGuiFrontend *pane_get_active_wgf(Pane *pane);
WinGuiFrontend *pane_get_wgf(Pane *pane, int index);
int pane_get_tab_count(Pane *pane);
int pane_import_tab(Pane *pane, Pane *source, int index);
Pane *pane_get_from_tab_bar_hwnd(HWND hwnd);
Pane *pane_get_from_term_hwnd(HWND hwnd);
void pane_set_focused(Pane *pane, bool focused);

void pane_update_sizetip(Pane *pane, const RECT *rect, const POINT *pos);
void pane_move_sizetip(Pane *pane, const POINT *pos);
void pane_hide_sizetip(Pane *pane);

void pane_get_term_rect(Pane *pane, RECT *rect);
void pane_get_possible_term_rect(Pane *pane, const RECT *pane_rect, RECT *rect);
