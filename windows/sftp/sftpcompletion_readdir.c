#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpcompletion.h"

typedef struct CompletionReaddir {
    SftpCmd cmd;
    struct fxp_handle *dirh;
    SftpCompletionName *names;
    size_t nnames;
    size_t namesize;
} CompletionReaddir;

static void send_close(Sftp *sftp, SftpCmd *cmd, struct fxp_handle *dirh)
{
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_CLOSE, fxp_close_send(dirh));
}

static void free_name_list(CompletionReaddir *cmdreaddir)
{
    for (size_t i = 0; i < cmdreaddir->nnames; i++) {
        sfree((void *)cmdreaddir->names[i].name);
    }
    sfree(cmdreaddir->names);
    cmdreaddir->names = NULL;
    cmdreaddir->nnames = 0;
    cmdreaddir->namesize = 0;
}

static int completion_name_compare(const void *av, const void *bv)
{
    const SftpCompletionName *a = av;
    const SftpCompletionName *b = bv;
    return strcmp(a->name, b->name);
}

static void continue_completion(Sftp *sftp, CompletionReaddir *cmdreaddir)
{
    if (cmdreaddir->nnames > 0) {
        qsort(cmdreaddir->names, cmdreaddir->nnames, sizeof(*cmdreaddir->names), completion_name_compare);
        sftpcompletion_continue_completion(sftp->completion, cmdreaddir->names, cmdreaddir->nnames);
        cmdreaddir->names = NULL;
        cmdreaddir->nnames = 0;
        cmdreaddir->namesize = 0;
    }
}

static SftpCmd *completion_readdir_init(Sftp *sftp)
{
    const char *dir = sftpcompletion_get_remote_path(sftp->completion);

    CompletionReaddir *cmdreaddir = snew(CompletionReaddir);
    cmdreaddir->dirh = NULL;
    cmdreaddir->names = NULL;
    cmdreaddir->nnames = 0;
    cmdreaddir->namesize = 0;

    sftpcmd_clear_request(&cmdreaddir->cmd);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdreaddir->cmd, SSH_FXP_OPENDIR, fxp_opendir_send(dir));
    return &cmdreaddir->cmd;
}

static bool completion_readdir_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    CompletionReaddir *cmdreaddir = container_of(cmd, CompletionReaddir, cmd);

    if (cmd->req_type == SSH_FXP_OPENDIR) {
        cmdreaddir->dirh = fxp_opendir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (cmdreaddir->dirh == NULL) {
            return false;
        }
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdreaddir->dirh));
        return true;
    } else if (cmd->req_type == SSH_FXP_READDIR) {
        struct fxp_names *names = fxp_readdir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);

        if (names == NULL) {
            send_close(sftp, cmd, cmdreaddir->dirh);
            continue_completion(sftp, cmdreaddir);
            return true;
        }
        if (names->nnames == 0) {
            fxp_free_names(names);
            send_close(sftp, cmd, cmdreaddir->dirh);
            continue_completion(sftp, cmdreaddir);
            return true;
        }

        sgrowarrayn(cmdreaddir->names, cmdreaddir->namesize, cmdreaddir->nnames, names->nnames);
        for (size_t i = 0; i < (size_t)names->nnames; i++) {
            struct fxp_name *fn = &names->names[i];
            bool is_dir = (fn->attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) &&
                          ((fn->attrs.permissions & PERMS_DIRECTORY) == PERMS_DIRECTORY);
            if (is_dir && (strcmp(fn->filename, ".") == 0 || strcmp(fn->filename, "..") == 0)) {
                continue;
            }
            cmdreaddir->names[cmdreaddir->nnames].name = fn->filename;
            cmdreaddir->names[cmdreaddir->nnames].is_dir = is_dir;
            fn->filename = NULL;
            cmdreaddir->nnames++;
        }
        fxp_free_names(names);
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdreaddir->dirh));
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        cmdreaddir->dirh = NULL;
    }
    return false;
}

static void completion_readdir_free(SftpCmd *cmd)
{
    CompletionReaddir *cmdreaddir = container_of(cmd, CompletionReaddir, cmd);
    if (cmdreaddir->dirh) {
        sftp_free_fxphandle(cmdreaddir->dirh);
    }
    free_name_list(cmdreaddir);
    sfree(cmdreaddir);
}

const SftpCmdVtable sftpcompletion_readdir_vt = {
    .init = completion_readdir_init,
    .free = completion_readdir_free,
    .process_pkt = completion_readdir_process_pkt,
    .get_arg_info = NULL
};
