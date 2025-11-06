#include "sftpcmd.h"
#include "sftpfxp.h"
#include "sftpwcm.h"

typedef struct SftpCmdMv {
    SftpCmd cmd;
    SftpWildcardMatcherIterator it;
    const char *fname;
    const char *dstfname;
    bool dest_is_dir;
    const char *final_dstfname;
} SftpCmdMv;

static void send_rename(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdMv *cmdmv = container_of(cmd, SftpCmdMv, cmd);

    if (cmdmv->dest_is_dir) {
        const char *p = fname + strlen(fname);
        while (p > fname && p[-1] != '/') p--;
        cmdmv->final_dstfname = dupcat(cmdmv->dstfname, "/", p);
    } else {
        cmdmv->final_dstfname = dupstr(cmdmv->dstfname);
    }
    cmdmv->fname = fname;

    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmv->cmd, SSH_FXP_RENAME, fxp_rename_send(fname, cmdmv->final_dstfname));
}

static SftpCmd *sftpcmdmv_init(Sftp *sftp)
{
    if (sftp->args.argc < 3) {
        sftpcmd_print(sftp->seat, SEAT_OUTPUT_STDERR, "mv: expects two filenames");
        return NULL;
    }

    SftpCmdMv *cmdmv = snew(SftpCmdMv);
    cmdmv->dstfname = sftpcmd_get_absolute_path(sftp->pwd, sftp->args.argv[sftp->args.argc-1]);
    cmdmv->final_dstfname = NULL;
    sftpwcm_iterator_init(&cmdmv->it, send_rename);

    sftpcmd_clear_request(&cmdmv->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmv->cmd, SSH_FXP_STAT, fxp_stat_send(cmdmv->dstfname));
    return &cmdmv->cmd;
}

static bool sftpcmdmv_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdMv *cmdmv = container_of(cmd, SftpCmdMv, cmd);

    if (cmd->req_type == SSH_FXP_STAT) {
        struct fxp_attrs attrs;
        bool result = fxp_stat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);

        cmdmv->dest_is_dir = (result &&
            (attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) &&
            (attrs.permissions & 0040000));
        if (cmdmv->dest_is_dir) {
            cmdmv->it.end_arg = sftp->args.argc-1;
        } else {
            cmdmv->it.end_arg = 2;
            cmdmv->it.disable_wc = true;
        }
        return sftpwcm_iterator_next(&cmdmv->it, sftp, cmd);
    }
    if (cmd->req_type == SSH_FXP_RENAME) {
        bool result = fxp_rename_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);

        if (result) {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "%s -> %s", cmdmv->fname, cmdmv->final_dstfname);
        } else {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "mv %s %s: %s", cmdmv->fname, cmdmv->final_dstfname, fxp_error());
            return false;
        }
        sfree((void *)cmdmv->final_dstfname);
        cmdmv->final_dstfname = NULL;
        return sftpwcm_iterator_next(&cmdmv->it, sftp, cmd);
    }
    return sftpwcm_iterator_pktin(&cmdmv->it, sftp, cmd, pktin);
}

static void sftpcmdmv_free(SftpCmd *cmd)
{
    SftpCmdMv *cmdmv = container_of(cmd, SftpCmdMv, cmd);
    sftpwcm_iterator_uninit(&cmdmv->it);
    sfree((void *)cmdmv->dstfname);
    sfree((void *)cmdmv->final_dstfname);
    sfree(cmdmv);
}

static SftpCmdArgInfo sftpcmdmv_get_arg_info(int file_arg_index)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, true};
}

const SftpCmdVtable sftpcmdmv_vt = {
    .init = sftpcmdmv_init,
    .free = sftpcmdmv_free,
    .process_pkt = sftpcmdmv_process_pkt,
    .get_arg_info = sftpcmdmv_get_arg_info
};
