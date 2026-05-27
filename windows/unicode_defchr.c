#define wc_to_mb wc_to_mb_original
#include "unicode.c"
#undef wc_to_mb

int wc_to_mb(int codepage, int flags, const wchar_t *wcstr, int wclen, char *mbstr, int mblen, const char *defchr)
{
    return wc_to_mb_original(codepage, flags, wcstr, wclen, mbstr, mblen, defchr);
}

int wc_to_mb_defchr(int codepage, int flags, const wchar_t *wcstr, int wclen, char *mbstr, int mblen, const char *defchr, int *defused)
{
    reverse_mapping *rmap = get_reverse_mapping(codepage);

    if (rmap) {
        /* Do this by array lookup if we can. */
        if (wclen < 0) {
            for (wclen = 0; wcstr[wclen++] ;);   /* will include the NUL */
        }
        char *p;
        int i;
        for (p = mbstr, i = 0; i < wclen; i++) {
            wchar_t ch = wcstr[i];
            int by;
            const char *p1;

            #define WRITECH(chr) do             \
            {                                   \
                assert(p - mbstr < mblen);      \
                *p++ = (char)(chr);             \
            } while (0)

            if ((p1 = rmap->blocks[(ch >> 8) & 0xFF]) != NULL &&
                (by = p1[ch & 0xFF]) != '\0')
                WRITECH(by);
            else if (ch < 0x80)
                WRITECH(ch);
            else if (defchr) {
                *defused = 1;
                for (const char *q = defchr; *q; q++)
                    WRITECH(*q);
            }
#if 1
            else {
                *defused = 1;
                WRITECH('.');
            }
#endif

            #undef WRITECH
        }
        return p - mbstr;
    } else {
        int ret;
        ret = WideCharToMultiByte(codepage, flags, wcstr, wclen,
                                  mbstr, mblen, defchr, defused);
        if (ret)
            return ret;

        *defused = 0;

#ifdef LEGACY_WINDOWS
        /*
         * Fallback for legacy platforms too old to support UTF-8: if
         * the codepage is UTF-8, we can do the translation ourselves.
         */
        if (codepage == CP_UTF8 && mblen > 0 && wclen > 0) {
            size_t remaining = mblen;
            char *p = mbstr;

            while (wclen > 0) {
                unsigned long wc = (wclen--, *wcstr++);
                if (wclen > 0 && IS_SURROGATE_PAIR(wc, *wcstr)) {
                    wc = FROM_SURROGATES(wc, *wcstr);
                    wclen--, wcstr++;
                }

                char utfbuf[6];
                size_t utflen = encode_utf8(utfbuf, wc);
                if (utflen <= remaining) {
                    memcpy(p, utfbuf, utflen);
                    p += utflen;
                    remaining -= utflen;
                } else {
                    return p - mbstr;
                }
            }

            return p - mbstr;
        }
#endif

        /* No other fallbacks are available */
        return 0;
    }
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
