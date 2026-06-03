#include "sftpcmd.h"
#include "sftpfxp.h"
#include "sftpwcm.h"
#include "sftpgetput.h"
#include "psftp.h"
#include "sftpprogressbar.h"

const char *get_absolute_path(const char *pwd, const char *name);

typedef struct SftpCmdGet {
    SftpCmd cmd;
    bool restart;
    bool multiple;
    bool recurse;
    bool user_outfname;
    SftpWildcardMatcherIterator it;

    const char *fname;
    const char *outfname;
    struct fxp_attrs attrs;
    int open_req;
    struct fxp_handle *handle;
    struct fxp_xfer *xfer;
    WFile *file;
    bool stop;

    SftpDirStack dirstack;
    SftpProgressBar progress;
    Seat *progress_first_seat;
} SftpCmdGet;

static void send_close(SftpCmdGet *cmdget, Sftp *sftp)
{
    sfree((void *)cmdget->fname);
    sfree((void *)cmdget->outfname);
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(&cmdget->cmd, SSH_FXP_CLOSE, fxp_close_send(cmdget->handle));
    cmdget->fname = NULL;
    cmdget->outfname = NULL;
    cmdget->handle = NULL;
}

static void sftpcmdget_free(SftpCmd *cmd);
static bool sftpcmdget_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin);
static bool get_file_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin);

static const SftpCmdVtable getfile_vt = {
    .init = NULL,
    .free = sftpcmdget_free,
    .process_pkt = get_file_process_pkt
};

static const SftpCmdVtable get_vt = {
    .init = NULL,
    .free = sftpcmdget_free,
    .process_pkt = sftpcmdget_process_pkt
};

static void get_file(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdGet *cmdget = container_of(cmd, SftpCmdGet, cmd);

    if (fname == cmdget->it.cname) {
      fname = dupstr(fname);
    }
    assert(cmdget->fname == NULL);
    cmdget->fname = fname;
    if (!cmdget->outfname) {
        cmdget->outfname = get_absolute_path(sftp->lpwd, stripslashes(fname, false));
    }

    cmd->vt = &getfile_vt;
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_STAT, fxp_stat_send(fname));
}

static void dir_get_file(SftpDir *dir, Sftp *sftp, SftpCmdGet *cmdget)
{
    const char *nextfname = dupcat(dir->fname, "/", dir->ournames[dir->i]);
    const char *nextoutfname = dir_file_cat(dir->outfname, dir->ournames[dir->i]);
    assert(cmdget->outfname == NULL);
    cmdget->outfname = nextoutfname;
    get_file(nextfname, sftp, &cmdget->cmd);
}

static bool next_file(Sftp *sftp, SftpCmdGet *cmdget)
{
    if (cmdget->recurse) {
        SftpDir *dir = sftpdirstack_top(&cmdget->dirstack);
        while (dir) {
            dir->i++;
            if (dir->i < dir->nnames) {
                break;
            }
            dir = sftpdirstack_pop(&cmdget->dirstack);
        }
        if (dir) {
            dir_get_file(dir, sftp, cmdget);
            return true;
        }
    }
    cmdget->cmd.vt = &get_vt;
    return sftpwcm_iterator_next(&cmdget->it, sftp, &cmdget->cmd);
}

static bool get_file_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdGet *cmdget = container_of(cmd, SftpCmdGet, cmd);

    if (cmd->req_type == SSH_FXP_STAT) {
        bool result = fxp_stat_recv(pktin, cmd->req, &cmdget->attrs);
        sftpcmd_clear_request(cmd);
        if (cmdget->recurse) {
            if (result && (cmdget->attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS) && (cmdget->attrs.permissions & 0040000)) {
                if (file_type(cmdget->outfname) != FILE_TYPE_DIRECTORY && !create_directory(cmdget->outfname)) {
                    sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: Cannot create local directory", cmdget->outfname);
                    return false;
                }
                sftp_set_sending_backend(sftp);
                sftpcmd_set_request(cmd, SSH_FXP_OPENDIR, fxp_opendir_send(cmdget->fname));
                return true;
            }
        }
        if (!result) {
            cmdget->attrs.flags = 0;
        }
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_OPEN, fxp_open_send(cmdget->fname, SSH_FXF_READ, NULL));
        return true;
    } else if (cmd->req_type == SSH_FXP_OPENDIR) {
        assert(!cmdget->handle);
        cmdget->open_req = cmd->req_type;
        cmdget->handle = fxp_opendir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!cmdget->handle) {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: unable to open directory: %s", cmdget->fname, fxp_error());
            return false;
        }
        SftpDir *dir = sftpdirstack_push(&cmdget->dirstack);
        dir->fname = cmdget->fname;
        dir->outfname = cmdget->outfname;
        cmdget->fname = NULL;
        cmdget->outfname = NULL;
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdget->handle));
        return true;
    } else if (cmd->req_type == SSH_FXP_READDIR) {
        SftpDir *dir = sftpdirstack_top(&cmdget->dirstack);
        struct fxp_names *names = fxp_readdir_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (names == NULL) {
            if (fxp_error_type() != SSH_FX_EOF) {
                sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: reading directory: %s", dir->fname, fxp_error());
                cmdget->stop = true;
            }
            send_close(cmdget, sftp);
            return true;
        }
        if (names->nnames == 0) {
            fxp_free_names(names);
            send_close(cmdget, sftp);
            return true;
        }
        sgrowarrayn(dir->ournames, dir->namesize, dir->nnames, names->nnames);
        for (size_t i = 0; i < names->nnames; i++) {
            if (strcmp(names->names[i].filename, ".") && strcmp(names->names[i].filename, "..")) {
                if (!vet_filename(names->names[i].filename)) {
                    sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "ignoring potentially dangerous server-supplied filename '%s'", names->names[i].filename);
                    continue;
                }
                dir->ournames[dir->nnames++] = names->names[i].filename;
                names->names[i].filename = NULL;
            }
        }
        fxp_free_names(names);
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_READDIR, fxp_readdir_send(cmdget->handle));
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE && cmdget->open_req == SSH_FXP_OPENDIR) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        cmdget->open_req = 0;
        if (cmdget->stop) {
            return false;
        }
        SftpDir *dir = sftpdirstack_top(&cmdget->dirstack);
        if (dir->nnames == 0) {
          sftpdirstack_pop(&cmdget->dirstack);
          return next_file(sftp, cmdget);
        }
        /*
         * Sort the names into a clear order. This ought to
         * make things more predictable when we're doing a
         * reget of the same directory, just in case two
         * readdirs on the same remote directory return a
         * different order.
         */
        getput_sort_dir_names(dir);
        /*
         * If we're in restart mode, find the last filename on
         * this list that already exists. We may have to do a
         * reget on _that_ file, but shouldn't have to do
         * anything on the previous files.
         *
         * If none of them exists, of course, we start at 0.
         */
        dir->i = 0;
        if (cmdget->restart) {
            while (dir->i < dir->nnames) {
                char *nextoutfname;
                bool nonexistent;
                nextoutfname = dir_file_cat(dir->outfname, dir->ournames[dir->i]);
                nonexistent = (file_type(nextoutfname) == FILE_TYPE_NONEXISTENT);
                sfree(nextoutfname);
                if (nonexistent) {
                    break;
                }
                dir->i++;
            }
            if (dir->i > 0) {
                dir->i--;
            }
        }
        dir_get_file(dir, sftp, cmdget);
        return true;
    } else if (cmd->req_type == SSH_FXP_OPEN) {
        assert(!cmdget->handle);
        cmdget->open_req = cmd->req_type;
        cmdget->handle = fxp_open_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        if (!cmdget->handle) {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: open for read: %s", cmdget->fname, fxp_error());
            return false;
        }

        if (cmdget->user_outfname && !cmdget->recurse && file_type(cmdget->outfname) == FILE_TYPE_DIRECTORY) {
          const char *outfdir = cmdget->outfname;
          cmdget->outfname = dir_file_cat(outfdir, stripslashes(cmdget->fname, false));
          sfree((void *)outfdir);
        }
        assert(!cmdget->file);
        if (cmdget->restart) {
            cmdget->file = open_existing_wfile(cmdget->outfname, NULL);
        }
        if (!cmdget->file) {
            cmdget->file = open_new_file(cmdget->outfname, GET_PERMISSIONS(cmdget->attrs, -1));
        }
        if (!cmdget->file) {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "local: unable to open %s", cmdget->outfname);
            send_close(cmdget, sftp);
            cmdget->stop = true;
            return true;
        }

        uint64_t offset = 0;
        if (cmdget->restart) {
            if (seek_file(cmdget->file, 0, FROM_END) == -1) {
                sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "reget: cannot restart %s - file too large", cmdget->outfname);
                close_wfile(cmdget->file);
                cmdget->file = NULL;
                send_close(cmdget, sftp);
                cmdget->stop = true;
                return true;
            }
            offset = get_file_posn(cmdget->file);
            if (offset != 0) {
                sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "reget: restarting at file position %"PRIu64"", offset);
            }
        }
        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDOUT, "remote: %s => local: %s", cmdget->fname, cmdget->outfname);
        assert(!cmdget->xfer);
        sftpprogressbar_init(&cmdget->progress, offset, (cmdget->attrs.flags & SSH_FILEXFER_ATTR_SIZE) ? cmdget->attrs.size : 0);
        sftp_set_sending_backend(sftp);
        cmdget->xfer = xfer_download_init(cmdget->handle, offset);
        sftpcmd_set_request(cmd, SSH_FXP_READ, NULL);
        return true;
    } else if (cmd->req_type == SSH_FXP_READ) {
        int retd = xfer_download_gotpkt(cmdget->xfer, pktin);
        if (retd <= 0) {
            sftpprogressbar_finish(&cmdget->progress, sftp->seat);
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "error while reading: %s", fxp_error());
            if (retd == INT_MIN) {
                sfree(pktin);
            }
            cmdget->stop = true;
        }

        void *vbuf;
        int len;
        bool shown_err = false;
        bool got_data = false;
        while (xfer_download_data(cmdget->xfer, &vbuf, &len)) {
            unsigned char *buf = (unsigned char *)vbuf;

            int wpos = 0;
            while (wpos < len) {
                int wlen = write_to_file(cmdget->file, buf + wpos, len - wpos);
                if (wlen <= 0) {
                    if (!shown_err) {
                        sftpprogressbar_finish(&cmdget->progress, sftp->seat);
                        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "error while writing local file");
                        shown_err = true;
                    }
                    cmdget->stop = true;
                    xfer_set_error(cmdget->xfer);
                    break;
                }
                wpos += wlen;
            }
            if (wpos < len) {          /* we had an error */
                cmdget->stop = true;
                xfer_set_error(cmdget->xfer);
            } else if (len > 0) {
                sftpprogressbar_update(&cmdget->progress, len);
                got_data = true;
            }
            sfree(vbuf);
        }
        if (got_data || xfer_done(cmdget->xfer)) {
            sftpprogressbar_draw(&cmdget->progress, sftp->seat, sftp->width);
        }
        if (xfer_done(cmdget->xfer)) {
            sftpprogressbar_finish(&cmdget->progress, sftp->seat);
            xfer_cleanup(cmdget->xfer);
            cmdget->xfer = NULL;
            close_wfile(cmdget->file);
            cmdget->file = NULL;
            send_close(cmdget, sftp);
        } else {
            sftp_set_sending_backend(sftp);
            xfer_download_queue(cmdget->xfer);
        }
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE && cmdget->open_req == SSH_FXP_OPEN) {
        fxp_close_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);
        cmdget->open_req = 0;
        if (cmdget->stop) {
            return false;
        }
        return next_file(sftp, cmdget);
    }
    return false;
}

static SftpCmd *generic_init(Sftp *sftp, bool restart, bool multiple)
{
    bool recurse;
    int i;

    if (!getput_parse_args(sftp, &i, &recurse)) {
        return NULL;
    }

    SftpCmdGet *cmdget = snew(SftpCmdGet);
    cmdget->restart = restart;
    cmdget->multiple = multiple;
    cmdget->recurse = recurse;
    cmdget->user_outfname = (!multiple && i+1 < sftp->args.argc);
    sftpwcm_iterator_init(&cmdget->it, get_file);
    cmdget->it.current_arg = i-1;
    cmdget->it.end_arg = (multiple ? sftp->args.argc : i+1);
    cmdget->it.disable_wc = !multiple;
    cmdget->fname = NULL;
    if (cmdget->user_outfname) {
        cmdget->outfname = get_absolute_path(sftp->lpwd, sftp->args.argv[i+1]);
    } else {
      cmdget->outfname = NULL;
    }
    cmdget->handle = NULL;
    cmdget->xfer = NULL;
    cmdget->file = NULL;
    cmdget->stop = false;
    cmdget->progress_first_seat = sftp->seat;
    sftpdirstack_init(&cmdget->dirstack);

    sftpcmd_clear_request(&cmdget->cmd);
    if (!sftpwcm_iterator_next(&cmdget->it, sftp, &cmdget->cmd)) {
        sfree(cmdget);
        return NULL;
    }
    return &cmdget->cmd;
}

static SftpCmd *sftpcmdget_init(Sftp *sftp)
{
    return generic_init(sftp, false, false);
}

static SftpCmd *sftpcmdmget_init(Sftp *sftp)
{
    return generic_init(sftp, false, true);
}

static SftpCmd *sftpcmdreget_init(Sftp *sftp)
{
    return generic_init(sftp, true, false);
}

static bool sftpcmdget_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdGet *cmdget = container_of(cmd, SftpCmdGet, cmd);
    return sftpwcm_iterator_pktin(&cmdget->it, sftp, cmd, pktin);
}

static void sftpcmdget_free(SftpCmd *cmd)
{
    SftpCmdGet *cmdget = container_of(cmd, SftpCmdGet, cmd);
    sftpprogressbar_finish(&cmdget->progress, cmdget->progress_first_seat);
    sfree((void *)cmdget->fname);
    sfree((void *)cmdget->outfname);
    sftpwcm_iterator_uninit(&cmdget->it);
    if (cmdget->xfer) {
        xfer_cleanup(cmdget->xfer);
    }
    if (cmdget->handle) {
        sftpcmd_free_fxphandle(cmdget->handle);
    }
    if (cmdget->file) {
       close_wfile(cmdget->file);
    }
    sftpdirstack_uninit(&cmdget->dirstack);
    sfree(cmdget);
}

static SftpCmdArgInfo sftpcmdget_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, true};
    } else if (file_arg_index == 1) {
        return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_LOCAL, false, false};
    }
    return sftpcmd_get_arg_info(file_arg_index);
}

static SftpCmdArgInfo sftpcmdmget_get_arg_info(int file_arg_index)
{
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, true};
}

const SftpCmdVtable sftpcmdget_vt = {
    .init = sftpcmdget_init,
    .free = sftpcmdget_free,
    .process_pkt = sftpcmdget_process_pkt,
    .get_arg_info = sftpcmdget_get_arg_info
};

const SftpCmdVtable sftpcmdmget_vt = {
    .init = sftpcmdmget_init,
    .free = sftpcmdget_free,
    .process_pkt = sftpcmdget_process_pkt,
    .get_arg_info = sftpcmdmget_get_arg_info
};

const SftpCmdVtable sftpcmdreget_vt = {
    .init = sftpcmdreget_init,
    .free = sftpcmdget_free,
    .process_pkt = sftpcmdget_process_pkt,
    .get_arg_info = sftpcmdget_get_arg_info
};