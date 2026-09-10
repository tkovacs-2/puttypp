#include "putty.h"
#include "sftputil.h"

bool put_wc_to_mb_pp(BinarySink *bs, int codepage, const wchar_t *wcstr, int wclen, const char *defchr, int *defused);

const char *sftp_dup_utf8_to_line(int line_codepage, const char *utf8, Seat *seat)
{
    if (line_codepage == CP_UTF8) {
        return utf8;
    }

    strbuf *wsb = strbuf_new();
    put_mb_to_wc(wsb, CP_UTF8, utf8, strlen(utf8));
    int outwlen = wsb->len / sizeof(wchar_t);
    wchar_t *outw = (wchar_t *)strbuf_to_str(wsb);

    strbuf *sb = strbuf_new();
    int defused = 0;
    put_wc_to_mb_pp(BinarySink_UPCAST(sb), line_codepage, outw, outwlen, "", &defused);
    sfree(outw);

    if (defused) {
        strbuf_free(sb);
        sftp_printf(seat, SEAT_OUTPUT_STDERR, "error: failed to convert string to %s: %s", cp_name(line_codepage), utf8);
        return NULL;
    }
    return strbuf_to_str(sb);
}

const char *sftp_dup_utf8_from_line(int line_codepage, const char *s)
{
    assert(s);
    if (line_codepage == CP_UTF8) {
        return s;
    }

    strbuf *wsb = strbuf_new();
    put_mb_to_wc(wsb, line_codepage, s, strlen(s));
    int outwlen = wsb->len / sizeof(wchar_t);
    wchar_t *outw = (wchar_t *)strbuf_to_str(wsb);

    strbuf *sb = strbuf_new();
    put_wc_to_mb(sb, CP_UTF8, outw, outwlen, NULL);
    sfree(outw);
    return strbuf_to_str(sb);
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
