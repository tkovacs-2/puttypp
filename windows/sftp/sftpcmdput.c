#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpgetput.h"
#include "psftp.h"
#include "sftpprogressbar.h"

const char *get_absolute_path(const char *pwd, const char *name);

typedef struct WildcardMatcherIterator {
    int current_arg;
    int end_arg;
    bool disable_wc;
    WildcardMatcher *wcm;
    const char *cname;
} WildcardMatcherIterator;

static void wcm_iterator_init(WildcardMatcherIterator* it)
{
    it->current_arg = 0;
    it->end_arg = 0;
    it->disable_wc = 0;
    it->wcm = NULL;
    it->cname = NULL;
}

static void wcm_iterator_uninit(WildcardMatcherIterator* it)
{
    if (it->wcm) {
        finish_wildcard_matching(it->wcm);
        it->wcm = NULL;
    }
    sfree((void *)it->cname);
}

static bool put_file(const char *fname, Sftp *sftp, SftpCmd *cmd);

static bool wcm_iterator_next_arg(WildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd)
{
    assert(!it->wcm && !it->cname);
    it->current_arg++;
    while (it->current_arg < it->end_arg) {
        const char *fname = get_absolute_path(sftp->lpwd, sftp->args.argv[it->current_arg]);
        bool is_wc = (!it->disable_wc && test_wildcard(fname, false) == WCTYPE_WILDCARD);

        if (is_wc) {
            it->wcm = begin_wildcard_matching(fname);
            it->cname = wildcard_get_filename(it->wcm);
            if (!it->cname) {
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "%s: nothing matched", fname);
            }
            sfree((void *)fname);
            if (!it->cname) {
                finish_wildcard_matching(it->wcm);
                it->wcm = NULL;
                it->current_arg++;
                continue;
            }
        } else {
            it->cname = fname;
        }
        return put_file(it->cname, sftp, cmd);
    }
    return false;
}

static bool wcm_iterator_next(WildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd)
{
    sfree((void *)it->cname);
    it->cname = NULL;

    if (it->wcm) {
        it->cname = wildcard_get_filename(it->wcm);
        if (it->cname) {
            return put_file(it->cname, sftp, cmd);
        }
        finish_wildcard_matching(it->wcm);
        it->wcm = NULL;
    }
    return wcm_iterator_next_arg(it, sftp, cmd);
}

typedef enum {
    SR_RECURSE_CHECK_IF_DIR,
    SR_RECURSE_CHECK_IF_PRESENT,
    SR_RESTART_GET_OFFSET,
    SR_CHECK_IF_DIR
} StatReason;

typedef struct SftpCmdPut {
    SftpCmd cmd;
    bool restart;
    bool multiple;
    bool recurse;
    bool user_outfname;
    WildcardMatcherIterator it;
    const char *fname;
    const char *outfname;
    struct fxp_handle *handle;
    struct fxp_xfer *xfer;
    RFile *file;
    bool stop;
    bool xfer_err;
    StatReason stat_reason;

    SftpDirStack dirstack;
    SftpProgressBar progress;
    Seat *progress_first_seat;
    uint64_t file_size;
} SftpCmdPut;


static bool open_files(Sftp *sftp, SftpCmdPut *cmdput)
{
    long permissions;
    struct fxp_attrs attrs;

    assert(!cmdput->file);
    cmdput->file = open_existing_file(cmdput->fname, &cmdput->file_size, NULL, NULL, &permissions);
    if (!cmdput->file) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "local: unable to open %s", cmdput->fname);
        return false;
    }
    attrs.flags = 0;
    PUT_PERMISSIONS(attrs, permissions);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdput->cmd, SSH_FXP_OPEN, fxp_open_send(cmdput->outfname, SSH_FXF_WRITE | SSH_FXF_CREAT | (cmdput->restart ? 0 : SSH_FXF_TRUNC), &attrs));
    return true;
}

static bool put_file(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdPut *cmdput = container_of(cmd, SftpCmdPut, cmd);

    if (fname == cmdput->it.cname) {
      fname = dupstr(fname);
    }
    assert(cmdput->fname == NULL);
    cmdput->fname = fname;
    if (!cmdput->outfname) {
        cmdput->outfname = sftp_get_absolute_path(sftp->pwd, stripslashes(fname, true));
    }

    if (cmdput->recurse && file_type(fname) == FILE_TYPE_DIRECTORY) {
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_STAT, fxp_stat_send(cmdput->outfname));
        cmdput->stat_reason = SR_RECURSE_CHECK_IF_DIR;
        return true;
    }

    if (cmdput->user_outfname && !cmdput->recurse) {
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(&cmdput->cmd, SSH_FXP_STAT, fxp_stat_send(cmdput->outfname));
        cmdput->stat_reason = SR_CHECK_IF_DIR;
        return true;
    }
    return open_files(sftp, cmdput);
}

static bool dir_put_file(SftpDir *dir, Sftp *sftp, SftpCmdPut *cmdput)
{
    const char *nextfname = dir_file_cat(dir->fname, dir->ournames[dir->i]);
    const char *nextoutfname = dupcat(dir->outfname, "/", dir->ournames[dir->i]);
    assert(cmdput->outfname == NULL);
    cmdput->outfname = nextoutfname;
    return put_file(nextfname, sftp, &cmdput->cmd);
}

static void check_dir_file_remote(SftpDir *dir, Sftp *sftp, SftpCmdPut *cmdput)
{
    char *nextoutfname = dupcat(dir->outfname, "/", dir->ournames[dir->i]);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdput->cmd, SSH_FXP_STAT, fxp_stat_send(nextoutfname));
    cmdput->stat_reason = SR_RECURSE_CHECK_IF_PRESENT;
    sfree(nextoutfname);
}

static bool next_file(Sftp *sftp, SftpCmdPut *cmdput)
{
    if (cmdput->recurse) {
        SftpDir *dir = sftpdirstack_top(&cmdput->dirstack);
        while (dir) {
            dir->i++;
            if (dir->i < dir->nnames) {
                break;
            }
            dir = sftpdirstack_pop(&cmdput->dirstack);
        }
        if (dir) {
            return dir_put_file(dir, sftp, cmdput);
        }
    }
    return wcm_iterator_next(&cmdput->it, sftp, &cmdput->cmd);
}

static bool read_dir(Sftp *sftp, SftpCmdPut *cmdput)
{
    const char *opendir_err;
    DirHandle *dh = open_directory(cmdput->fname, &opendir_err);
    if (!dh) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: unable to open directory: %s\n", cmdput->fname, opendir_err);
        return false;
    }
    const char *name = read_filename(dh);
    if (!name) {
        close_directory(dh);
        sfree((void *)cmdput->fname);
        sfree((void *)cmdput->outfname);
        cmdput->fname = NULL;
        cmdput->outfname = NULL;
        return next_file(sftp, cmdput);
    }
    SftpDir *dir = sftpdirstack_push(&cmdput->dirstack);
    dir->fname = cmdput->fname;
    dir->outfname = cmdput->outfname;
    cmdput->fname = NULL;
    cmdput->outfname = NULL;
    do {
        sgrowarray(dir->ournames, dir->namesize, dir->nnames);
        dir->ournames[dir->nnames++] = name;
    } while ((name = read_filename(dh)) != NULL);
    close_directory(dh);

    getput_sort_dir_names(dir);

    dir->i = 0;
    if (cmdput->restart) {
        check_dir_file_remote(dir, sftp, cmdput);
        return true;
    }
    return dir_put_file(dir, sftp, cmdput);
}

static void send_close(SftpCmdPut *cmdput, Sftp *sftp)
{
    sfree((void *)cmdput->fname);
    sfree((void *)cmdput->outfname);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdput->cmd, SSH_FXP_CLOSE, fxp_close_send(cmdput->handle));
    cmdput->fname = NULL;
    cmdput->outfname = NULL;
    cmdput->handle = NULL;
}

static void transfer(Sftp *sftp, SftpCmdPut *cmdput)
{
    bool uploaded = false;

    if (!cmdput->stop) {
        char buffer[4096];
        int len;
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(&cmdput->cmd, SSH_FXP_WRITE, NULL);
        while (xfer_upload_ready(cmdput->xfer) && !cmdput->xfer_err) {
            len = read_from_file(cmdput->file, buffer, sizeof(buffer));
            if (len == -1) {
                sftpprogressbar_finish(&cmdput->progress, sftp->seat);
                sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "error while reading local file");
                cmdput->xfer_err = true;
                cmdput->stop = true;
                break;
            } else if (len == 0) {
                cmdput->xfer_err = true;
                break;
            } else {
                xfer_upload_data(cmdput->xfer, buffer, len);
                sftpprogressbar_update(&cmdput->progress, len);
                uploaded = true;
            }
        }
    }
    if (uploaded || xfer_done(cmdput->xfer)) {
        sftpprogressbar_draw(&cmdput->progress, sftp->seat, sftp->width);
    }
    if (xfer_done(cmdput->xfer)) {
        sftpprogressbar_finish(&cmdput->progress, sftp->seat);
        xfer_cleanup(cmdput->xfer);
        cmdput->xfer = NULL;
        close_rfile(cmdput->file);
        cmdput->file = NULL;
        cmdput->file_size = 0;
        send_close(cmdput, sftp);
    }
}

static void start_transfer(Sftp *sftp, SftpCmdPut *cmdput, uint64_t offset)
{
    sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "local: %s => remote: %s", cmdput->fname, cmdput->outfname);
    sftpprogressbar_init(&cmdput->progress, offset, cmdput->file_size);
    cmdput->xfer = xfer_upload_init(cmdput->handle, offset);
    cmdput->xfer_err = false;
    transfer(sftp, cmdput);
}

static SftpCmd *generic_init(Sftp *sftp, bool restart, bool multiple)
{
    bool recurse;
    int i;

    if (!getput_parse_args(sftp, &i, &recurse)) {
        return NULL;
    }

    SftpCmdPut *cmdput = snew(SftpCmdPut);
    cmdput->restart = restart;
    cmdput->multiple = multiple;
    cmdput->recurse = recurse;
    cmdput->user_outfname = (!multiple && i+1 < sftp->args.argc);
    wcm_iterator_init(&cmdput->it);
    cmdput->it.current_arg = i-1;
    cmdput->it.end_arg = (multiple ? sftp->args.argc : i+1);
    cmdput->it.disable_wc = !multiple;
    cmdput->fname = NULL;
    if (cmdput->user_outfname) {
        cmdput->outfname = sftp_get_absolute_path(sftp->pwd, sftp->args.argv[i+1]);
    } else {
      cmdput->outfname = NULL;
    }
    cmdput->handle = NULL;
    cmdput->xfer = NULL;
    cmdput->file = NULL;
    cmdput->file_size = 0;
    cmdput->stop = false;
    cmdput->progress_first_seat = sftp->seat;
    sftpdirstack_init(&cmdput->dirstack);

    sftpcmd_clear_request(&cmdput->cmd);
    if (!wcm_iterator_next(&cmdput->it, sftp, &cmdput->cmd)) {
        sfree((void *)cmdput->fname);
        sfree((void *)cmdput->outfname);
        wcm_iterator_uninit(&cmdput->it);
        sfree(cmdput);
        return NULL;
    }
    return &cmdput->cmd;
}

static SftpCmd *sftpcmdput_init(Sftp *sftp)
{
    return generic_init(sftp, false, false);
}

static SftpCmd *sftpcmdmput_init(Sftp *sftp)
{
    return generic_init(sftp, false, true);
}

static SftpCmd *sftpcmdreput_init(Sftp *sftp)
{
    return generic_init(sftp, true, false);
}

static bool sftpcmdput_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdPut *cmdput = container_of(cmd, SftpCmdPut, cmd);

    if (cmd->req_type == SSH_FXP_STAT && cmdput->stat_reason == SR_RECURSE_CHECK_IF_DIR) {
        struct fxp_attrs attrs;
        bool result = fxp_stat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);
        if (!result || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) || !(attrs.permissions & 0040000)) {
            sftp_set_sending_backend(sftp);
            sftpcmd_set_request(cmd, SSH_FXP_MKDIR, fxp_mkdir_send(cmdput->outfname, NULL));
            return true;
        }
        return read_dir(sftp, cmdput);
    } else if (cmd->req_type == SSH_FXP_STAT && cmdput->stat_reason == SR_RESTART_GET_OFFSET) {
        struct fxp_attrs attrs;
        bool retd = fxp_fstat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);
        if (!retd) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "read size of %s: %s", cmdput->outfname, fxp_error());
            cmdput->stop = true;
            send_close(cmdput, sftp);
            return true;
        }
        if (!(attrs.flags & SSH_FILEXFER_ATTR_SIZE)) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "read size of %s: size was not given", cmdput->outfname);
            cmdput->stop = true;
            send_close(cmdput, sftp);
            return true;
        }
        uint64_t offset = attrs.size;
        if (offset != 0) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "reput: restarting at file position %"PRIu64, offset);

            if (seek_file((WFile *)cmdput->file, offset, FROM_START) != 0) {
                seek_file((WFile *)cmdput->file, 0, FROM_END);    /* *shrug* */
            }
        }
        start_transfer(sftp, cmdput, offset);
        return true;
    } else if (cmd->req_type == SSH_FXP_STAT && cmdput->stat_reason == SR_RECURSE_CHECK_IF_PRESENT) {
        struct fxp_attrs attrs;
        bool result = fxp_stat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);
        SftpDir *dir = sftpdirstack_top(&cmdput->dirstack);
        if (result) {
            dir->i++;
            if (dir->i < dir->nnames) {
                check_dir_file_remote(dir, sftp, cmdput);
                return true;
            }
       }
       if (dir->i > 0) {
          dir->i--;
       }
       return dir_put_file(dir, sftp, cmdput);
    } else if (cmd->req_type == SSH_FXP_STAT && cmdput->stat_reason == SR_CHECK_IF_DIR) {
        struct fxp_attrs attrs;
        bool result = fxp_stat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);
        if (result && (attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) && (attrs.permissions & 0040000)) {
            const char *outfdir = cmdput->outfname;
            cmdput->outfname = dupcat(outfdir, "/", stripslashes(cmdput->fname, true));
            sfree((void *)outfdir);
        }
        return open_files(sftp, cmdput);
    } else if (cmd->req_type == SSH_FXP_MKDIR) {
        bool result = fxp_mkdir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!result) {
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: create directory: %s\n", cmdput->outfname, fxp_error());
            return false;
        }
        return read_dir(sftp, cmdput);
    } else if (cmd->req_type == SSH_FXP_OPEN) {
        cmdput->handle = fxp_open_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!cmdput->handle) {
            close_rfile(cmdput->file);
            cmdput->file = NULL;
            cmdput->file_size = 0;
            sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: open for write: %s", cmdput->outfname, fxp_error());
            return false;
        }

        if (cmdput->restart) {
            sftp_set_sending_backend(sftp);
            sftpcmd_set_request(cmd, SSH_FXP_STAT, fxp_fstat_send(cmdput->handle));
            cmdput->stat_reason = SR_RESTART_GET_OFFSET;
            return true;
        }

        start_transfer(sftp, cmdput, 0);
        return true;
    } else if (cmd->req_type == SSH_FXP_WRITE) {
        int ret = xfer_upload_gotpkt(cmdput->xfer, pktin);
        if (ret <= 0) {
            if (ret == INT_MIN) {        /* pktin not even freed */
                sfree(pktin);
            }
            if (!cmdput->stop) {
                sftpprogressbar_finish(&cmdput->progress, sftp->seat);
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "error while writing: %s", fxp_error());
                cmdput->xfer_err = true;
                cmdput->stop = true;
            }
        }
        transfer(sftp, cmdput);
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (cmdput->stop) {
            return false;
        }
        return next_file(sftp, cmdput);
    }
    return false;
}

static void sftpcmdput_free(SftpCmd *cmd)
{
    SftpCmdPut *cmdput = container_of(cmd, SftpCmdPut, cmd);
    sftpprogressbar_finish(&cmdput->progress, cmdput->progress_first_seat);
    sfree((void *)cmdput->fname);
    sfree((void *)cmdput->outfname);
    wcm_iterator_uninit(&cmdput->it);
    if (cmdput->xfer) {
        xfer_cleanup(cmdput->xfer);
    }
    if (cmdput->handle) {
        sftp_free_fxphandle(cmdput->handle);
    }
    if (cmdput->file) {
       close_rfile(cmdput->file);
    }
    sftpdirstack_uninit(&cmdput->dirstack);
    sfree(cmdput);
}

static SftpCmdArgInfo sftpcmdput_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_LOCAL, false, true};
    } else if (file_arg_index == 1) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

static SftpCmdArgInfo sftpcmdmput_get_arg_info(int file_arg_index)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_LOCAL, false, true};
}

const SftpCmdVtable sftpcmdput_vt = {
    .init = sftpcmdput_init,
    .free = sftpcmdput_free,
    .process_pkt = sftpcmdput_process_pkt,
    .get_arg_info = sftpcmdput_get_arg_info
};

const SftpCmdVtable sftpcmdmput_vt = {
    .init = sftpcmdmput_init,
    .free = sftpcmdput_free,
    .process_pkt = sftpcmdput_process_pkt,
    .get_arg_info = sftpcmdmput_get_arg_info
};

const SftpCmdVtable sftpcmdreput_vt = {
    .init = sftpcmdreput_init,
    .free = sftpcmdput_free,
    .process_pkt = sftpcmdput_process_pkt,
    .get_arg_info = sftpcmdput_get_arg_info
};
