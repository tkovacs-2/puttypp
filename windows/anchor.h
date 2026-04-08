#ifndef ANCHOR_H
#define ANCHOR_H

#include <windows.h>

typedef enum {
    ANCHOR_TOP_MIDDLE,
    ANCHOR_TOP_RIGHT,
    ANCHOR_BOTTOM_LEFT,
    ANCHOR_BOTTOM_MIDDLE,
    ANCHOR_BOTTOM_RIGHT
} Anchor;

typedef enum {
    OP_MOVE,
    OP_SIZE
} Operation;

typedef struct {
    int item;
    Anchor anchor;
    Operation op;
    POINT distance;
    POINT distance_dpi;
} AnchorInfo;

void anchor_init(HWND hwnd, const POINT *dpi_info, AnchorInfo *ai, int item_count);
void anchor_apply(HWND hwnd, AnchorInfo *ai, int item_count);
void anchor_change_dpi(const POINT *dpi_info, AnchorInfo *ai, int item_count);

#endif
