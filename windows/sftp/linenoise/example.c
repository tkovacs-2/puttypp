#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <ctype.h>

#include "linenoise.h"

static struct termios orig_termios; /* In order to restore at exit.*/
static int rawmode = 0; /* For atexit() function to check if restore is needed*/

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
    struct termios raw;

    if (!isatty(fd)) goto fatal;
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
        rawmode = 0;
}

static int getColumns() {
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        return 80;
    } else {
        return ws.ws_col;
    }
}

static void linenoisePrintKeyCodes(void) {
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
            "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if (enableRawMode(STDIN_FILENO) == -1) return;
    memset(quit,' ',4);
    while(1) {
        char c;
        int nread;

        nread = read(STDIN_FILENO,&c,1);
        if (nread <= 0) continue;
        memmove(quit,quit+1,sizeof(quit)-1); /* shift string to left. */
        quit[sizeof(quit)-1] = c; /* Insert current char on the right. */
        if (memcmp(quit,"quit",sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n",
            isprint(c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    disableRawMode(STDIN_FILENO);
}

ssize_t stdinRead(void *buffer, size_t count, void *) {
    return read(STDIN_FILENO, buffer, count);
}

ssize_t stdoutWrite(const void *buffer, size_t count, void *) {
    return write(STDOUT_FILENO, buffer, count);
}

int sigwinch = 0;

void sigwinchHandler(int sig) {
    sigwinch = 1;
}

void setSigwinch() {
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_handler = sigwinchHandler;
  act.sa_flags = SA_RESTART;
  sigemptyset(&act.sa_mask);
  sigaction(SIGWINCH, &act, NULL);
}

int main(int argc, char **argv) {
    char *line;
    char *prgname = argv[0];
    int async = 0;

    /* Parse options, with --multiline we enable multi line editing. */
    while(argc > 1) {
        argc--;
        argv++;
        if (!strcmp(*argv,"--keycodes")) {
            linenoisePrintKeyCodes();
            exit(0);
        } else {
            fprintf(stderr, "Usage: %s [--keycodes]\n", prgname);
            exit(1);
        }
    }

    setSigwinch();

    /* Now this is the main loop of the typical linenoise-based application.
     * The call to linenoise() will block as long as the user types something
     * and presses enter.
     *
     * The typed string is returned as a malloc() allocated string by
     * linenoise, so the user needs to free() it. */

    linenoiseHistory history;
    linenoiseInitHistory(&history);

    while(1) {
        /* Asynchronous mode using the multiplexing API: wait for
         * data on stdin, and simulate async data coming from some source
         * using the select(2) timeout. */
        struct linenoiseState ls;
        enableRawMode(STDIN_FILENO);
        linenoiseEditStart(&ls,stdinRead,stdoutWrite,NULL,"hello> ", getColumns(STDOUT_FILENO), &history);
        while(1) {
            fd_set readfds;
            int retval;

            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);

            retval = select(STDIN_FILENO+1, &readfds, NULL, NULL, NULL);
            if (retval == -1) {
                if (errno == EINTR) {
                  if (sigwinch) {
                      sigwinch = 0;
                      linenoiseChangeColumns(&ls, getColumns(STDOUT_FILENO));
                  }
                } else {
                    perror("select()");
                    disableRawMode(STDIN_FILENO);
                    exit(1);
                }
            } else if (retval) {
                line = linenoiseEditFeed(&ls);
                /* A NULL return means: line editing is continuing.
                 * Otherwise the user hit enter or stopped editing
                 * (CTRL+C/D). */
                if (line != linenoiseEditMore) break;
            }
        }
        disableRawMode(STDIN_FILENO);
        if (line == NULL) {
            linenoiseFreeHistory(&history);
            exit(0); /* Ctrl+D/C. */
        }

        printf("\n");
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            linenoiseHistoryAdd(&history, line); /* Add to the history. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(&history, len);
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        linenoiseFree(line);
    }
    return 0;
}
