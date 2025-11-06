#ifndef SFTPWCM_H
#define SFTPWCM_H

#include <stdbool.h>

typedef struct Sftp Sftp;
typedef struct SftpCmd SftpCmd;
typedef struct SftpWildcardMatcher SftpWildcardMatcher;
struct sftp_packet;

SftpWildcardMatcher *sftpwcm_begin(const char *name, Sftp *sftp, SftpCmd *cmd);
bool sftpwcm_realpath_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin);
bool sftpwcm_opendir_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin);

const char *sftpwcm_get_filename(SftpWildcardMatcher *swcm);
bool sftpwcm_readdir_recv(SftpWildcardMatcher *, struct sftp_packet *pktin);

void sftpwcm_finish(SftpWildcardMatcher *swcm);
void sftpwcm_close_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin);

void sftpwcm_free(SftpWildcardMatcher *swcm);

typedef struct SftpWildcardMatcherIterator {
    int current_arg;
    int end_arg;
    bool disable_wc;
    SftpWildcardMatcher *swcm;
    const char *cname;
    void (*func)(const char *, Sftp *, SftpCmd *);
} SftpWildcardMatcherIterator;

bool sftpwcm_iterator_next(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd);
bool sftpwcm_iterator_pktin(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd, struct sftp_packet *pktin);
void sftpwcm_iterator_init(SftpWildcardMatcherIterator* it, void (*func)(const char *, Sftp *, SftpCmd *));
void sftpwcm_iterator_uninit(SftpWildcardMatcherIterator* it);

#endif
