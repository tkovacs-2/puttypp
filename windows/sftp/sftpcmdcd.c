#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"

typedef struct {
    SftpCmd cmd;
    const char *pwd;
    bool get_realpath;
} SftpCmdCd;

static SftpCmd *sftpcmdcd_init(Sftp *sftp)
{
    SftpCmdCd *cmdcd = snew(SftpCmdCd);
    if (sftp->args.argc < 2) {
        cmdcd->pwd = dupstr(sftp->homedir);
        cmdcd->get_realpath = false;
    } else {
        cmdcd->pwd = sftp_get_absolute_path(sftp->pwd, sftp->args.argv[1]);
        cmdcd->get_realpath = true;
    }
    sftpcmd_clear_request(&cmdcd->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdcd->cmd, SSH_FXP_OPENDIR, fxp_opendir_send(cmdcd->pwd));
    return &cmdcd->cmd;
}

static bool sftpcmdcd_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdCd *cmdcd = container_of(cmd, SftpCmdCd, cmd);

    if (cmd->req_type == SSH_FXP_OPENDIR) {
        struct fxp_handle *dirh = fxp_opendir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!dirh) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "cd: directory %s: %s", cmdcd->pwd, fxp_error());
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
            sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(cmdcd->pwd));
            return true;
        }
    } else if (cmd->req_type == SSH_FXP_REALPATH) {
      const char *pwd = fxp_realpath_recv(pktin, cmd->req);
      sftpcmd_clear_request(cmd);
      if (pwd) {
          sfree((void *)cmdcd->pwd);
          cmdcd->pwd = pwd;
      }
    }
    sfree((void *)sftp->pwd);
    sftp->pwd = cmdcd->pwd;
    cmdcd->pwd = NULL;
    sftp_print_pwd(sftp->seat, sftp->pwd);
    return false;
}

static void sftpcmdcd_free(SftpCmd *cmd)
{
    SftpCmdCd *cmdcd = container_of(cmd, SftpCmdCd, cmd);
    sfree((void *)cmdcd->pwd);
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
