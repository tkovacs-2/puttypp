#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpwcm.h"
#include "sftpunicode.h"

typedef struct SftpCmdMv {
    SftpCmd cmd;
    SftpWildcardMatcherIterator it;
    const char *fname; //utf8
    const char *dstfname; //utf8
    bool dest_is_dir;
    const char *final_dstfname; //utf8
} SftpCmdMv;

static void send_rename(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdMv *cmdmv = container_of(cmd, SftpCmdMv, cmd);

    const char *fname_utf8 = sftp_dup_utf8_from_line(sftp->line_codepage, fname);

    if (cmdmv->dest_is_dir) {
        const char *p = fname_utf8 + strlen(fname_utf8);
        while (p > fname_utf8 && p[-1] != '/') p--;
        cmdmv->final_dstfname = dupcat(cmdmv->dstfname, "/", p);
    } else {
        cmdmv->final_dstfname = dupstr(cmdmv->dstfname);
    }
    assert(!cmdmv->fname);
    cmdmv->fname = fname_utf8;

    const char *line_final_dstfname = sftp_dup_utf8_to_line(sftp->line_codepage, cmdmv->final_dstfname, sftp->seat);
    assert(line_final_dstfname); // both dstfname and fname are convertible to line codepage

    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmv->cmd, SSH_FXP_RENAME, fxp_rename_send(fname, line_final_dstfname));
    sftp_dup_utf8_free(line_final_dstfname, cmdmv->final_dstfname);
}

static SftpCmd *sftpcmdmv_init(Sftp *sftp)
{
    if (sftp->args.argc < 3) {
        sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "mv: expects two filenames");
        return NULL;
    }

    const char *dstfname = sftp_get_absolute_path(sftp->pwd, sftp->args.argv[sftp->args.argc-1]);
    const char *line_dstfname = sftp_dup_utf8_to_line(sftp->line_codepage, dstfname, sftp->seat);
    if (!line_dstfname) {
        sfree((void *)dstfname);
        return NULL;
    }

    SftpCmdMv *cmdmv = snew(SftpCmdMv);
    cmdmv->fname = NULL;
    cmdmv->dstfname = dstfname;
    cmdmv->final_dstfname = NULL;
    sftpwcm_iterator_init(&cmdmv->it, NULL, NULL);

    sftpcmd_clear_request(&cmdmv->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdmv->cmd, SSH_FXP_STAT, fxp_stat_send(line_dstfname));
    sftp_dup_utf8_free(line_dstfname, dstfname);
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
        int end_arg = 2;
        bool disable_wc = true;
        if (cmdmv->dest_is_dir) {
            end_arg = sftp->args.argc-1;
            disable_wc = false;
        }
        SftpWildcardArgs *args = sftpwcm_args_create(sftp, 1, end_arg, disable_wc);
        if (args == NULL) {
            return false;
        }
        sftpwcm_iterator_init(&cmdmv->it, args, send_rename);
        bool next = sftpwcm_iterator_next(&cmdmv->it, sftp, cmd);
        assert(next);
        return true;
    }
    if (cmd->req_type == SSH_FXP_RENAME) {
        bool result = fxp_rename_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);

        if (result) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "%s -> %s", cmdmv->fname, cmdmv->final_dstfname);
        } else {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "mv %s %s: %s", cmdmv->fname, cmdmv->final_dstfname, fxp_error());
            return false;
        }
        sftp_dup_utf8_free(cmdmv->fname, cmdmv->it.cname);
        cmdmv->fname = NULL;
        sfree((void *)cmdmv->final_dstfname);
        cmdmv->final_dstfname = NULL;
        return sftpwcm_iterator_next(&cmdmv->it, sftp, cmd);
    }
    return sftpwcm_iterator_pktin(&cmdmv->it, sftp, cmd, pktin);
}

static void sftpcmdmv_free(SftpCmd *cmd)
{
    SftpCmdMv *cmdmv = container_of(cmd, SftpCmdMv, cmd);
    sftp_dup_utf8_free(cmdmv->fname, cmdmv->it.cname);
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
