
static void register_frame_class();
static HWND create_frame_window(Conf *conf, int guess_width, int guess_height);
static void adjust_terminal_window(HWND frame_hwnd, HWND term_hwnd);
static void adjust_extra_size();
static void adjust_client_size(int *width, int *height);

static bool create_conf(const char *saved_session, Conf **conf, const char **session_name);
static WinGuiFrontend *create_frontend(Conf *conf, const char *session_name);
static void destroy_frontend(WinGuiFrontend *wgf);

static void set_title_from_session(WinGuiFrontend *wgf);
static void set_icon_title_from_session(WinGuiFrontend *wgf);
static void set_scrollbar_from_session(WinGuiFrontend *wgf, bool redraw);
static bool set_frame_style(Conf *conf);
static void realize_palette(WinGuiFrontend *wgf);

static void activate_session(WinGuiFrontend *wgf);
static char *create_tab_title(int id, const char *session_name);
static void add_session_tab(int protocol, const char *session_name, int index);
static void add_session(Conf *conf, const char *session_name, int index);
static void delete_session(WinGuiFrontend *wgf);

static void handle_wm_notify(LPARAM lParam);
static void handle_wm_initmenu(WPARAM wParam);
