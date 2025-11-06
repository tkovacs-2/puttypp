#ifndef SFTPARGS_H
#define SFTPARGS_H

#include <stdbool.h>

typedef struct SftpArgs SftpArgs;
struct SftpArgs {
    const char * const *argv;
    int argc;
    char *cmdline;
};

void sftpargs_parse(char *cmdline, SftpArgs *args, bool completion);
void sftpargs_free(SftpArgs *args);

#endif
