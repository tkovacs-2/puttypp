#include "sftpcmd.h"
#include "sftpbe.h"
#include "sftpfxp.h"
#include "psftp.h"

void sftpcmd_print(Seat *seat, SeatOutputType type, const char *text)
{
    seat_output(seat, type, text, strlen(text));
    seat_output(seat, type, "\r\n", 2);
}

void sftpcmd_printf(Seat *seat, SeatOutputType type, const char *format, ...)
{
    static char buffer[1024];
    va_list args;

    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    seat_output(seat, type, buffer, min((int)sizeof(buffer), length));
    va_end(args);
    seat_output(seat, type, "\r\n", 2);
}

void sftpcmd_print_pwd(Seat *seat, const char *pwd)
{
    sftpcmd_printf(seat, SEAT_OUTPUT_STDOUT, "remote directory is %s", pwd);
}

const char *sftpcmd_get_absolute_path(const char *pwd, const char *name)
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

void sftpcmd_set_request(SftpCmd *cmd, int req_type, struct sftp_request *req)
{
    assert(!cmd->req);
    if (req) {
        sftp_register(req);
    }
    cmd->req = req;
    cmd->req_type = req_type;
}

void sftpcmd_clear_request(SftpCmd *cmd)
{
    cmd->req = NULL;
    cmd->req_type = 0;
}

static void print_lpwd(Seat *seat, const char *lpwd)
{
    sftpcmd_printf(seat, SEAT_OUTPUT_STDOUT, "local directory is %s", lpwd);
}

static SftpCmd *sftpcmdbye_init(Sftp *sftp)
{
    seat_notify_remote_exit(sftp->seat);
    return NULL;
}

static SftpCmd *sftpcmdlcd_init(Sftp *sftp)
{
    if (sftp->args.argc < 2) {
        sftpcmd_print(sftp->seat, SEAT_OUTPUT_STDERR, "lcd: expects a local directory name");
        return NULL;
    }

    char *oldcwd = psftp_getcwd();

    char *errmsg = psftp_lcd((char *)sftp->lpwd);
    if (errmsg == NULL) {
        errmsg = psftp_lcd((char *)sftp->args.argv[1]);
    }
    if (errmsg) {
        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "lcd: unable to change directory: %s", errmsg);
        sfree(errmsg);
    } else {
        sfree((void *)sftp->lpwd);
        sftp->lpwd = psftp_getcwd();
        print_lpwd(sftp->seat, sftp->lpwd);
    }

    errmsg = psftp_lcd(oldcwd);
    if (errmsg) {
        sfree(errmsg);
    }
    sfree(oldcwd);
    return NULL;
}

static SftpCmdArgInfo sftpcmdlcd_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_LOCAL, true, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

static SftpCmd *sftpcmdlpwd_init(Sftp *sftp)
{
    print_lpwd(sftp->seat, sftp->lpwd);
    return NULL;
}

static SftpCmd *sftpcmdpwd_init(Sftp *sftp)
{
    sftpcmd_print_pwd(sftp->seat, sftp->pwd);
    return NULL;
}

static SftpCmdArgInfo sftpcmdhelp_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_COMMAND, false, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

extern const SftpCmdVtable sftpcmdcd_vt;
extern const SftpCmdVtable sftpcmdchmod_vt;
extern const SftpCmdVtable sftpcmdget_vt;
extern const SftpCmdVtable sftpcmdinit_vt;
extern const SftpCmdVtable sftpcmdls_vt;
extern const SftpCmdVtable sftpcmdmget_vt;
extern const SftpCmdVtable sftpcmdmkdir_vt;
extern const SftpCmdVtable sftpcmdmput_vt;
extern const SftpCmdVtable sftpcmdmv_vt;
extern const SftpCmdVtable sftpcmdput_vt;
extern const SftpCmdVtable sftpcmdreget_vt;
extern const SftpCmdVtable sftpcmdreput_vt;
extern const SftpCmdVtable sftpcmdrm_vt;

static const SftpCmdVtable sftpcmdbye_vt = {
    .init = sftpcmdbye_init,
    .free = NULL,
    .process_pkt = NULL,
    .get_arg_info = sftpcmd_get_arg_info
};

static const SftpCmdVtable sftpcmdlcd_vt = {
    .init = sftpcmdlcd_init,
    .free = NULL,
    .process_pkt = NULL,
    .get_arg_info = sftpcmdlcd_get_arg_info
};

static const SftpCmdVtable sftpcmdlpwd_vt = {
    .init = sftpcmdlpwd_init,
    .free = NULL,
    .process_pkt = NULL,
    .get_arg_info = sftpcmd_get_arg_info
};

static const SftpCmdVtable sftpcmdpwd_vt = {
    .init = sftpcmdpwd_init,
    .free = NULL,
    .process_pkt = NULL,
    .get_arg_info = sftpcmd_get_arg_info
};

static SftpCmd *sftpcmdhelp_init(Sftp *sftp);
static const SftpCmdVtable sftpcmdhelp_vt = {
    .init = sftpcmdhelp_init,
    .free = NULL,
    .process_pkt = NULL,
    .get_arg_info = sftpcmdhelp_get_arg_info
};

struct SftpCmdLookup {
    const char *name;
    bool listed;
    const char *shorthelp;
    const char *longhelp;
    const SftpCmdVtable *vt;
} sftp_lookup[] = {
    {
        "bye", true, "finish your SFTP session",
            "\r\n"
            "  Terminates your SFTP session.",
            &sftpcmdbye_vt
    },
    {
        "cd", true, "change your remote working directory",
            " [ <new working directory> ]\r\n"
            "  Change the remote working directory for your SFTP session.\r\n"
            "  If a new working directory is not supplied, you will be\r\n"
            "  returned to your home directory.",
            &sftpcmdcd_vt
    },
    {
        "chmod", true, "change file permissions and modes",
            " <modes> <filename-or-wildcard> [ <filename-or-wildcard>... ]\r\n"
            "  Change the file permissions on one or more remote files or\r\n"
            "  directories.\r\n"
            "  <modes> can be any octal Unix permission specifier.\r\n"
            "  Alternatively, <modes> can include the following modifiers:\r\n"
            "    u+r     make file readable by owning user\r\n"
            "    u+w     make file writable by owning user\r\n"
            "    u+x     make file executable by owning user\r\n"
            "    u-r     make file not readable by owning user\r\n"
            "    [also u-w, u-x]\r\n"
            "    g+r     make file readable by members of owning group\r\n"
            "    [also g+w, g+x, g-r, g-w, g-x]\r\n"
            "    o+r     make file readable by all other users\r\n"
            "    [also o+w, o+x, o-r, o-w, o-x]\r\n"
            "    a+r     make file readable by absolutely everybody\r\n"
            "    [also a+w, a+x, a-r, a-w, a-x]\r\n"
            "    u+s     enable the Unix set-user-ID bit\r\n"
            "    u-s     disable the Unix set-user-ID bit\r\n"
            "    g+s     enable the Unix set-group-ID bit\r\n"
            "    g-s     disable the Unix set-group-ID bit\r\n"
            "    +t      enable the Unix \"sticky bit\"\r\n"
            "  You can give more than one modifier for the same user (\"g-rwx\"), and\r\n"
            "  more than one user for the same modifier (\"ug+w\"). You can\r\n"
            "  use commas to separate different modifiers (\"u+rwx,g+s\").",
            &sftpcmdchmod_vt
    },
    {
        "del", true, "delete files on the remote server",
            " <filename-or-wildcard> [ <filename-or-wildcard>... ]\r\n"
            "  Delete a file or files from the server.\r\n",
            &sftpcmdrm_vt
    },
    {
        "delete", false, "del", NULL, &sftpcmdrm_vt
    },
    {
        "dir", true, "list remote files",
            " [-l] [--] [ <directory-name> ]/[ <wildcard> ]\r\n"
            "  List the contents of a specified directory on the server.\r\n"
            "  If <directory-name> is not given, the current working directory\r\n"
            "  is assumed.\r\n"
            "  If <wildcard> is given, it is treated as a set of files to\r\n"
            "  list; otherwise, all files are listed.",
            &sftpcmdls_vt
    },
    {
        "exit", true, "bye", NULL, &sftpcmdbye_vt
    },
    {
        "get", true, "download a file from the server to your local machine",
            " [ -r ] [ -- ] <filename> [ <local-filename> ]\r\n"
            "  Downloads a file on the server and stores it locally under\r\n"
            "  the same name, or under a different one if you supply the\r\n"
            "  argument <local-filename>.\r\n"
            "  If -r specified, recursively fetch a directory.",
            &sftpcmdget_vt
    },
    {
        "help", true, "give help",
            " [ <command> [ <command> ... ] ]\r\n"
            "  Give general help if no commands are specified.\r\n"
            "  If one or more commands are specified, give specific help on\r\n"
            "  those particular commands.",
            &sftpcmdhelp_vt
    },
    {
        "lcd", true, "change local working directory",
            " <local-directory-name>\r\n"
            "  Change the local working directory of the PSFTP program (the\r\n"
            "  default location where the \"get\" command will save files).",
            &sftpcmdlcd_vt
    },
    {
        "lpwd", true, "print local working directory",
            "\r\n"
            "  Print the local working directory of the PSFTP program (the\r\n"
            "  default location where the \"get\" command will save files).",
            &sftpcmdlpwd_vt
    },
    {
        "ls", true, "dir", NULL, &sftpcmdls_vt
    },
    {
        "mget", true, "download multiple files at once",
            " [ -r ] [ -- ] <filename-or-wildcard> [ <filename-or-wildcard>... ]\r\n"
            "  Downloads many files from the server, storing each one under\r\n"
            "  the same name it has on the server side. You can use wildcards\r\n"
            "  such as \"*.c\" to specify lots of files at once.\r\n"
            "  If -r specified, recursively fetch files and directories.",
            &sftpcmdmget_vt
    },
    {
        "mkdir", true, "create directories on the remote server",
            " <directory-name> [ <directory-name>... ]\r\n"
            "  Creates directories with the given names on the server.",
            &sftpcmdmkdir_vt
    },
    {
        "mput", true, "upload multiple files at once",
            " [ -r ] [ -- ] <filename-or-wildcard> [ <filename-or-wildcard>... ]\r\n"
            "  Uploads many files to the server, storing each one under the\r\n"
            "  same name it has on the client side. You can use wildcards\r\n"
            "  such as \"*.c\" to specify lots of files at once.\r\n"
            "  If -r specified, recursively store files and directories.",
            &sftpcmdmput_vt
    },
    {
        "mv", true, "move or rename file(s) on the remote server",
            " <source> [ <source>... ] <destination>\r\n"
            "  Moves or renames <source>(s) on the server to <destination>,\r\n"
            "  also on the server.\r\n"
            "  If <destination> specifies an existing directory, then <source>\r\n"
            "  may be a wildcard, and multiple <source>s may be given; all\r\n"
            "  source files are moved into <destination>.\r\n"
            "  Otherwise, <source> must specify a single file, which is moved\r\n"
            "  or renamed so that it is accessible under the name <destination>.",
            &sftpcmdmv_vt
    },
    {
        "put", true, "upload a file from your local machine to the server",
            " [ -r ] [ -- ] <filename> [ <remote-filename> ]\r\n"
            "  Uploads a file to the server and stores it there under\r\n"
            "  the same name, or under a different one if you supply the\r\n"
            "  argument <remote-filename>.\r\n"
            "  If -r specified, recursively store a directory.",
            &sftpcmdput_vt
    },
    {
        "pwd", true, "print your remote working directory",
            "\r\n"
            "  Print the current remote working directory for your SFTP session.",
            &sftpcmdpwd_vt
    },
    {
        "quit", true, "bye", NULL, &sftpcmdbye_vt
    },
    {
        "reget", true, "continue downloading files",
            " [ -r ] [ -- ] <filename> [ <local-filename> ]\r\n"
            "  Works exactly like the \"get\" command, but the local file\r\n"
            "  must already exist. The download will begin at the end of the\r\n"
            "  file. This is for resuming a download that was interrupted.\r\n"
            "  If -r specified, resume interrupted \"get -r\".",
            &sftpcmdreget_vt
    },
    {
        "ren", true, "mv", NULL,
            &sftpcmdmv_vt
    },
    {
        "rename", false, "mv", NULL,
            &sftpcmdmv_vt
    },
    {
        "reput", true, "continue uploading files",
            " [ -r ] [ -- ] <filename> [ <remote-filename> ]\r\n"
            "  Works exactly like the \"put\" command, but the remote file\r\n"
            "  must already exist. The upload will begin at the end of the\r\n"
            "  file. This is for resuming an upload that was interrupted.\r\n"
            "  If -r specified, resume interrupted \"put -r\".",
            &sftpcmdreput_vt
    },
    {
        "rm", true, "del", NULL,
            &sftpcmdrm_vt
    },
    {
        "rmdir", true, "remove directories on the remote server",
            " <directory-name> [ <directory-name>... ]\r\n"
            "  Removes the directory with the given name on the server.\r\n"
            "  The directory will not be removed unless it is empty.\r\n"
            "  Wildcards may be used to specify multiple directories.",
            &sftpcmdrm_vt
    }
};

static const struct SftpCmdLookup *lookup_command(const char *name)
{
    int i, j, k, cmp;

    i = -1;
    j = lenof(sftp_lookup);
    while (j - i > 1) {
        k = (j + i) / 2;
        cmp = strcmp(name, sftp_lookup[k].name);
        if (cmp < 0)
            j = k;
        else if (cmp > 0)
            i = k;
        else {
            return &sftp_lookup[k];
        }
    }
    return NULL;
}

static SftpCmd *sftpcmdhelp_init(Sftp *sftp)
{
    int i;
    if (sftp->args.argc == 1) {
        int maxlen;
        maxlen = 0;
        for (i = 0; i < lenof(sftp_lookup); i++) {
            int len;
            if (!sftp_lookup[i].listed)
                continue;
            len = strlen(sftp_lookup[i].name);
            if (maxlen < len)
                maxlen = len;
        }
        char *t = snewn(maxlen+3, char);
        for (i = 0; i < lenof(sftp_lookup); i++) {
            const struct SftpCmdLookup *lookup;
            if (!sftp_lookup[i].listed)
                continue;
            lookup = &sftp_lookup[i];
            sprintf(t, "%-*s", maxlen+2, lookup->name);
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, t, maxlen+2);
            if (lookup->longhelp == NULL)
                lookup = lookup_command(lookup->shorthelp);
            sftpcmd_print(sftp->seat, SEAT_OUTPUT_STDOUT, lookup->shorthelp);
        }
        sfree(t);
    } else {
        for (i = 1; i < sftp->args.argc; i++) {
            const struct SftpCmdLookup *lookup;
            lookup = lookup_command(sftp->args.argv[i]);
            if (!lookup) {
                sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "help: %s: command not found", sftp->args.argv[i]);
            } else {
                seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, lookup->name, strlen(lookup->name));
                if (lookup->longhelp == NULL)
                    lookup = lookup_command(lookup->shorthelp);
                sftpcmd_print(sftp->seat, SEAT_OUTPUT_STDOUT, lookup->longhelp);
            }
        }
    }
    return NULL;
}

const SftpCmdVtable *sftpcmd_vt_from_name(const char *name) {
    const struct SftpCmdLookup *lookup = lookup_command(name);
    return (lookup ? lookup->vt : NULL);
}

void sftpcmd_free_fxphandle(struct fxp_handle *handle) {
    sfree(handle->hstring);
    sfree(handle);
}

size_t sftpcmd_get_command_count()
{
    return lenof(sftp_lookup);
}

const char *sftpcmd_get_command_name(size_t i)
{
    if (i >= lenof(sftp_lookup)) {
        return NULL;
    }
    return sftp_lookup[i].name;
}

SftpCmdArgInfo sftpcmd_get_arg_info(int)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_INVALID, false, false};
}
