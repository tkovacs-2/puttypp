#ifndef PANE_SIZETIP_H
#define PANE_SIZETIP_H

typedef struct SizeTip {
    HWND hwnd;
    int width;
    int height;
    SIZE size;
    WCHAR text[32];
} SizeTip;

void size_tip_common_init(HFONT dpi_aware_font);
void size_tip_common_dpi_changed(HFONT dpi_aware_font);

void size_tip_init(SizeTip *size_tip);
void size_tip_uninit(SizeTip *size_tip);
void size_tip_update(SizeTip *size_tip, int width, int height, const RECT *rect);
HDWP size_tip_move(SizeTip *size_tip, const POINT *point, HDWP hdwp);
void size_tip_hide(SizeTip *size_tip);

#endif
