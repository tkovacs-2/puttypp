typedef struct Pane Pane;
typedef struct Split Split;

typedef enum SplitType {
    SPLIT_TYPE_PANE,
    SPLIT_TYPE_HORIZONTAL,
    SPLIT_TYPE_VERTICAL
} SplitType;

typedef enum SplitPart {
    SPLIT_PART_FIRST,
    SPLIT_PART_SECOND
} SplitPart;

#define SPLITTER_NOTIFY_ID 2
#define SPLITTER_NOTIFY_MERGE 0

void split_common_init();
void split_common_dpi_changed();

Split *split_create(const RECT *rect);
void split_destroy(Split *split);

void split_split(Split *split, SplitType type, SplitPart new_pane);
Pane *split_merge(Split *split);

void split_plan_layout(Split *split, const RECT *rect);
void split_apply_layout(Split *split);
void split_pin_layout(Split *split);

const RECT *split_get_rect(Split *split);
Pane *split_get_pane(Split *split);
Split *split_get_first(Split *split);
Split *split_get_second(Split *split);

void split_dpi_changed(Split *split);

Split *split_get_from_point(Split *split, const POINT *point);
SplitType split_get_possible_split(Split *split, bool slim, const POINT *point, SplitPart *part);
SplitType split_get_possible_split_rect(Split *split, bool slim, const POINT *point, RECT *rect);

Split *split_find_parent(Split *split, Pane *pane);

Split *split_get_from_hwnd(HWND hwnd);

void split_update_sizetips(Split *split);
void split_move_sizetips(Split *split);
void split_hide_sizetips(Split *split);

Pane *split_find_pane(Split *split);
