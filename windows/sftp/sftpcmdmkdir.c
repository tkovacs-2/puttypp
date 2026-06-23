#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpunicode.h"

typedef struct {
    const char *dir; //utf8
    const char *line_dir; //line codepage
} SftpCmdMkdirArg;

typedef struct {
    SftpCmd cmd;
    int current_arg;
    int argc;
    SftpCmdMkdirArg argv[1];
} SftpCmdMkdir;

static void sftpcmdmkdir_free(SftpCmd *cmd);

static void send_mkdir(SftpCmdMkdir *cmdmkdir, Sftp *sftp)
{
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmkdir->cmd, SSH_FXP_MKDIR, fxp_mkdir_send(cmdmkdir->argv[cmdmkdir->current_arg].line_dir, NULL));
}

static SftpCmd *sftpcmdmkdir_init(Sftp *sftp)
{
    if (sftp->args.argc < 2) {
        sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "mkdir: expects a directory");
        return NULL;
    }

    SftpCmdMkdir *cmdmkdir = (SftpCmdMkdir *)snewn(sizeof(SftpCmdMkdir) + (sftp->args.argc-1) * sizeof(SftpCmdMkdirArg), char);
    cmdmkdir->argc = sftp->args.argc-1;
    cmdmkdir->current_arg = 0;

    bool result = true;
    for (int i = 0; i < cmdmkdir->argc; i++) {
        SftpCmdMkdirArg *arg = &cmdmkdir->argv[i];
        arg->dir = sftp_get_absolute_path(sftp->pwd, sftp->args.argv[i+1]);
        arg->line_dir = sftp_dup_utf8_to_line(sftp->line_codepage, arg->dir, sftp->seat);
        if (!arg->line_dir) {
            result = false;
        }
    }
    if (!result) {
        sftpcmdmkdir_free(&cmdmkdir->cmd);
        return NULL;
    }

    sftpcmd_clear_request(&cmdmkdir->cmd);
    send_mkdir(cmdmkdir, sftp);
    return &cmdmkdir->cmd;
}

static bool sftpcmdmkdir_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdMkdir *cmdmkdir = container_of(cmd, SftpCmdMkdir, cmd);
    bool result = fxp_mkdir_recv(pktin, cmd->req);
    sftpcmd_clear_request(&cmdmkdir->cmd);

    SftpCmdMkdirArg *arg = &cmdmkdir->argv[cmdmkdir->current_arg];
    if (result) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "mkdir %s: OK", arg->dir);
    } else {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "mkdir %s: %s", arg->dir, fxp_error());
    }
    sftp_dup_utf8_free(arg->line_dir, arg->dir);
    sfree((void *)arg->dir);
    arg->line_dir = NULL;
    arg->dir = NULL;

    cmdmkdir->current_arg++;
    if (cmdmkdir->current_arg == cmdmkdir->argc ) {
        return false;
    }
    send_mkdir(cmdmkdir, sftp);
    return true;
}

static void sftpcmdmkdir_free(SftpCmd *cmd)
{
    SftpCmdMkdir *cmdmkdir = container_of(cmd, SftpCmdMkdir, cmd);
    for (int i = 0; i < cmdmkdir->argc; i++) {
        SftpCmdMkdirArg *arg = &cmdmkdir->argv[i];
        sftp_dup_utf8_free(arg->line_dir, arg->dir);
        sfree((void *)arg->dir);
    }
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
