#ifndef SFTPPROGRESSBAR_H
#define SFTPROGRESSBAR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Seat Seat;

typedef struct SftpProgressBar {
    uint64_t got;
    uint64_t goal;
    uint64_t window_start_got;
    uint64_t window_start_tick;
    uint64_t bytes_per_sec;
    bool drawn;
    bool finished;
} SftpProgressBar;

void sftpprogressbar_init(SftpProgressBar *pb, uint64_t start_offset, uint64_t total_size);
void sftpprogressbar_update(SftpProgressBar *pb, uint64_t got);
void sftpprogressbar_draw(SftpProgressBar *pb, Seat *seat, int term_cols);
void sftpprogressbar_finish(SftpProgressBar *pb, Seat *seat);

#endif
