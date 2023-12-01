#include <stdbool.h>
#include <windows.h>

#include "pastedlg_res.h"

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

void apply_move_item(const RECT *parent, Anchor anchor, const POINT *distance, POINT *p) {
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

void apply_size_item(const RECT *parent, Anchor anchor, const POINT *distance, POINT *p) {
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

static void init_anchor_info(HWND hwnd, const POINT *dpi_info, AnchorInfo *ai, int item_count) {
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

static void apply_anchor_info(HWND hwnd, AnchorInfo *ai, int item_count) {
    RECT parent;
    HDWP hdefer = BeginDeferWindowPos(item_count);
    GetClientRect(hwnd, &parent);
    for (int i=0; i<item_count; i++, ai++) {
        HWND item_hwnd = GetDlgItem(hwnd, ai->item);
        POINT p;
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

static void change_anchor_info_dpi(const POINT *dpi_info, AnchorInfo *ai, int item_count) {
    for (int i=0; i<item_count; i++, ai++) {
        ai->distance_dpi.x = MulDiv(ai->distance.x, dpi_info->x, 96);
        ai->distance_dpi.y = MulDiv(ai->distance.y, dpi_info->y, 96);
    }
}

extern HINSTANCE hinst;
extern HWND frame_hwnd;
extern POINT dpi_info;

static AnchorInfo  anchor_info[3] = {
    {IDC_TEXT, ANCHOR_BOTTOM_RIGHT, OP_SIZE},
    {IDOK, ANCHOR_BOTTOM_MIDDLE, OP_MOVE},
    {IDCANCEL, ANCHOR_BOTTOM_MIDDLE, OP_MOVE}
};

static SIZE dialog_size = {0, 0};
static POINT dialog_dpi_info;
static HWND text_hwnd = NULL;
static HFONT text_hfont = NULL;

typedef struct {
    const RECT *screen_size;
    wchar_t *text;
    size_t text_length;
} InitData;

static void set_text_font() {
    HFONT default_hfont = (HFONT)SendMessage(text_hwnd, WM_GETFONT, 0, 0);
    LOGFONT lf;
    GetObject(default_hfont, sizeof(LOGFONT), &lf);

    text_hfont = CreateFont(lf.lfHeight, 0, 0, 0, FW_DONTCARE, false, false, false,
                                 lf.lfCharSet, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, lf.lfQuality,
                                 FIXED_PITCH | FF_DONTCARE, "Courier New");
    SendMessage(text_hwnd, WM_SETFONT, (WPARAM)text_hfont, (LPARAM)FALSE);
}

static void delete_text_font() {
    DeleteObject(text_hfont);
}

static void push_into_interval(int *start, int *size, int min, int max) {
   if (min+*size > max) {
        *start = min;
        *size = max-min;
    }
    else {
        if (*start < min) {
            *start = min;
        }
        else if (*start+*size > max) {
            *start = max-*size;
        }
    }
}

static void set_inital_size(HWND hwnd, const RECT *ss) {
    int w;
    int h;
    if (dialog_size.cx == 0 || dialog_size.cy == 0) {
        RECT r;
        GetWindowRect(hwnd, &r);
        w = r.right-r.left;
        h = r.bottom-r.top;
    }
    else {
        w = MulDiv(dialog_size.cx, dpi_info.x, 96);
        h = MulDiv(dialog_size.cy, dpi_info.y, 96);
    }
    RECT r;
    GetWindowRect(GetParent(hwnd), &r);
    int x = (r.right+r.left-w)/2;
    int y = (r.bottom+r.top-h)/2;
    push_into_interval(&x, &w, ss->left, ss->right);
    push_into_interval(&y, &h, ss->top, ss->bottom);
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);
}

static void save_size(HWND hwnd) {
    RECT r;
    GetWindowRect(hwnd, &r);
    dialog_size.cx = MulDiv(r.right-r.left, 96, dialog_dpi_info.x);
    dialog_size.cy = MulDiv(r.bottom-r.top, 96, dialog_dpi_info.y);
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

static INT_PTR ClipProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
      case WM_INITDIALOG: {
        dialog_dpi_info = dpi_info;
        SetWindowLongPtr(hwnd, DWLP_USER, lParam);
        init_anchor_info(hwnd, &dpi_info, anchor_info, 3);
        InitData *init_data = (InitData *)lParam;
        set_inital_size(hwnd, init_data->screen_size);
        text_hwnd = GetDlgItem(hwnd, IDC_TEXT);
        set_text_font();
        SetWindowTextW(text_hwnd, init_data->text);
        free(init_data->text);
        init_data->text = NULL;
        return 1;
      }
      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDOK: {
            InitData *init_data = (InitData *)GetWindowLongPtr(hwnd, DWLP_USER);
            init_data->text_length = GetWindowTextLengthW(text_hwnd);
            init_data->text = malloc((init_data->text_length+1)*sizeof(wchar_t));
            GetWindowTextW(text_hwnd, init_data->text, init_data->text_length+1);
            save_size(hwnd);
            EndDialog(hwnd, true);
            delete_text_font();
            break;
          }
          case IDCANCEL:
            save_size(hwnd);
            EndDialog(hwnd, false);
            delete_text_font();
            return 0;
        }
        return 0;
      case WM_CLOSE:
        save_size(hwnd);
        EndDialog(hwnd, false);
        delete_text_font();
        return 0;
      case WM_SIZE:
        apply_anchor_info(hwnd, anchor_info, 3);
        return 0;
      case WM_DPICHANGED:
        dpi_info.x = LOWORD(wParam);
        dpi_info.y = HIWORD(wParam);
        dialog_dpi_info = dpi_info;
        delete_text_font();
        set_text_font();
        change_anchor_info_dpi(&dpi_info, anchor_info, 3);
        apply_anchor_info(hwnd, anchor_info, 3);
        return 0;
    }
    return 0;
}

bool show_paste_confirm(const RECT *screen_size, wchar_t **text, size_t *text_length) {
    InitData init_data = {screen_size, *text, 0};
    bool r = DialogBoxParam(hinst, MAKEINTRESOURCE(IDD_PASTE_CONFIRM), frame_hwnd, (DLGPROC)ClipProc, (LPARAM)&init_data);
    *text = init_data.text;
    *text_length = init_data.text_length;
    return r;
}
