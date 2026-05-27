#ifndef GETPUT_H
#define GETPUT_H

#include <stddef.h>
#include <stdbool.h>

typedef struct Sftp Sftp;

typedef struct SftpDir {
    const char *fname; //utf8
    const char *line_fname; //line codepage
    const char *outfname; //utf8
    size_t nnames, namesize;
    const char **ournames; //line codepage for get, utf8 for put
    size_t i;
} SftpDir;

typedef struct SftpDirStack {
    size_t top;
    size_t capacity;
    SftpDir *stack;
} SftpDirStack;

SftpDir *sftpdirstack_top(SftpDirStack *dirstack);
SftpDir *sftpdirstack_pop(SftpDirStack *dirstack);
SftpDir *sftpdirstack_push(SftpDirStack *dirstack);

void sftpdirstack_init(SftpDirStack *dirstack);
void sftpdirstack_uninit(SftpDirStack *dirstack);

bool getput_parse_args(Sftp *sftp, int *i, bool *recurse);
void getput_sort_dir_names(SftpDir *dir);

#endif
