#include "sftpprogressbar.h"
#include "putty.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define SPEED_WINDOW_TICKS 250

static int format_speed(char *buf, size_t buflen, uint64_t bps)
{
    if (bps >= 1073741824ULL) {
        return snprintf(buf, buflen, "%.2f GiB/s", bps / 1073741824.0);
    } else if (bps >= 1048576ULL) {
        return snprintf(buf, buflen, "%.2f MiB/s", bps / 1048576.0);
    } else if (bps >= 1024ULL) {
        return snprintf(buf, buflen, "%.1f KiB/s", bps / 1024.0);
    } else {
        return snprintf(buf, buflen, "%"PRIu64" B/s", bps);
    }
}

static int format_eta(char *buf, size_t buflen, uint64_t bytes, uint64_t bps)
{
    if (bps == 0) {
        return snprintf(buf, buflen, "--:--");
    }
    uint64_t eta = bytes/bps;
    if (eta >= 3600ULL * 99) {
        return snprintf(buf, buflen, "%"PRIu64"h+", eta / 3600);
    } else if (eta >= 3600ULL) {
        uint64_t h = eta / 3600;
        unsigned m = (unsigned)((eta % 3600) / 60);
        unsigned s = (unsigned)(eta % 60);
        return snprintf(buf, buflen, "%"PRIu64":%02u:%02u", h, m, s);
    } else {
        unsigned m = (unsigned)(eta / 60);
        unsigned s = (unsigned)(eta % 60);
        return snprintf(buf, buflen, "%u:%02u", m, s);
    }
}

void sftpprogressbar_init(SftpProgressBar *pb, uint64_t start_offset, uint64_t total_size)
{
    if (total_size > 0 && start_offset > total_size) {
        start_offset = total_size;
    }
    pb->got = start_offset;
    pb->goal = total_size;
    pb->window_start_got = start_offset;
    pb->window_start_tick = GETTICKCOUNT();
    pb->bytes_per_sec = 0;
    pb->drawn = false;
    pb->finished = false;
}

void sftpprogressbar_update(SftpProgressBar *pb, uint64_t got)
{
    pb->got += got;
    if (pb->goal > 0 && pb->got > pb->goal) {
        pb->got = pb->goal;
    }
}

void sftpprogressbar_draw(SftpProgressBar *pb, Seat *seat, int term_cols)
{
    if (pb->finished || term_cols < 6) {
        return;
    }

    static char line[128];
    char *line_end = line + sizeof(line);

    uint64_t now = GETTICKCOUNT();
    uint64_t elapsed = now-pb->window_start_tick;

    if (elapsed >= SPEED_WINDOW_TICKS) {
        pb->bytes_per_sec = (pb->got - pb->window_start_got) * 1000 / elapsed;
        pb->window_start_tick = now;
        pb->window_start_got = pb->got;
    }

    char *p = line;
    *p++ = '\r';

    if (pb->goal > 0) {
        int bar_size = term_cols-52;
        if (bar_size >= 6) {
            if (bar_size > 28) {
                bar_size = 28;
            }
            int filled = (int)((pb->got * bar_size) / pb->goal);
            *p++ = '[';
            int i = 0;
            while (i < filled) {
                *p++ = '=';
                i++;
            }
            while (i < bar_size) {
                *p++ = '.';
                i++;
            }
            *p++ = ']';
        }

        int percent = (int)((pb->got*100)/pb->goal);
        p += snprintf(p, line_end-p, " %3d%%", percent);
        p += snprintf(p, line_end-p, "  %"PRIu64"/%"PRIu64"  ", pb->got, pb->goal);
        p += format_speed(p, line_end-p, pb->bytes_per_sec);
        p += snprintf(p, line_end-p, "  ETA ");
        p += format_eta(p, line_end-p, (pb->goal - pb->got), pb->bytes_per_sec);
    } else {
        p += snprintf(p, line_end-p, "%"PRIu64" bytes  ", pb->got);
        p += format_speed(p, line_end-p, pb->bytes_per_sec);
    }
    int len = p-line;
    if (len > term_cols) {
        len = term_cols;
    }
    seat_output(seat, SEAT_OUTPUT_STDOUT, line, len);
    seat_output(seat, SEAT_OUTPUT_STDOUT, "\x1b[0K", 4);
    pb->drawn |= true;
    return;
}

void sftpprogressbar_finish(SftpProgressBar *pb, Seat *seat)
{
    if (pb->drawn) {
        seat_output(seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
        pb->drawn = false;
    }
    pb->finished = true;
}
