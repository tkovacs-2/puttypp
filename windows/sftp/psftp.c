#include "putty.h"
#include "psftp.h"
#include <shlwapi.h>

/*
 * Set local current directory. Returns NULL on success, or else an
 * error message which must be freed after printing.
 */
char *psftp_lcd(char *dir)
{
    char *ret = NULL;

    if (!SetCurrentDirectory(dir)) {
        LPVOID message;
        int i;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, GetLastError(),
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR)&message, 0, NULL);
        i = strcspn((char *)message, "\n");
        ret = dupprintf("%.*s", i, (LPCTSTR)message);
        LocalFree(message);
    }

    return ret;
}

/*
 * Get local current directory. Returns a string which must be
 * freed.
 */
char *psftp_getcwd(void)
{
    char *ret = snewn(256, char);
    size_t len = GetCurrentDirectory(256, ret);
    if (len > 256)
        ret = sresize(ret, len, char);
    GetCurrentDirectory(len, ret);
    return ret;
}

static inline uint64_t uint64_from_words(uint32_t hi, uint32_t lo)
{
    return (((uint64_t)hi) << 32) | lo;
}

#define TIME_POSIX_TO_WIN(t, ft) do { \
    ULARGE_INTEGER uli; \
    uli.QuadPart = ((ULONGLONG)(t) + 11644473600ull) * 10000000ull; \
    (ft).dwLowDateTime  = uli.LowPart; \
    (ft).dwHighDateTime = uli.HighPart; \
} while(0)
#define TIME_WIN_TO_POSIX(ft, t) do { \
    ULARGE_INTEGER uli; \
    uli.LowPart  = (ft).dwLowDateTime; \
    uli.HighPart = (ft).dwHighDateTime; \
    uli.QuadPart = uli.QuadPart / 10000000ull - 11644473600ull; \
    (t) = (unsigned long) uli.QuadPart; \
} while(0)

struct RFile {
    HANDLE h;
};

RFile *open_existing_file(const char *name, uint64_t *size,
                          unsigned long *mtime, unsigned long *atime,
                          long *perms)
{
    HANDLE h;
    RFile *ret;

    h = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                   OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    ret = snew(RFile);
    ret->h = h;

    if (size) {
        DWORD lo, hi;
        lo = GetFileSize(h, &hi);
        *size = uint64_from_words(hi, lo);
    }

    if (mtime || atime) {
        FILETIME actime, wrtime;
        GetFileTime(h, NULL, &actime, &wrtime);
        if (atime)
            TIME_WIN_TO_POSIX(actime, *atime);
        if (mtime)
            TIME_WIN_TO_POSIX(wrtime, *mtime);
    }

    if (perms)
        *perms = -1;

    return ret;
}

int read_from_file(RFile *f, void *buffer, int length)
{
    DWORD read;
    if (!ReadFile(f->h, buffer, length, &read, NULL))
        return -1;                     /* error */
    else
        return read;
}

void close_rfile(RFile *f)
{
    CloseHandle(f->h);
    sfree(f);
}

struct WFile {
    HANDLE h;
};

WFile *open_new_file(const char *name, long perms)
{
    HANDLE h;
    WFile *ret;

    h = CreateFile(name, GENERIC_WRITE, 0, NULL,
                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    ret = snew(WFile);
    ret->h = h;

    return ret;
}

WFile *open_existing_wfile(const char *name, uint64_t *size)
{
    HANDLE h;
    WFile *ret;

    h = CreateFile(name, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                   OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    ret = snew(WFile);
    ret->h = h;

    if (size) {
        DWORD lo, hi;
        lo = GetFileSize(h, &hi);
        *size = uint64_from_words(hi, lo);
    }

    return ret;
}

int write_to_file(WFile *f, void *buffer, int length)
{
    DWORD written;
    if (!WriteFile(f->h, buffer, length, &written, NULL))
        return -1;                     /* error */
    else
        return written;
}

void set_file_times(WFile *f, unsigned long mtime, unsigned long atime)
{
    FILETIME actime, wrtime;
    TIME_POSIX_TO_WIN(atime, actime);
    TIME_POSIX_TO_WIN(mtime, wrtime);
    SetFileTime(f->h, NULL, &actime, &wrtime);
}

void close_wfile(WFile *f)
{
    CloseHandle(f->h);
    sfree(f);
}

/* Seek offset bytes through file, from whence, where whence is
   FROM_START, FROM_CURRENT, or FROM_END */
int seek_file(WFile *f, uint64_t offset, int whence)
{
    DWORD movemethod;

    switch (whence) {
      case FROM_START:
        movemethod = FILE_BEGIN;
        break;
      case FROM_CURRENT:
        movemethod = FILE_CURRENT;
        break;
      case FROM_END:
        movemethod = FILE_END;
        break;
      default:
        return -1;
    }

    {
        LONG lo = offset & 0xFFFFFFFFU, hi = offset >> 32;
        SetFilePointer(f->h, lo, &hi, movemethod);
    }

    if (GetLastError() != NO_ERROR)
        return -1;
    else
        return 0;
}

uint64_t get_file_posn(WFile *f)
{
    LONG lo, hi = 0;

    lo = SetFilePointer(f->h, 0L, &hi, FILE_CURRENT);
    return uint64_from_words(hi, lo);
}

int file_type(const char *name)
{
    DWORD attr;
    attr = GetFileAttributes(name);
    /* We know of no `weird' files under Windows. */
    if (attr == (DWORD)-1)
        return FILE_TYPE_NONEXISTENT;
    else if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return FILE_TYPE_DIRECTORY;
    else
        return FILE_TYPE_FILE;
}

struct DirHandle {
    HANDLE h;
    char *name;
};

DirHandle *open_directory(const char *name, const char **errmsg)
{
    HANDLE h;
    WIN32_FIND_DATA fdat;
    char *findfile;
    DirHandle *ret;

    /* Enumerate files in dir `foo'. */
    findfile = dupcat(name, "/*");
    h = FindFirstFile(findfile, &fdat);
    if (h == INVALID_HANDLE_VALUE) {
        *errmsg = win_strerror(GetLastError());
        return NULL;
    }
    sfree(findfile);

    ret = snew(DirHandle);
    ret->h = h;
    ret->name = dupstr(fdat.cFileName);
    return ret;
}

char *read_filename(DirHandle *dir)
{
    do {

        if (!dir->name) {
            WIN32_FIND_DATA fdat;
            if (!FindNextFile(dir->h, &fdat))
                return NULL;
            else
                dir->name = dupstr(fdat.cFileName);
        }

        assert(dir->name);
        if (dir->name[0] == '.' &&
            (dir->name[1] == '\0' ||
             (dir->name[1] == '.' && dir->name[2] == '\0'))) {
            sfree(dir->name);
            dir->name = NULL;
        }

    } while (!dir->name);

    if (dir->name) {
        char *ret = dir->name;
        dir->name = NULL;
        return ret;
    } else
        return NULL;
}

void close_directory(DirHandle *dir)
{
    FindClose(dir->h);
    if (dir->name)
        sfree(dir->name);
    sfree(dir);
}

int test_wildcard(const char *name, bool cmdline)
{
    HANDLE fh;
    WIN32_FIND_DATA fdat;

    /* First see if the exact name exists. */
    if (GetFileAttributes(name) != (DWORD)-1)
        return WCTYPE_FILENAME;

    /* Otherwise see if a wildcard match finds anything. */
    fh = FindFirstFile(name, &fdat);
    if (fh == INVALID_HANDLE_VALUE)
        return WCTYPE_NONEXISTENT;

    FindClose(fh);
    return WCTYPE_WILDCARD;
}

struct WildcardMatcher {
    HANDLE h;
    char *name;
    char *srcpath;
};

char *stripslashes(const char *str, bool local)
{
    char *p;

    /*
     * On Windows, \ / : are all path component separators.
     */

    if (local) {
        p = strchr(str, ':');
        if (p) str = p+1;
    }

    p = strrchr(str, '/');
    if (p) str = p+1;

    if (local) {
        p = strrchr(str, '\\');
        if (p) str = p+1;
    }

    return (char *)str;
}

WildcardMatcher *begin_wildcard_matching(const char *name)
{
    HANDLE h;
    WIN32_FIND_DATA fdat;
    WildcardMatcher *ret;
    char *last;

    h = FindFirstFile(name, &fdat);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    ret = snew(WildcardMatcher);
    ret->h = h;
    ret->srcpath = dupstr(name);
    last = stripslashes(ret->srcpath, true);
    *last = '\0';
    if (fdat.cFileName[0] == '.' &&
        (fdat.cFileName[1] == '\0' ||
         (fdat.cFileName[1] == '.' && fdat.cFileName[2] == '\0')))
        ret->name = NULL;
    else
        ret->name = dupcat(ret->srcpath, fdat.cFileName);

    return ret;
}

char *wildcard_get_filename(WildcardMatcher *dir)
{
    while (!dir->name) {
        WIN32_FIND_DATA fdat;

        if (!FindNextFile(dir->h, &fdat))
            return NULL;

        if (fdat.cFileName[0] == '.' &&
            (fdat.cFileName[1] == '\0' ||
             (fdat.cFileName[1] == '.' && fdat.cFileName[2] == '\0')))
            dir->name = NULL;
        else
            dir->name = dupcat(dir->srcpath, fdat.cFileName);
    }

    if (dir->name) {
        char *ret = dir->name;
        dir->name = NULL;
        return ret;
    } else
        return NULL;
}

void finish_wildcard_matching(WildcardMatcher *dir)
{
    FindClose(dir->h);
    if (dir->name)
        sfree(dir->name);
    sfree(dir->srcpath);
    sfree(dir);
}

bool vet_filename(const char *name)
{
    if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ':'))
        return false;

    if (!name[strspn(name, ".")])      /* entirely composed of dots */
        return false;

    return true;
}

bool create_directory(const char *name)
{
    return CreateDirectory(name, NULL) != 0;
}

char *dir_file_cat(const char *dir, const char *file)
{
    ptrlen dir_pl = ptrlen_from_asciz(dir);
    return dupcat(
        dir, (ptrlen_endswith(dir_pl, PTRLEN_LITERAL("\\"), NULL) ||
              ptrlen_endswith(dir_pl, PTRLEN_LITERAL("/"), NULL)) ? "" : "\\",
        file);
}

const char *get_absolute_path(const char *pwd, const char *name)
{
    if (!PathIsRelative(name)) {
        return dupstr(name);
    }

    char *t = dupcat(pwd, "\\", name);
    char result[MAX_PATH];
    if (PathCanonicalize(result, t))
    {
        sfree(t);
        return dupstr(result);
    }
    return t;
}

void delete_file(const char *name)
{
    DeleteFile(name);
}

void delete_directory(const char *name)
{
    RemoveDirectory(name);
}

const char *truncate_path(const char *name)
{
    while (*name == '\\' || *name == '/') {
        name++;
    }
    while (*name && *name != '\\' && *name != '/') {
        name++;
    }
    return name;
}

char get_path_separator()
{
    return '\\';
}
