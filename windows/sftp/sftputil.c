#include "putty.h"
#include "ssh/sftp.h"

void sftp_print(Seat *seat, SeatOutputType type, const char *text)
{
    seat_output(seat, type, text, strlen(text));
    seat_output(seat, type, "\r\n", 2);
}

void sftp_printf(Seat *seat, SeatOutputType type, const char *format, ...)
{
    static char buffer[1024];
    va_list args;

    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    seat_output(seat, type, buffer, min((int)sizeof(buffer), length));
    va_end(args);
    seat_output(seat, type, "\r\n", 2);
}

void sftp_print_pwd(Seat *seat, const char *pwd)
{
    sftp_printf(seat, SEAT_OUTPUT_STDOUT, "remote directory is %s", pwd);
}

const char *sftp_get_absolute_path(const char *pwd, const char *name)
{
    char *fullname;
    if (*name == '/') {
        fullname = dupstr(name);
    } else {
        if (strcmp(pwd, "/") == 0) {
            fullname = dupcat(pwd, "", name);
        } else {
            fullname = dupcat(pwd, "/", name);
        }
    }
    return fullname;
}

void sftp_free_fxphandle(struct fxp_handle *handle) {
    sfree(handle->hstring);
    sfree(handle);
}
