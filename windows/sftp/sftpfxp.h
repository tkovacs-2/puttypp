#ifndef SFTPFXP_H
#define SFTPFXP_H

typedef struct Sftp Sftp;
struct fxp_xfer;

void sftp_set_sending_backend(Sftp *sftp);

void sftp_init_requests(Sftp *sftp);
void sftp_uninit_requests(Sftp *sftp);
void sftp_free_pending_requests(Sftp *sftp);
bool xfer_download_data_wrapper(struct fxp_xfer *xfer, void **buf, int *len);

#include "ssh/sftp.h"

#endif
