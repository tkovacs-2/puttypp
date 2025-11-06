#ifndef SFTPFXP_H
#define SFTPFXP_H

typedef struct Sftp Sftp;

void sftp_set_sending_backend(Sftp *sftp);

void sftp_init_requests(Sftp *sftp);
void sftp_uninit_requests(Sftp *sftp);
void sftp_free_pending_requests(Sftp *sftp);

#include "ssh/sftp.h"

#endif
