#include "sftpcmd.h"
#include "sftputil.h"
#include "sftpfxp.h"
#include "sftpwcm.h"
#include "sftpunicode.h"

typedef struct SftpCmdChmod {
    SftpCmd cmd;
    SftpWildcardMatcherIterator it;
    unsigned attrs_clr, attrs_xor;
    const char *fname;
    unsigned oldperms, newperms;
} SftpCmdChmod;

static void send_stat(const char *fname, Sftp *sftp, SftpCmd *cmd)
{
    SftpCmdChmod *cmdchmod = container_of(cmd, SftpCmdChmod, cmd);

    cmdchmod->fname = fname;
    cmdchmod->oldperms = 0;
    cmdchmod->newperms = 0;
    sftp_set_sending_backend(sftp);
    sftpcmd_set_request(cmd, SSH_FXP_STAT, fxp_stat_send(fname));
}

static SftpCmd *sftpcmdchmod_init(Sftp *sftp)
{
    if (sftp->args.argc < 3) {
        sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: expects a mode specifier and a filename");
        return NULL;
    }

    unsigned attrs_clr = 0, attrs_xor = 0;
    const char *mode = sftp->args.argv[1];
    if (mode[0] >= '0' && mode[0] <= '9') {
        if (mode[strspn(mode, "01234567")]) {
            sftp_print(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: numeric file modes should"
                   " contain digits 0-7 only");
            return NULL;
        }
        attrs_clr = 07777;
        sscanf(mode, "%o", &attrs_xor);
        attrs_xor &= attrs_clr;
    } else {
        while (*mode) {
            const char *modebegin = mode;
            unsigned subset, perms;
            int action;

            subset = 0;
            while (*mode && *mode != ',' &&
                   *mode != '+' && *mode != '-' && *mode != '=') {
                switch (*mode) {
                  case 'u': subset |= 04700; break; /* setuid, user perms */
                  case 'g': subset |= 02070; break; /* setgid, group perms */
                  case 'o': subset |= 00007; break; /* just other perms */
                  case 'a': subset |= 06777; break; /* all of the above */
                  default:
                    sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s' contains unrecognised"
                           " user/group/other specifier '%c'",
                           (int)strcspn(modebegin, ","), modebegin, *mode);
                    return NULL;
                }
                mode++;
            }
            if (!*mode || *mode == ',') {
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s' is incomplete",
                       (int)strcspn(modebegin, ","), modebegin);
                return NULL;
            }
            action = *mode++;
            if (!*mode || *mode == ',') {
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s' is incomplete",
                       (int)strcspn(modebegin, ","), modebegin);
                return NULL;
            }
            perms = 0;
            while (*mode && *mode != ',') {
                switch (*mode) {
                  case 'r': perms |= 00444; break;
                  case 'w': perms |= 00222; break;
                  case 'x': perms |= 00111; break;
                  case 't': perms |= 01000; subset |= 01000; break;
                  case 's':
                    if ((subset & 06777) != 04700 &&
                        (subset & 06777) != 02070) {
                        sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s': set[ug]id bit should"
                               " be used with exactly one of u or g only",
                               (int)strcspn(modebegin, ","), modebegin);
                        return NULL;
                    }
                    perms |= 06000;
                    break;
                  default:
                    sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s' contains unrecognised"
                           " permission specifier '%c'",
                           (int)strcspn(modebegin, ","), modebegin, *mode);
                    return NULL;
                }
                mode++;
            }
            if (!(subset & 06777) && (perms &~ subset)) {
                sftp_printf(sftp->seat, SEAT_OUTPUT_STDERR, "chmod: file mode '%.*s' contains no user/group/other"
                       " specifier and permissions other than 't' ",
                       (int)strcspn(modebegin, ","), modebegin);
                return NULL;
            }
            perms &= subset;
            switch (action) {
              case '+':
                attrs_clr |= perms;
                attrs_xor |= perms;
                break;
              case '-':
                attrs_clr |= perms;
                attrs_xor &= ~perms;
                break;
              case '=':
                attrs_clr |= subset;
                attrs_xor |= perms;
                break;
            }
            if (*mode) mode++;         /* eat comma */
        }
    }

    SftpWildcardArgs *args = sftpwcm_args_create(sftp, 2, sftp->args.argc, false);
    if (args == NULL) {
        return NULL;
    }

    SftpCmdChmod *cmdchmod = snew(SftpCmdChmod);
    cmdchmod->attrs_clr = attrs_clr;
    cmdchmod->attrs_xor = attrs_xor;
    sftpwcm_iterator_init(&cmdchmod->it, args, send_stat);

    sftpcmd_clear_request(&cmdchmod->cmd);
    bool next = sftpwcm_iterator_next(&cmdchmod->it, sftp, &cmdchmod->cmd);
    assert(next);
    return &cmdchmod->cmd;
}

static bool sftpcmdchmod_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    SftpCmdChmod *cmdchmod = container_of(cmd, SftpCmdChmod, cmd);

    if (cmd->req_type == SSH_FXP_STAT) {
        struct fxp_attrs attrs;
        bool result = fxp_stat_recv(pktin, cmd->req, &attrs);
        sftpcmd_clear_request(cmd);

        if (!result || !(attrs.flags & SSH_FILEXFER_ATTR_PERMISSIONS)) {
            sftp_line_printf(sftp, SEAT_OUTPUT_STDERR, cmdchmod->fname, "get attrs for %s: %s", utf8_arg,
                   result ? "file permissions not provided" : fxp_error());
            return false;
        }

        attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS;   /* perms _only_ */
        cmdchmod->oldperms = attrs.permissions & 07777;
        attrs.permissions &= ~cmdchmod->attrs_clr;
        attrs.permissions ^= cmdchmod->attrs_xor;
        cmdchmod->newperms = attrs.permissions & 07777;

        if (cmdchmod->oldperms == cmdchmod->newperms) {
            return sftpwcm_iterator_next(&cmdchmod->it, sftp, cmd); /* no need to do anything! */
        }

        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_SETSTAT, fxp_setstat_send(cmdchmod->fname, attrs));
        return true;
    } else if (cmd->req_type == SSH_FXP_SETSTAT) {
        bool result = fxp_setstat_recv(pktin, cmd->req);
        sftpcmd_clear_request(cmd);

        if (!result) {
            sftp_line_printf(sftp, SEAT_OUTPUT_STDERR, cmdchmod->fname, "set attrs for %s: %s", utf8_arg, fxp_error());
            return false;
        }

        sftp_line_printf(sftp, SEAT_OUTPUT_STDOUT, cmdchmod->fname, "%s: %04o -> %04o", utf8_arg, cmdchmod->oldperms, cmdchmod->newperms);
        return sftpwcm_iterator_next(&cmdchmod->it, sftp, cmd);
    }
    return sftpwcm_iterator_pktin(&cmdchmod->it, sftp, cmd, pktin);
}

static void sftpcmdchmod_free(SftpCmd *cmd)
{
    SftpCmdChmod *cmdchmod = container_of(cmd, SftpCmdChmod, cmd);
    sftpwcm_iterator_uninit(&cmdchmod->it);
    sfree(cmdchmod);
}

static SftpCmdArgInfo sftpcmdchmod_get_arg_info(int file_arg_index)
{
    if (file_arg_index == 0) {
        return sftpcmd_get_arg_info(file_arg_index);
    }
    return (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_REMOTE, false, true};
}

const SftpCmdVtable sftpcmdchmod_vt = {
    .init = sftpcmdchmod_init,
    .free = sftpcmdchmod_free,
    .process_pkt = sftpcmdchmod_process_pkt,
    .get_arg_info = sftpcmdchmod_get_arg_info
};
