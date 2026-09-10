#include "unicode.c"

bool put_wc_to_mb_pp(
    BinarySink *bs, int codepage, const wchar_t *wcstr, int wclen,
    const char *defchr, int *defused)
{
    if (!wclen)
        return true;

    reverse_mapping *rmap = get_reverse_mapping(codepage);

    if (rmap) {
        size_t defchr_len = 0;
        bool defchr_len_known = false;

        /* Do this by array lookup if we can. */
        for (size_t i = 0; i < wclen; i++) {
            wchar_t ch = wcstr[i];
            int by;
            const char *blk;

            if ((blk = rmap->blocks[(ch >> 8) & 0xFF]) != NULL &&
                (by = blk[ch & 0xFF]) != '\0')
                put_byte(bs, by);
            else if (ch < 0x80)
                put_byte(bs, ch);
            else if (defchr) {
                if (!defchr_len_known) {
                    defchr_len = strlen(defchr);
                    defchr_len_known = true;
                }
                put_data(bs, defchr, defchr_len);
                *defused = 1;
            }
        }
        return true;
    }

    {
        char internalbuf[2048];
        char *allocbuf = NULL;
        size_t allocsize = 0;
        char *currbuf = internalbuf;
        size_t currsize = lenof(internalbuf);
        bool success;

        if (codepage == CP_UTF8 || !defchr[0]) {
            /*
             * The Win32 API spec says that defchr and defused must be
             * NULL when doing a UTF-8 conversion, on pain of
             * ERROR_INVALID_PARAMETER.
             *
             * Also, translate defchr="" on input to NULL in the Win32
             * API.
             */
            defchr = NULL;
            defused = NULL;
        }

        while (true) {
            int ret = WideCharToMultiByte(
                codepage, 0, wcstr, wclen, currbuf, currsize,
                defchr, defused);

            if (ret) {
                put_data(bs, currbuf, ret);
                success = true;
                break;
            } else if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
                success = false;
                break;
            } else {
                sgrowarray_nm(allocbuf, allocsize, currsize);
                currbuf = allocbuf;
                currsize = allocsize;
            }
        }

        smemclr(allocbuf, allocsize);
        if (success)
            return true;
    }

#ifdef LEGACY_WINDOWS
    /*
     * Fallback for legacy platforms too old to support UTF-8: if
     * the codepage is UTF-8, we can do the translation ourselves.
     */
    if (codepage == CP_UTF8 && wclen > 0) {
        while (wclen > 0) {
            unsigned long wc = (wclen--, *wcstr++);
            if (wclen > 0 && IS_SURROGATE_PAIR(wc, *wcstr)) {
                wc = FROM_SURROGATES(wc, *wcstr);
                wclen--, wcstr++;
            }
            put_utf8_char(bs, wc);
        }

        return true;
    }
#endif

    /* No other fallbacks are available */
    return false;
}

void free_reverse_mappings()
{
    if (!reverse_mappings)
        return;

    reverse_mapping *rmap;
    while ((rmap = delpos234(reverse_mappings, 0)) != NULL) {
        if (rmap->blocks) {
            for (int i = 0; i < 256; i++) {
                if (rmap->blocks[i])
                    sfree(rmap->blocks[i]);
            }
            sfree(rmap->blocks);
        }
        sfree(rmap);
    }
    freetree234(reverse_mappings);
    reverse_mappings = NULL;
}
