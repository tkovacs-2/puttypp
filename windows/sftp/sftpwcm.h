#ifndef SFTPWCM_H
#define SFTPWCM_H

#include <stdbool.h>

typedef struct Sftp Sftp;
typedef struct SftpCmd SftpCmd;
typedef struct SftpWildcardMatcher SftpWildcardMatcher;
typedef struct SftpWildcardArgs SftpWildcardArgs;
struct sftp_packet;

SftpWildcardArgs *sftpwcm_args_create(Sftp *sftp, int begin_arg, int end_arg, bool disable_wc);
void sftpwcm_args_free(SftpWildcardArgs *args);

typedef struct SftpWildcardMatcherIterator {
    SftpWildcardArgs *args;
    int current_arg;
    SftpWildcardMatcher *swcm;
    const char *cname; //line codepage
    void (*func)(const char *, Sftp *, SftpCmd *);
} SftpWildcardMatcherIterator;

bool sftpwcm_iterator_next(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd);
bool sftpwcm_iterator_pktin(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd, struct sftp_packet *pktin);
void sftpwcm_iterator_init(SftpWildcardMatcherIterator* it, SftpWildcardArgs *args, void (*func)(const char *, Sftp *, SftpCmd *));
void sftpwcm_iterator_uninit(SftpWildcardMatcherIterator* it);

#endif
