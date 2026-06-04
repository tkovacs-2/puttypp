#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "psftp.h"

typedef struct {
    SftpCmd cmd;
    const char *dir;
    const char *wildcard;
    const char *cdir;
    struct fxp_handle *dirh;
    struct list_directory_from_sftp_ctx *ctx;
} SftpCmdLs;

static void send_close(Sftp *sftp, SftpCmd *cmd, struct fxp_handle *dirh)
{
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_CLOSE, fxp_close_send(dirh));
}

static Seat *list_directory_from_sftp_seat = NULL;

void list_directory_from_sftp_warn_unsorted(void)
{
    sftp_print(list_directory_from_sftp_seat, SEAT_OUTPUT_STDERR, "Directory is too large to sort; writing file names unsorted");
}

void list_directory_from_sftp_print(struct fxp_name *name)
{
    sftp_print(list_directory_from_sftp_seat, SEAT_OUTPUT_STDOUT, name->longname);
}

static SftpCmd *sftpcmdls_init(Sftp *sftp)
{
    const char *dir;
    char *unwcdir, *wildcard;
    int i = 1;

    while (i < sftp->args.argc && sftp->args.argv[i][0] == '-') {
        if (strcmp(sftp->args.argv[i], "--") == 0) {
            i++;
            break;
        }
        if (sftp->args.argv[i][1] == 'l') {
            i++;
            continue;
        }
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: unrecognised option '%s'", sftp->args.argv[0], sftp->args.argv[i]);
        return NULL;
    }

    if (i >= sftp->args.argc)
        dir = ".";
    else
        dir = sftp->args.argv[i];

    unwcdir = snewn(1 + strlen(dir), char);
    if (wc_unescape(unwcdir, dir)) {
        dir = unwcdir;
        wildcard = NULL;
    } else {
        char *tmpdir;
        int len;
        bool check;

        sfree(unwcdir);
        wildcard = stripslashes(dir, false);
        unwcdir = dupstr(dir);
        len = wildcard - dir;
        unwcdir[len] = '\0';
        if (len > 0 && unwcdir[len-1] == '/')
            unwcdir[len-1] = '\0';
        tmpdir = snewn(1 + len, char);
        check = wc_unescape(tmpdir, unwcdir);
        sfree(tmpdir);
        if (!check) {
            sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "Multiple-level wildcards are not supported");
            sfree(unwcdir);
            return NULL;
        }
        dir = unwcdir;
    }


    SftpCmdLs *cmdls = snew(SftpCmdLs);
    cmdls->dir = sftp_get_absolute_path(sftp->pwd, dir);
    sfree((void *)dir);
    cmdls->wildcard = wildcard;
    cmdls->cdir = NULL;
    cmdls->dirh = NULL;
    cmdls->ctx = NULL;

    sftpcmd_clear_request(&cmdls->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdls->cmd, SSH_FXP_REALPATH, fxp_realpath_send(cmdls->dir));
    return &cmdls->cmd;
}

static bool sftpcmdls_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdLs *cmdls = container_of(cmd, SftpCmdLs, cmd);

    if (cmd->req_type == SSH_FXP_REALPATH) {
        cmdls->cdir = fxp_realpath_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!cmdls->cdir) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "ls: unable to open %s: %s", cmdls->dir, fxp_error());
            return false;
        }
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_OPENDIR, fxp_opendir_send(cmdls->cdir));
        return true;
    } else if (cmd->req_type == SSH_FXP_OPENDIR) {
        cmdls->dirh = fxp_opendir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (cmdls->dirh == NULL) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "ls: unable to open %s: %s", cmdls->cdir, fxp_error());
            return false;
        }
        cmdls->ctx = list_directory_from_sftp_new();
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdls->dirh));
        return true;
    } else if (cmd->req_type == SSH_FXP_READDIR) {
        struct fxp_names *names = fxp_readdir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);

        if (names == NULL) {
            if (fxp_error_type() == SSH_FX_EOF) {
                send_close(sftp, cmd, cmdls->dirh);
                return true;
            }
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "ls: reading directory %s: %s", cmdls->cdir, fxp_error());
            send_close(sftp, cmd, cmdls->dirh);
            return true;
        }
        if (names->nnames == 0) {
            fxp_free_names(names);
            send_close(sftp, cmd, cmdls->dirh);
            return true;
        }

        list_directory_from_sftp_seat = sftp->seat;
        for (size_t i = 0; i < names->nnames; i++) {
            if (!cmdls->wildcard || wc_match(cmdls->wildcard, names->names[i].filename)) {
                list_directory_from_sftp_feed(cmdls->ctx, &names->names[i]);
            }
        }
        fxp_free_names(names);
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdls->dirh));
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        cmdls->dirh = NULL;
        list_directory_from_sftp_finish(cmdls->ctx);
    }
    return false;
}

static void sftpcmdls_free(SftpCmd *cmd)
{
    SftpCmdLs *cmdls = container_of(cmd, SftpCmdLs, cmd);
    sfree((void *)cmdls->dir);
    sfree((void *)cmdls->cdir);
    if (cmdls->dirh) {
        sftp_free_fxphandle(cmdls->dirh);
    }
    if (cmdls->ctx) {
        list_directory_from_sftp_free(cmdls->ctx);
    }
    sfree(cmdls);
}

static SftpCmdArgInfo sftpcmdls_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, true, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

const SftpCmdVtable sftpcmdls_vt = {
    .init = sftpcmdls_init,
    .free = sftpcmdls_free,
    .process_pkt = sftpcmdls_process_pkt,
    .get_arg_info = sftpcmdls_get_arg_info
};
