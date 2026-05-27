#include "putty.h"
#include "sftputil.h"
#include <limits.h>
#include <wchar.h>

int wc_to_mb_defchr(int codepage, int flags, const wchar_t *wcstr, int wclen, char *mbstr, int mblen, const char *defchr, int *defused);

const char *sftp_dup_utf8_to_line(int line_codepage, const char *utf8, Seat *seat)
{
    if (line_codepage == CP_UTF8) {
        return utf8;
    }
    int len = strlen(utf8);
    wchar_t *outw = snewn(2*len + 1, wchar_t);
    int outwlen = mb_to_wc(CP_UTF8, 0, utf8, len, outw, 2*len + 1);
    assert((outwlen > 0 || len == 0) && outwlen < 2*len+1);

    size_t outsize = outwlen+1+MB_LEN_MAX;
    char *out = snewn(outsize, char);
    int outlen = 0;
    int defused = 0;
    while (true) {
        outlen = wc_to_mb_defchr(line_codepage, 0, outw, outwlen, out, outsize - 1, "", &defused);
        if (defused) {
            break;
        }
        if ((outlen > 0 || outwlen == 0) && outlen < outsize && outsize - outlen > MB_LEN_MAX) {
            break;
        }
        sgrowarray(out, outsize, outsize*2);
    }
    sfree(outw);

    if (defused) {
        sfree(out);
        sftp_printf(seat, SEAT_OUTPUT_STDERR, "error: failed to convert string to %s: %s", cp_name(line_codepage), utf8);
        return NULL;
    }

    out[outlen] = '\0';
    return out;
}

const char *sftp_dup_utf8_from_line(int line_codepage, const char *s)
{
    assert(s);
    if (line_codepage == CP_UTF8) {
        return s;
    }
    int len = strlen(s);
    int outwlen = 0;
    wchar_t *outw = NULL;
    for (int mult = 1 ;; mult++) {
        outw = snewn(mult*len + 1, wchar_t);
        outwlen = mb_to_wc(line_codepage, 0, s, len, outw, mult*len + 1);
        if ((outwlen > 0 || len == 0) && outwlen < mult*len+1) {
            break;
        }
        sfree(outw);
    }

    size_t outsize = outwlen+MB_LEN_MAX+1;
    char *out = snewn(outsize, char);
    int outlen = 0;
    while (true) {
        outlen = wc_to_mb(CP_UTF8, 0, outw, outwlen, out, outsize, NULL);
        if ((outlen > 0 || outwlen == 0) && outlen < outsize && outsize - outlen > MB_LEN_MAX) {
            break;
        }
        sgrowarray(out, outsize, outsize*2);
    }
    out[outlen] = '\0';
    sfree(outw);
    return out;
}

void sftp_dup_utf8_free(const char *dup, const char *orig)
{
    if (dup && dup != orig) {
        sfree((void *)dup);
    }
}

const char *sftp_utf8_from_line(int line_codepage, const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    const char *utf8 = sftp_dup_utf8_from_line(line_codepage, s);
    if (utf8 != s) {
        sfree((void *)s);
    }
    return utf8;
}

const char *sftp_utf8_to_line(int line_codepage, const char *utf8, Seat *seat)
{
    if (utf8 == NULL) {
        return NULL;
    }
    const char *s = sftp_dup_utf8_to_line(line_codepage, utf8, seat);
    if (s != utf8) {
        sfree((void *)utf8);
    }
    return s;
}
