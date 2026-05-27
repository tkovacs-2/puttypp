#ifndef SFTPCMD_UNICODE_H
#define SFTPCMD_UNICODE_H

const char *sftp_dup_utf8_to_line(int line_codepage, const char *utf8, Seat *seat);
const char *sftp_dup_utf8_from_line(int line_codepage, const char *s);
void sftp_dup_utf8_free(const char *dup, const char *orig);
const char *sftp_utf8_to_line(int line_codepage, const char *utf8, Seat *seat);
const char *sftp_utf8_from_line(int line_codepage, const char *s);

#define sftp_line_printf(SFTP, SEAT_OUTPUT, LINE_ARG, FORMAT, ...) \
    const char *utf8_arg = sftp_dup_utf8_from_line(SFTP->line_codepage, LINE_ARG); \
    sftp_printf(SFTP->seat, SEAT_OUTPUT, FORMAT, __VA_ARGS__); \
    sftp_dup_utf8_free(utf8_arg, LINE_ARG);

#endif
