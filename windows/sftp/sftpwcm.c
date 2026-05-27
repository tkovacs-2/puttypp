#include "sftpwcm.h"
#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpunicode.h"
#include "psftp.h"

typedef struct SftpWildcardArg {
    const char *name; //line codepage
    const char *wildcard; //line codepage
} SftpWildcardArg;

typedef struct SftpWildcardArgs {
    int argc;
    SftpWildcardArg argv[1];
} SftpWildcardArgs;

typedef struct SftpWildcardMatcher {
    Sftp *sftp;
    SftpCmd *cmd;
    const char *cdir; //line codepage
    struct fxp_handle *dirh;
    struct fxp_names *names;
    int namepos;
    const char *wildcard; //line codepage
} SftpWildcardMatcher;

static void sftpwcm_free(SftpWildcardMatcher *swcm);

static SftpWildcardMatcher *sftpwcm_begin(const char *dir, const char *wildcard, Sftp *sftp, SftpCmd *cmd)
{
    SftpWildcardMatcher *swcm;

    swcm = snew(SftpWildcardMatcher);
    swcm->sftp = sftp;
    swcm->cmd = cmd;
    swcm->cdir = dir;
    swcm->dirh = NULL;
    swcm->names = NULL;
    swcm->wildcard = wildcard;

    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(swcm->cdir));
    return swcm;
}

static bool sftpwcm_realpath_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    const char *cdir = fxp_realpath_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!cdir) {
        sftp_line_printf(swcm->sftp, SEAT_OUTPUT_STDERR, swcm->cdir, "unable to open %s: %s", utf8_arg, fxp_error());
        return false;
    }
    sfree((void *)swcm->cdir);
    swcm->cdir = cdir;
    sftp_set_sending_backend(swcm->sftp);
    sftpcmd_set_request(swcm->cmd, SSH_FXP_OPENDIR, fxp_opendir_send(swcm->cdir));
    return true;
}

static bool sftpwcm_opendir_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    swcm->dirh = fxp_opendir_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!swcm->dirh) {
        sftp_line_printf(swcm->sftp, SEAT_OUTPUT_STDERR, swcm->cdir, "unable to open %s: %s", utf8_arg, fxp_error());
        return false;
    }
    return true;
}

static const char *sftpwcm_get_filename(SftpWildcardMatcher *swcm)
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
            sftp_line_printf(swcm->sftp, SEAT_OUTPUT_STDERR, name->filename, "ignoring potentially dangerous server-supplied filename '%s'", utf8_arg);
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

static bool sftpwcm_readdir_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    assert(!swcm->names);

    swcm->names = fxp_readdir_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    if (!swcm->names) {
        if (fxp_error_type() != SSH_FX_EOF) {
            sftp_line_printf(swcm->sftp, SEAT_OUTPUT_STDERR, swcm->cdir, "%s: reading directory: %s", utf8_arg, fxp_error());
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

static void sftpwcm_finish(SftpWildcardMatcher *swcm)
{
    sftp_set_sending_backend(swcm->sftp);
    sftpcmd_set_request(swcm->cmd, SSH_FXP_CLOSE, fxp_close_send(swcm->dirh));
    swcm->dirh = NULL;
}

static void sftpwcm_close_recv(SftpWildcardMatcher *swcm, struct sftp_packet *pktin)
{
    fxp_close_recv(pktin, swcm->cmd->req);
    sftpcmd_clear_request(swcm->cmd);
    sftpwcm_free(swcm);
}

static void sftpwcm_free(SftpWildcardMatcher *swcm)
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

void sftpwcm_args_free(SftpWildcardArgs *args)
{
    if (!args) {
        return;
    }
    for (int i = 0; i < args->argc; i++) {
        SftpWildcardArg *arg = &args->argv[i];
        sfree((void *)arg->name);
        sfree((void *)arg->wildcard);
    }
    sfree(args);
}

SftpWildcardArgs *sftpwcm_args_create(Sftp *sftp, int begin_arg, int end_arg, bool disable_wc)
{
    int argc = end_arg - begin_arg;
    bool result = true;
    SftpWildcardArgs *args = (SftpWildcardArgs *)snewn(sizeof(SftpWildcardArgs)-sizeof(SftpWildcardArg) + argc * sizeof(SftpWildcardArg), char);
    SftpWildcardArg *arg = args->argv;
    args->argc = argc;

    for (int current_arg = begin_arg; current_arg < end_arg; current_arg++, arg++) {
        const char *name = sftp->args.argv[current_arg];
        bool is_wc = false;
        if (!disable_wc) {
            char *unwcname = snewn(strlen(name)+1, char);
            is_wc = !wc_unescape(unwcname, name);
            sfree(unwcname);
        }

        /*
        * We don't handle multi-level wildcards; so we expect to find
        * a fully specified directory part, followed by a wildcard
        * after that.
        */
        if (is_wc) {
            const char *wildcard = stripslashes(name, false);

            char *unwcdir = dupstr(name);
            int len = wildcard - name;
            unwcdir[len] = '\0';
            if (len > 0 && unwcdir[len-1] == '/')
                unwcdir[len-1] = '\0';
            char *tmpdir = snewn(1 + len, char);
            bool check = wc_unescape(tmpdir, unwcdir);
            sfree(tmpdir);

            if (!check) {
                sfree(unwcdir);
                sftp_line_printf(sftp, SEAT_OUTPUT_STDERR, name, "Multiple-level wildcards are not supported in %s", utf8_arg);
                arg->name = NULL;
                arg->wildcard = NULL;
                result = false;
            } else {
                arg->name = sftp_utf8_to_line(sftp->line_codepage, sftp_get_absolute_path(sftp->pwd, unwcdir), sftp->seat);
                sfree(unwcdir);
                arg->wildcard = sftp_utf8_to_line(sftp->line_codepage, dupstr(wildcard), sftp->seat);
                if (!arg->name || !arg->wildcard) {
                    result = false;
                }
            }
        } else {
            arg->name = sftp_utf8_to_line(sftp->line_codepage, sftp_get_absolute_path(sftp->pwd, name), sftp->seat);
            arg->wildcard = NULL;
            if (!arg->name) {
                result = false;
            }
        }
    }
    if (!result) {
        sftpwcm_args_free(args);
        return NULL;
    }
    return args;
}

static bool sftpwcm_iterator_next_arg(SftpWildcardMatcherIterator* it, Sftp *sftp, SftpCmd *cmd)
{
    assert(!it->swcm && !it->cname);
    while (it->current_arg < it->args->argc) {
        SftpWildcardArg *arg = &it->args->argv[it->current_arg];
        if (arg->wildcard) {
            it->swcm = sftpwcm_begin(arg->name, arg->wildcard, sftp, cmd);
        } else {
            it->cname = arg->name;
            sftp_set_sending_backend(sftp);
            sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send(it->cname));
        }
        arg->name = NULL;
        arg->wildcard = NULL;
        it->current_arg++;
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
                sftp_line_printf(sftp, SEAT_OUTPUT_STDERR, it->cname, "unable to open %s: %s", utf8_arg, fxp_error());
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

void sftpwcm_iterator_init(SftpWildcardMatcherIterator* it, SftpWildcardArgs *args, void (*func)(const char *, Sftp *, SftpCmd *))
{
    it->args = args;
    it->current_arg = 0;
    it->swcm = NULL;
    it->cname = NULL; //line codepage
    it->func = func;
}

void sftpwcm_iterator_uninit(SftpWildcardMatcherIterator* it)
{
    if (it->swcm) {
        sftpwcm_free(it->swcm);
    }
    sftpwcm_args_free(it->args);
    sfree((void *)it->cname);
}
