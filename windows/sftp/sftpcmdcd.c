#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpunicode.h"

typedef struct {
    SftpCmd cmd;
    const char *line_pwd;
    bool get_realpath;
} SftpCmdCd;

static SftpCmd *sftpcmdcd_init(Sftp *sftp)
{
    const char *line_pwd;
    bool get_realpath;

    if (sftp->args.argc < 2) {
        line_pwd = dupstr(sftp->line_homedir);
        get_realpath = false;
    } else {
        line_pwd = sftp_utf8_to_line(sftp->line_codepage, sftp_get_absolute_path(sftp->pwd, sftp->args.argv[1]), sftp->seat);
        if (!line_pwd) {
            return NULL;
        }
        get_realpath = true;
    }

    SftpCmdCd *cmdcd = snew(SftpCmdCd);
    cmdcd->line_pwd = line_pwd;
    cmdcd->get_realpath = get_realpath;
    sftpcmd_clear_request(&cmdcd->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdcd->cmd, SSH_FXP_OPENDIR, fxp_opendir_send(cmdcd->line_pwd));
    return &cmdcd->cmd;
}

static bool sftpcmdcd_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdCd *cmdcd = container_of(cmd, SftpCmdCd, cmd);

    if (cmd->req_type == SSH_FXP_OPENDIR) {
        struct fxp_handle *dirh = fxp_opendir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!dirh) {
            sftp_line_printf(sftp, SEAT_OUTPUT_STDERR, cmdcd->line_pwd, "cd: directory %s: %s", utf8_arg, fxp_error());
            return false;
        }
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_CLOSE, fxp_close_send(dirh));
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (cmdcd->get_realpath) {
            sftp_set_sending_backend(sftp);
            sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(cmdcd->line_pwd));
            return true;
        }
    } else if (cmd->req_type == SSH_FXP_REALPATH) {
        const char *line_pwd = fxp_realpath_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (line_pwd) {
            sfree((void *)cmdcd->line_pwd);
            cmdcd->line_pwd = line_pwd;
        }
    }
    sftp_dup_utf8_free(sftp->pwd, sftp->line_pwd);
    sfree((void *)sftp->line_pwd);
    sftp->line_pwd = cmdcd->line_pwd;
    cmdcd->line_pwd = NULL;
    sftp->pwd = sftp_dup_utf8_from_line(sftp->line_codepage, sftp->line_pwd);
    sftp_print_pwd(sftp->seat, sftp->pwd);
    return false;
}

static void sftpcmdcd_free(SftpCmd *cmd)
{
    SftpCmdCd *cmdcd = container_of(cmd, SftpCmdCd, cmd);
    sfree((void *)cmdcd->line_pwd);
    sfree(cmdcd);
}

static SftpCmdArgInfo sftpcmdcd_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, true, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

const SftpCmdVtable sftpcmdcd_vt = {
    .init = sftpcmdcd_init,
    .free = sftpcmdcd_free,
    .process_pkt = sftpcmdcd_process_pkt,
    .get_arg_info = sftpcmdcd_get_arg_info
};
