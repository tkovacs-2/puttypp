#ifndef SFTPCLI_H
#define SFTPCLI_H

#include "putty.h"

typedef struct SftpCli SftpCli;

typedef enum {
    SFTPCLISTATE_CONTINUE,
    SFTPCLISTATE_COMPLETION,
    SFTPCLISTATE_COMPLETION_AGAIN,
    SFTPCLISTATE_FINISH,
    SFTPCLISTATE_FINISH_CANCEL,
    SFTPCLISTATE_FINISH_EXIT
} SftpCliState;

SftpCli *sftpcli_create(Seat *seat);
void sftpcli_free(SftpCli *cli);

void sftpcli_start(SftpCli *cli, int columns, const char *lpwd, const char *pwd);
void sftpcli_change_columns(SftpCli *cli, int columns);
SftpCliState sftpcli_feed(SftpCli *cli, const char *buf, size_t len);
char *sftpcli_copy_line(SftpCli *cli, bool until_cursor);
size_t sftpcli_get_unprocessed_feed(SftpCli *cli);
void sftpcli_refresh(SftpCli *cli);
void sftpcli_start_quote(SftpCli *cli);

#endif
