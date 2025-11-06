#ifndef SFTPCOMPLETION_H
#define SFTPCOMPLETION_H

#include <stdbool.h>

typedef struct SftpCompletion SftpCompletion;
typedef struct Sftp Sftp;
typedef struct SftpCmdVtable SftpCmdVtable;

typedef struct SftpCompletionName {
    const char *name;
    bool is_dir;
} SftpCompletionName;

SftpCompletion *sftpcompletion_create(Sftp *sftp);
void sftpcompletion_free(SftpCompletion *completion);
const SftpCmdVtable *sftpcompletion_start_completion(SftpCompletion *completion);
void sftpcompletion_continue_completion(SftpCompletion *completion, const SftpCompletionName *names, size_t nnames);
const char *sftpcompletion_get_remote_path(SftpCompletion *completion);

void sftpcompletion_continue_paging(SftpCompletion *completion, int max_lines);
void sftpcompletion_cancel_paging(SftpCompletion *completion);
bool sftpcompletion_is_paging(SftpCompletion *completion);
bool sftpcompletion_is_paging_displayed(SftpCompletion *completion);
#endif
