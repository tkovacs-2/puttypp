#include "sftpcmd.h"
#include "sftpfxp.h"
#include "psftp.h"

static SftpCmd *sftpinit_init(Sftp *sftp)
{
    struct sftp_packet *pktout = sftp_pkt_init(SSH_FXP_INIT);
    put_uint32(pktout, SFTP_PROTO_VERSION);
    sftp_set_sending_backend(sftp);
    sftp_send_prepare(pktout);
    sftp_senddata(pktout->data, pktout->length);
    sftp_pkt_free(pktout);

    SftpCmd *cmd = snew(SftpCmd);
    sftpcmd_clear_request(cmd);
    sftpcmd_set_request(cmd, SSH_FXP_INIT, NULL);
    return cmd;
}

static bool sftpinit_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pktin)
{
    if (!cmd->req) {
        if (pktin->type != SSH_FXP_VERSION) {
            seat_connection_fatal(sftp->seat, "Fatal: unable to initialise SFTP: did not receive FXP_VERSION");
            sftp_pkt_free(pktin);
            return false;
        }
        unsigned long remotever = get_uint32(pktin);
        if (get_err(pktin)) {
            seat_connection_fatal(sftp->seat, "Fatal: unable to initialise SFTP: malformed FXP_VERSION packet");
            sftp_pkt_free(pktin);
            return false;
        }
        if (remotever > SFTP_PROTO_VERSION) {
            seat_connection_fatal(sftp->seat, "Fatal: unable to initialise SFTP: remote protocol is more advanced than we support");
            sftp_pkt_free(pktin);
            return false;
        }
        sftp_pkt_free(pktin);
        sftp_set_sending_backend(sftp);
        sftpcmd_set_request(cmd, SSH_FXP_REALPATH, fxp_realpath_send("."));
        return true;
    }
    sftp->homedir = fxp_realpath_recv(pktin, cmd->req);
    sftpcmd_clear_request(cmd);

    if (!sftp->homedir) {
        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "Warning: failed to resolve home directory: %s", fxp_error());
        sftp->homedir = dupstr(".");
    } else {
        sftpcmd_print_pwd(sftp->seat, sftp->homedir);
    }
    sftp->pwd = dupstr(sftp->homedir);
    sftp->lpwd = psftp_getcwd();
    return false;
}

static void sftpinit_free(SftpCmd *cmd)
{
    sfree(cmd);
}

const SftpCmdVtable sftpinit_vt = {
    .init = sftpinit_init,
    .free = sftpinit_free,
    .process_pkt = sftpinit_process_pkt,
    .get_arg_info = NULL
};
