
static void register_frame_class();
static void create_frame_window();

static bool create_conf(const char *saved_session, Conf **conf, const char **session_name);
static WinGuiSession *create_session(Conf *conf, const char *session_name);
static void destroy_session(WinGuiSession *wgs);

static void set_title_from_session(WinGuiSession *wgs);
static void set_icon_title_from_session(WinGuiSession *wgs);
static void realize_palette(WinGuiSession *wgs);

static void activate_session(Pane *pane, int index);
static char *create_session_title(int id, const char *session_name);
static WinGuiSession *add_stale_session(Pane *pane, Conf *conf, const char *session_name, int index);
static WinGuiSession *add_session(Pane *pane, Conf *conf, const char *session_name, int index);
static int delete_session(Pane *pane, int index);
static void close_session(Pane *pane, int index);

static void handle_wm_initmenu(WinGuiSession *wgs, HMENU menu);

static void show_finddlg(WinGuiSession *wgs);
static void update_finddlg(WinGuiSession *wgs);

static bool is_scrollbar_visible(Pane *pane, WinGuiSession *wgs);
static void set_term_hwnd_style(Pane *pane, WinGuiSession *wgs);
static void flip_full_screen();
static void flip_always_on_top();
static void change_focused_pane(Pane *pane);
static void init_term_dimensions(WinGuiSession *wgs);
static void resize_term_dimensions(WinGuiSession *wgs, const RECT *term_rect);
static void snap_frame_to_term(Pane *pane);

static void session_set_index(WinGuiSession *wgs, int index);
static void session_set_pane(WinGuiSession *wgs, Pane *pane);

static void reconfig_session_title(WinGuiSession *wgs, const char *session_name);
static void reconfig_palette(WinGuiSession *wgs);
static void reconfig_term_dimensions(WinGuiSession *wgs, Conf *prev_conf);

static void request_term_resize(WinGuiSession *wgs, int w, int h);
static void set_term_maximised(WinGuiSession *wgs, bool maximised);
static void set_term_minimised(WinGuiSession *wgs, bool minimised);
static void move_term(WinGuiSession *wgs, int x, int y);
static void set_term_zorder(WinGuiSession *wgs, bool top);

static void handle_term_hwnd_setfocus(WinGuiSession *wgs);
static void handle_term_hwnd_killfocus(Pane *pane);
static void handle_term_hwnd_killfocus_on_session(WinGuiSession *wgs);
static void handle_term_hwnd_size(WinGuiSession *wgs, WPARAM wParam);

static HFONT get_dpi_aware_menu_font();
static bool is_mouse_captured();
