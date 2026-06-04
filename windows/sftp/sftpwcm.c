#include "sftpwcm.h"
#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "psftp.h"

typedef struct SftpWildcardMatcher {
    Sftp *sftp;
    SftpCmd *cmd;
    const char *cdir;
    struct fxp_handle *dirh;
    struct fxp_names *names;
    int namepos;
    const char *wildcard;
} SftpWildcardMatcher;

SftpWildcardMatcher *sftpwcm_begin(const char *name, Sftp *sftp, SftpCmd *cmd)
{
    char *wildcard;
    char *unwcdir, *tmpdir;
    int len;
    bool check;
    SftpWildcardMatcher *swcm;

    /*
     * We don't handle multi-level wildcards; so we expect to find
     * a fully specified directory part, followed by a wildcard
     * after that.
     */
    wildcard = stripslashes(name, false);

    unwcdir = dupstr(name);
    len = wildcard - name;
    unwcdir[len] = '\0';
    if (len > 0 && unwcdir[len-1] == '/')
        unwcdir[len-1] = '\0';
    tmpdir = snewn(1 + len, char);
    check = wc_unescape(tmpdir, unwcdir);
    sfree(tmpdir);

    if (!check) {
        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "Multiple-level wildcards are not supported in %s", name);
        sfree(unwcdir);
        return NULL;
    }

    swcm = snew(SftpWildcardMatcher);
    swcm->sftp = sftp;
    swcm->cmd = cmd;
    swcm->cdir = sftp_get_absolute_path(sftp->pwd, unwcdir);
    swcm->dirh = NULL;
    swcm->names = NULL;
    swcm->wildcard = dupstr(wildcard);
    sfree(unwcdir);

    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(swcm->cdir));
    return swcm;
}

bool sftpwcm_realpath_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    const char *cdir = fxp_realpath_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!cdir) {
        sftp_printf(swcm->sftp->seat, SEAT_OUTPUT_STDERR, "unable to open %s: %s", swcm->cdir, fxp_error());
        return false;
    }
    sfree((void *)swcm->cdir);
    swcm->cdir = cdir;
    sftp_set_sending_backend(swcm->sftp);
    sftpcmd_set_request(swcm->cmd, SSH_FXP_OPENDIR, fxp_opendir_send(swcm->cdir));
    return true;
}

bool sftpwcm_opendir_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    swcm->dirh = fxp_opendir_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!swcm->dirh) {
        sftp_printf(swcm->sftp->seat, SEAT_OUTPUT_STDERR, "unable to open %s: %s", swcm->cdir, fxp_error());
        return false;
    }
    return true;
}

const char *sftpwcm_get_filename(SftpWildcardMatcher *swcm)
{
    struct fxp_name *name;

    while (1) {
        if (swcm->names && swcm->namepos >= swcm->names->nnames) {
            fxp_free_names(swcm->names);
            swcm->names = NULL;
        }

        if (!swcm->names) {
            sftp_set_sending_backend(swcm->sftp);
            sftpcmd_set_request(swcm->cmd, SSH_FXP_READDIR, fxp_readdir_send(swcm->dirh));
            return NULL;
        }

        assert(swcm->names && swcm->namepos < swcm->names->nnames);

        name = &swcm->names->names[swcm->namepos++];

        if (!strcmp(name->filename, ".") || !strcmp(name->filename, ".."))
            continue;                  /* expected bad filenames */

        if (!vet_filename(name->filename)) {
            sftp_printf(swcm->sftp->seat, SEAT_OUTPUT_STDERR, "ignoring potentially dangerous server-supplied filename '%s'", name->filename);
            continue;                  /* unexpected bad filename */
        }

        if (!wc_match(swcm->wildcard, name->filename))
            continue;                  /* doesn't match the wildcard */

        /*
         * We have a working filename. Return it.
         */
        return dupprintf("%s/%s", swcm->cdir, name->filename);
    }
}

bool sftpwcm_readdir_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    assert(!swcm->names);

    swcm->names = fxp_readdir_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!swcm->names) {
        if (fxp_error_type() != SSH_FX_EOF) {
            sftp_printf(swcm->sftp->seat, SEAT_OUTPUT_STDERR, "%s: reading directory: %s", swcm->cdir, fxp_error());
        }
        return false;
    } else if (swcm->names->nnames == 0) {
        /*
         * Another failure mode which we treat as EOF is if
         * the server reports success from FXP_READDIR but
         * returns no actual names. This is unusual, since
         * from most servers you'd expect at least "." and
         * "..", but there's nothing forbidding a server from
         * omitting those if it wants to.
         */
        return false;
    }

    swcm->namepos = 0;
    return true;
}

void sftpwcm_finish(SftpWildcardMatcher *swcm)
{
    sftp_set_sending_backend(swcm->sftp);
    sftpcmd_set_request(swcm->cmd, SSH_FXP_CLOSE, fxp_close_send(swcm->dirh));
    swcm->dirh = NULL;
}

void sftpwcm_close_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    fxp_close_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    sftpwcm_free(swcm);
}

void sftpwcm_free(SftpWildcardMatcher *swcm)
{
    sfree((void *)swcm->cdir);
    if (swcm->dirh) {
        sftp_free_fxphandle(swcm->dirh);
    }
    if (swcm->names) {
        fxp_free_names(swcm->names);
    }
    sfree((void *)swcm->wildcard);
    sfree(swcm);
}

static bool sftpwcm_iterator_next_arg(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd)
{
    assert(!it->swcm && !it->cname);
    it->current_arg++;
    while (it->current_arg < it->end_arg) {
        const char *filename = sftp->args.argv[it->current_arg];
        char *unwcfname = snewn(strlen(filename)+1, char);
        bool is_wc = !it->disable_wc && !wc_unescape(unwcfname, filename);
        sfree(unwcfname);

        if (is_wc) {
            it->swcm = sftpwcm_begin(filename, sftp, cmd);
            if (!it->swcm) {
                it->current_arg++;
                continue;
            }
        } else {
            it->cname = sftp_get_absolute_path(sftp->pwd, filename);
            sftp_set_sending_backend(sftp);
            sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(it->cname));
        }
        return true;
    }
    return false;
}

bool sftpwcm_iterator_next(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd)
{
    sfree((void *)it->cname);
    it->cname = NULL;

    if (it->swcm) {
        it->cname = sftpwcm_get_filename(it->swcm);
        if (!it->cname) { // all names processed, next readdir sent
            return true;
        }
        it->func(it->cname, sftp, cmd);
        return true;
    }
    return sftpwcm_iterator_next_arg(it, sftp, cmd); // next argument
}

bool sftpwcm_iterator_pktin(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd, struct sftp_packet *pktin)
{
    if (cmd->req_type == SSH_FXP_REALPATH) {
        if (it->swcm) {
            if (sftpwcm_realpath_recv(it->swcm, pktin)) {
                return true;
            }
            sftpwcm_free(it->swcm);
            it->swcm = NULL;
        } else {
            const char *cname = fxp_realpath_recv(pktin, cmd->req);
            sftpcmd_clear_request(cmd);
            if (cname) {
                sfree((void *)it->cname);
                it->cname = cname;
                it->func(it->cname, sftp, cmd);
                return true;
            } else {
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "unable to open %s: %s", it->cname, fxp_error());
                return false;
            }
        }
        return sftpwcm_iterator_next_arg(it, sftp, cmd); // arg has problem, next argument
    } else if (cmd->req_type == SSH_FXP_OPENDIR) {
        if (!sftpwcm_opendir_recv(it->swcm, pktin)) {
            return false;
        }
        sftpwcm_get_filename(it->swcm);
        return true;
    } else if (cmd->req_type == SSH_FXP_READDIR) {
        if (!sftpwcm_readdir_recv(it->swcm, pktin)) {
            sftpwcm_finish(it->swcm);
            return true;
        }
        it->cname = sftpwcm_get_filename(it->swcm);
        if (!it->cname) { // all fetched names processed, next readdir sent
            return true;
        }
        it->func(it->cname, sftp, cmd);
        return true;
    } else if (cmd->req_type == SSH_FXP_CLOSE) {
        sftpwcm_close_recv(it->swcm, pktin);
        it->swcm = NULL;
        return sftpwcm_iterator_next_arg(it, sftp, cmd); // dir closed, next argument
    }
    return false;
}

void sftpwcm_iterator_init(SftpWildcardMatcherIterator* it, void (*func)(const char *, Sftp *, SftpCmd *))
{
    it->current_arg = 0;
    it->end_arg = 0;
    it->disable_wc = 0;
    it->swcm = NULL;
    it->cname = NULL;
    it->func = func;
}

void sftpwcm_iterator_uninit(SftpWildcardMatcherIterator* it)
{
    if (it->swcm) {
        sftpwcm_free(it->swcm);
    }
    sfree((void *)it->cname);
}
