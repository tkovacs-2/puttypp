#ifndef SFTPUTIL_H
#define SFTPUTIL_H

typedef struct Seat Seat;
typedef enum SeatOutputType SeatOutputType;

void sftp_print(Seat *seat, SeatOutputType type, const char *text);
void sftp_printf(Seat *seat, SeatOutputType type, const char *format, ...);
void sftp_print_pwd(Seat *seat, const char *pwd);

const char *sftp_get_absolute_path(const char *pwd, const char *name);

struct fxp_handle;
void sftp_free_fxphandle(struct fxp_handle *handle);

int sftp_decode_codepage(const char *name, Seat *seat);

#endif
