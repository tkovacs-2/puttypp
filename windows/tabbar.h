#define TCN_TABDELETE 1
#define TCN_TABEXCHANGE 2

struct TBHDR {
    NMHDR _hdr;
    int _tabOrigin;
};

void create_tab_bar();
void destroy_tab_bar();
void tab_bar_set_measurement();
int tab_bar_get_extra_width();
int tab_bar_get_extra_height();
void tab_bar_adjust_window();
void tab_bar_insert_tab(int index, const char *title, int image);
void tab_bar_remove_tab(int index);
void tab_bar_select_tab(int index);
int tab_bar_get_current_tab();
void tab_bar_set_tab_title(int index, const char *title);
