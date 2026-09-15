
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
static void add_session(Pane *pane, Conf *conf, const char *session_name, int index);
static int delete_session(Pane *pane, int index);
static void close_session(Pane *pane, int index);

static void handle_wm_initmenu(WinGuiSession *wgs, HMENU menu);

static void show_finddlg(WinGuiSession *wgs);
static void update_finddlg(WinGuiSession *wgs);

static bool is_scrollbar_visible(Pane *pane, WinGuiSession *wgs);
static void set_term_hwnd_style(Pane *pane, WinGuiSession *wgs);
static void flip_always_on_top();
static void change_focused_pane(Pane *pane);
static void init_term_dimensions(WinGuiSession *wgs);
static void resize_term_dimensions(WinGuiSession *wgs, const RECT *term_rect);
static void snap_frame_to_term(Pane *pane);
