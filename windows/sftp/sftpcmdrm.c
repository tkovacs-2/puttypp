#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpwcm.h"

typedef struct SftpCmdRm {
    SftpCmd cmd;
    SftpWildcardMatcherIterator it;
    bool rmdir;
} SftpCmdRm;

static void send_remove(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdRm *cmdrm = container_of(cmd, SftpCmdRm, cmd);

    sftp_set_sending_backend(sftp);
    if (cmdrm->rmdir) {
        sftpcmd_set_request(cmd, SSH_FXP_RMDIR, fxp_rmdir_send(fname));
    } else {
        sftpcmd_set_request(cmd, SSH_FXP_REMOVE, fxp_remove_send(fname));
    }
}

static SftpCmd *sftpcmdrm_init(Sftp *sftp)
{
    if (sftp->args.argc < 2) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: expects a filename", sftp->args.argv[0]);
        return NULL;
    }

    SftpCmdRm *cmdrm = snew(SftpCmdRm);
    cmdrm->rmdir = (strcmp(sftp->args.argv[0], "rmdir") == 0);
    sftpwcm_iterator_init(&cmdrm->it, send_remove);
    cmdrm->it.end_arg = sftp->args.argc;

    sftpcmd_clear_request(&cmdrm->cmd);
    if (!sftpwcm_iterator_next(&cmdrm->it, sftp, &cmdrm->cmd)) {
        sfree(cmdrm);
        return NULL;
    }
    return &cmdrm->cmd;
}

static bool sftpcmdrm_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdRm *cmdrm = container_of(cmd, SftpCmdRm, cmd);

    if (cmd->req_type == SSH_FXP_REMOVE || cmd->req_type == SSH_FXP_RMDIR) {
        bool result;
        if (cmd->req_type == SSH_FXP_RMDIR && cmdrm->rmdir) {
            result = fxp_rmdir_recv(pktin, cmd->req);
        } else if (cmd->req_type == SSH_FXP_REMOVE && !cmdrm->rmdir) {
            result = fxp_remove_recv(pktin, cmd->req);
        } else {
            return false;
        }
        sftpcmd_clear_request(cmd);
        if (result) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "%s %s: OK", sftp->args.argv[0], cmdrm->it.cname);
        } else {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s %s: %s", sftp->args.argv[0], cmdrm->it.cname, fxp_error());
        }
        return sftpwcm_iterator_next(&cmdrm->it, sftp, cmd);
    }
    return sftpwcm_iterator_pktin(&cmdrm->it, sftp, cmd, pktin);
}

static void sftpcmdrm_free(SftpCmd *cmd)
{
    SftpCmdRm *cmdrm = container_of(cmd, SftpCmdRm, cmd);
    sftpwcm_iterator_uninit(&cmdrm->it);
    sfree(cmdrm);
}

static SftpCmdArgInfo sftpcmdrm_get_arg_info(int file_arg_index)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, true};
}

const SftpCmdVtable sftpcmdrm_vt = {
    .init = sftpcmdrm_init,
    .free = sftpcmdrm_free,
    .process_pkt = sftpcmdrm_process_pkt,
    .get_arg_info = sftpcmdrm_get_arg_info
};
