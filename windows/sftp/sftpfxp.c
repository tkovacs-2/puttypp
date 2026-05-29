#include "ssh/sftp.c"

#include "sftpbe.h"

static Backend *sending_backend = NULL;

bool sftp_recvdata(char *buf, size_t len)
{
    return false;
}

bool sftp_senddata(const char *buf, size_t len)
{
    backend_send(sending_backend, buf, len);
    return true;
}

size_t sftp_sendbuffer(void)
{
    return backend_sendbuffer(sending_backend);
}

void sftp_set_sending_backend(Sftp *sftp)
{
    sending_backend = sftp->ssh;
    sftp_requests = sftp->requests;
}

void sftp_clear_sending_backend()
{
    sending_backend = NULL;
}

void sftp_init_requests(Sftp *sftp)
{
    sftp->requests = newtree234(sftp_reqcmp);
}

void sftp_uninit_requests(Sftp *sftp)
{
    freetree234(sftp->requests);
}

void sftp_free_pending_requests(Sftp *sftp)
{
    void *req;
    while ((req = delpos234(sftp->requests, 0))) {
        sfree(req);
    }
}
