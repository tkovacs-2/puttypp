#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"

typedef struct {
    SftpCmd cmd;
    const char *dir;
    int current_arg;
} SftpCmdMkdir;

static void send_mkdir(SftpCmdMkdir *cmdmkdir, Sftp *sftp)
{
    cmdmkdir->dir = sftp_get_absolute_path(sftp->pwd, sftp->args.argv[cmdmkdir->current_arg]);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmkdir->cmd, SSH_FXP_MKDIR, fxp_mkdir_send(cmdmkdir->dir, NULL));
}

static SftpCmd *sftpcmdmkdir_init(Sftp *sftp)
{
    if (sftp->args.argc < 2) {
        sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "mkdir: expects a directory");
        return NULL;
    }

    SftpCmdMkdir *cmdmkdir = snew(SftpCmdMkdir);
    cmdmkdir->dir = NULL;
    cmdmkdir->current_arg = 1;
    sftpcmd_clear_request(&cmdmkdir->cmd);
    send_mkdir(cmdmkdir, sftp);
    return &cmdmkdir->cmd;
}

static bool sftpcmdmkdir_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdMkdir *cmdmkdir = container_of(cmd, SftpCmdMkdir, cmd);
    bool result = fxp_mkdir_recv(pktin, cmd->req);

    if (result) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "mkdir %s: OK", cmdmkdir->dir);
    } else {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "mkdir %s: %s", cmdmkdir->dir, fxp_error());
    }
    sfree((void *)cmdmkdir->dir);
    cmdmkdir->dir = NULL;

    cmdmkdir->current_arg++;
    if (cmdmkdir->current_arg == sftp->args.argc) {
        return false;
    }
    send_mkdir(cmdmkdir, sftp);
    return true;
}

static void sftpcmdmkdir_free(SftpCmd *cmd)
{
    SftpCmdMkdir *cmdmkdir = container_of(cmd, SftpCmdMkdir, cmd);
    sfree((void *)cmdmkdir->dir);
    sfree(cmdmkdir);
}

static SftpCmdArgInfo sftpcmdmkdir_get_arg_info(int file_arg_index)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, true, true};
}

const SftpCmdVtable sftpcmdmkdir_vt = {
    .init = sftpcmdmkdir_init,
    .free = sftpcmdmkdir_free,
    .process_pkt = sftpcmdmkdir_process_pkt,
    .get_arg_info = sftpcmdmkdir_get_arg_info
};
