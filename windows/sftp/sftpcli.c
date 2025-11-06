#include "sftpcli.h"
#include "linenoise/linenoise.h"
#include <string.h>

struct SftpCli {
    Seat *seat;
    struct linenoiseState ls;
    linenoiseHistory history;
    const char *buf;
    size_t len;
};

static ssize_t cli_read(void *buffer, size_t count, void *ctx) {
    SftpCli *cli = (SftpCli *)ctx;
    if (cli->len == 0) {
        return -1;
    }
    size_t l = min(cli->len, count);
    memcpy(buffer, cli->buf, l);
    cli->buf += l;
    cli->len -= l;
    return l;
}

static ssize_t cli_write(const void *buffer, size_t count, void *ctx) {
    SftpCli *cli = (SftpCli *)ctx;
    seat_output(cli->seat, SEAT_OUTPUT_STDOUT, buffer, count);
    return count;
}

SftpCli *sftpcli_create(Seat *seat) {
    SftpCli *cli = snew(SftpCli);
    memset(cli, 0, sizeof(SftpCli));
    cli->seat = seat;
    linenoiseInitHistory(&cli->history);
    return cli;
}

void sftpcli_free(SftpCli *cli) {
    linenoiseFreeHistory(&cli->history);
    sfree(cli);
}

const char *truncate_path(const char *name);

static bool pwd_truncate_local(const char *p, size_t length, size_t *offset)
{
    assert(*offset < length);
    const char *s = truncate_path(p+*offset);
    if (*s == 0) {
        return false;
    }
    *offset = s - p;
    return true;
}

static bool pwd_truncate(const char *p, size_t length, size_t *offset)
{
    size_t s = *offset;
    while (s < length && p[s] == '/') {
        s++;
    }
    while (s < length && p[s] != '/') {
        s++;
    }
    if (s == length) {
        return false;
    }
    *offset = s;
    return true;
}

static size_t pwd_get_columns(size_t length, size_t offset)
{
    return offset ? 1 + (length - offset) : length;
}

static void pwd_write(const char *p, size_t length, size_t offset, SftpCli *cli)
{
    if (!offset) {
        cli_write(p, length, cli);
        return;
    }
    cli_write("\xe2\x80\xa6", 3, cli);
    cli_write(p+offset, length-offset, cli);
}

static bool pwd_pick_layout(size_t inner,
                            size_t L, size_t *loff, size_t lnext,
                            size_t R, size_t *roff, size_t rnext)
{
    size_t lsize = pwd_get_columns(L, *loff);
    size_t rsize = pwd_get_columns(R, *roff);
    size_t lnextsize = pwd_get_columns(L, lnext);
    size_t rnextsize = pwd_get_columns(R, rnext);

    if (lnextsize >= rnextsize) {
        if (lnextsize + rsize <= inner) {
            *loff = lnext;
            return true;
        }
        if (rnextsize + lsize <= inner) {
            *roff = rnext;
            return true;
        }
    } else {
        if (rnextsize + lsize <= inner) {
            *roff = rnext;
            return true;
        }
        if (lnextsize + rsize <= inner) {
            *loff = lnext;
            return true;
        }
    }
    *loff = lnext;
    *roff = rnext;
    if (lnextsize + rnextsize <= inner) {
        return true;
    }
    return false;
}

static void pwd_write_line(SftpCli *cli, int columns, const char *lpwd, const char *pwd)
{
    size_t L = strlen(lpwd);
    size_t R = strlen(pwd);

    if (columns < 6 || L == 0 || R == 0) {
        return;
    }
    size_t inner = (size_t)columns - 6u;
    size_t loffset = 0, roffset = 0;
    size_t lnext = 0, rnext = 0;
    bool canl = true, canr = true;

    for (;;) {
        if (pwd_pick_layout(inner, L, &loffset, lnext, R, &roffset, rnext)) {
            cli_write("\x1b[33m[", 6, cli);
            pwd_write(lpwd, L, loffset, cli);
            cli_write(" <> ", 4, cli);
            pwd_write(pwd, R, roffset, cli);
            cli_write("]\x1b[0m\r\n", 7, cli);
            return;
        }
        if (canl) {
            canl = pwd_truncate_local(lpwd, L, &lnext);
        }
        if (canr) {
            canr = pwd_truncate(pwd, R, &rnext);
        }
        if (!canl && !canr) {
            return;
        }
    }
}

void sftpcli_start(SftpCli *cli, int columns,
                   const char *lpwd, const char *pwd) {
    pwd_write_line(cli, columns, lpwd, pwd);
    linenoiseEditStart(&cli->ls, cli_read, cli_write, cli, "sftp> ", columns, &cli->history);
}

void sftpcli_change_columns(SftpCli *cli, int columns) {
    linenoiseChangeColumns(&cli->ls, columns);
}

SftpCliState sftpcli_feed(SftpCli *cli, const char *buf, size_t len) {
    cli->buf = buf;
    cli->len = len;
    while (cli->len > 0) {
        char *line = linenoiseEditFeed(&cli->ls);
        if (line == NULL) {
            return SFTPCLISTATE_FINISH_CANCEL;
        }
        if (line == linenoiseExit) {
            return SFTPCLISTATE_FINISH_EXIT;
        }
        if (line == linenoiseCompletion) {
            return SFTPCLISTATE_COMPLETION;
        }
        if (line == linenoiseCompletionAgain) {
            return SFTPCLISTATE_COMPLETION_AGAIN;
        }
        if (line == linenoiseEditMore) {
            continue;
        }
        linenoiseHistoryAdd(&cli->history, line);
        linenoiseFree(line);
        return SFTPCLISTATE_FINISH;
    }
    return SFTPCLISTATE_CONTINUE;
}

char *sftpcli_copy_line(SftpCli *cli, bool until_cursor) {
    size_t len = until_cursor ? cli->ls.pos : cli->ls.len;
    char *line = snewn(len + 1, char);
    memcpy(line, cli->ls.buf, len);
    line[len] = 0;
    return line;
}

size_t sftpcli_get_unprocessed_feed(SftpCli *cli) {
    return cli->len;
}

void sftpcli_refresh(SftpCli *cli) {
    linenoiseShow(&cli->ls);
}
