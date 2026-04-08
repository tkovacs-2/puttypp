#include "anchor.h"

static void init_move_item(const RECT *parent, const RECT *self, Anchor anchor, POINT *distance) {
    switch (anchor) {
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
        distance->y = self->top-parent->top;
        break;
      case ANCHOR_BOTTOM_LEFT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        distance->y = self->top-parent->bottom;
        break;
    }
    switch (anchor) {
      case ANCHOR_BOTTOM_LEFT:
        distance->x = self->left-parent->left;
        break;
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_BOTTOM_MIDDLE:
        distance->x = self->left-(parent->right-parent->left)/2;
        break;
      case ANCHOR_TOP_RIGHT:
      case ANCHOR_BOTTOM_RIGHT:
        distance->x = self->left-parent->right;
        break;
    }
}

static void init_size_item(const RECT *parent, const RECT *self, Anchor anchor, POINT *distance) {
    switch (anchor) {
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
        distance->y = self->bottom-parent->top;
        break;
      case ANCHOR_BOTTOM_LEFT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        distance->y = self->bottom-parent->bottom;
        break;
    }
    switch (anchor) {
      case ANCHOR_BOTTOM_LEFT:
        distance->x = self->right-parent->left;
        break;
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        distance->x = self->right-parent->right;
        break;
    }
}

static void apply_move_item(const RECT *parent, Anchor anchor, const POINT *distance, POINT *p) {
    switch (anchor) {
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
        p->y = distance->y+parent->top;
        break;
      case ANCHOR_BOTTOM_LEFT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        p->y = distance->y+parent->bottom;
        break;
    }
    switch (anchor) {
      case ANCHOR_BOTTOM_LEFT:
        p->x = distance->x+parent->left;
        break;
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_BOTTOM_MIDDLE:
        p->x = distance->x+(parent->right-parent->left)/2;
        break;
      case ANCHOR_TOP_RIGHT:
      case ANCHOR_BOTTOM_RIGHT:
        p->x = distance->x+parent->right;
        break;
    }
}

static void apply_size_item(const RECT *parent, Anchor anchor, const POINT *distance, POINT *p) {
    switch (anchor) {
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
        p->y = distance->y+parent->top;
        break;
      case ANCHOR_BOTTOM_LEFT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        p->y = distance->y+parent->bottom;
        break;
    }
    switch (anchor) {
      case ANCHOR_BOTTOM_LEFT:
        p->x = distance->x+parent->left;
        break;
      case ANCHOR_TOP_MIDDLE:
      case ANCHOR_TOP_RIGHT:
      case ANCHOR_BOTTOM_MIDDLE:
      case ANCHOR_BOTTOM_RIGHT:
        p->x = distance->x+parent->right;
        break;
    }
}

void anchor_init(HWND hwnd, const POINT *dpi_info, AnchorInfo *ai, int item_count) {
    RECT parent;
    RECT self;
    GetClientRect(hwnd, &parent);
    for (int i=0; i<item_count; i++, ai++) {
        HWND item_hwnd = GetDlgItem(hwnd, ai->item);
        GetWindowRect(item_hwnd, &self);
        MapWindowPoints(NULL, hwnd, (POINT *)&self, 2);
        if (ai->op == OP_MOVE) {
            init_move_item(&parent, &self, ai->anchor, &ai->distance_dpi);
        } else {
            init_size_item(&parent, &self, ai->anchor, &ai->distance_dpi);
        }
        ai->distance.x = MulDiv(ai->distance_dpi.x, 96, dpi_info->x);
        ai->distance.y = MulDiv(ai->distance_dpi.y, 96, dpi_info->y);
    }
}

void anchor_apply(HWND hwnd, AnchorInfo *ai, int item_count) {
    RECT parent;
    HDWP hdefer = BeginDeferWindowPos(item_count);
    GetClientRect(hwnd, &parent);
    for (int i=0; i<item_count; i++, ai++) {
        HWND item_hwnd = GetDlgItem(hwnd, ai->item);
        POINT p = {0, 0};
        if (ai->op == OP_MOVE) {
            apply_move_item(&parent, ai->anchor, &ai->distance_dpi, &p);
            DeferWindowPos(hdefer, item_hwnd, NULL, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        } else {
            RECT self;
            GetWindowRect(item_hwnd, &self);
            MapWindowPoints(NULL, hwnd, (POINT *)&self, 2);
            apply_size_item(&parent, ai->anchor, &ai->distance_dpi, &p);
            DeferWindowPos(hdefer, item_hwnd, NULL, 0, 0, p.x-self.left, p.y-self.top, SWP_NOMOVE | SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
        }
    }
    EndDeferWindowPos(hdefer);
}

void anchor_change_dpi(const POINT *dpi_info, AnchorInfo *ai, int item_count) {
    for (int i=0; i<item_count; i++, ai++) {
        ai->distance_dpi.x = MulDiv(ai->distance.x, dpi_info->x, 96);
        ai->distance_dpi.y = MulDiv(ai->distance.y, dpi_info->y, 96);
    }
}
