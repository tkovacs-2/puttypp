
typedef struct StubImageList {
    int cx, cy;
    int count;
} StubImageList;

WINUSERAPI HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int grow, int init) {
    StubImageList *il = calloc(1, sizeof(*il));
    il->cx = cx;
    il->cy = cy;
    return (HIMAGELIST)il;
}

WINUSERAPI BOOL WINAPI ImageList_Destroy(HIMAGELIST himl) {
    StubImageList *il = (StubImageList *)himl;
    free(il);
    return TRUE;
}

WINUSERAPI int WINAPI ImageList_Add(HIMAGELIST himl, HBITMAP hbm, HBITMAP mask) {
    StubImageList *il = (StubImageList *)himl;
    return il->count++;
}

WINUSERAPI int WINAPI ImageList_ReplaceIcon(HIMAGELIST himl, int i, HICON icon) {
    StubImageList *il = (StubImageList *)himl;
    if (i < 0) {
        i = il->count++;
    }
    return i;
}

WINUSERAPI BOOL WINAPI ImageList_SetIconSize(HIMAGELIST himl, int cx, int cy) {
    StubImageList *il = (StubImageList *)himl;
    il->cx = cx;
    il->cy = cy;
    return TRUE;
}

WINUSERAPI BOOL WINAPI ImageList_Draw(HIMAGELIST, int, HDC, int, int, UINT) {
    return TRUE;
}
