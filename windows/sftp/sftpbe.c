#include "sftpbe.h"
#include "sftpcli.h"
#include "sftpcmd.h"
#include "sftpfxp.h"
#include "sftpcompletion.h"

extern const SftpCmdVtable sftpcompletion_readdir_vt;
extern const SftpCmdVtable sftpinit_vt;

static void prepare_conf(Sftp *sftp, Conf *conf)
{
    sftp->ssh_conf = conf_copy(conf);
    conf_set_int(sftp->ssh_conf, CONF_protocol, PROT_SSH);
    if ((conf_get_int(sftp->ssh_conf, CONF_sshprot) & ~1) != 2) {
        conf_set_int(sftp->ssh_conf, CONF_sshprot, 2);
    }
    conf_set_bool(sftp->ssh_conf, CONF_x11_forward, false);
    conf_set_bool(sftp->ssh_conf, CONF_agentfwd, false);
    conf_set_bool(sftp->ssh_conf, CONF_ssh_simple, true);
    {
        char *key;
        while ((key = conf_get_str_nthstrkey(sftp->ssh_conf, CONF_portfwd, 0)) != NULL)
            conf_del_str_str(sftp->ssh_conf, CONF_portfwd, key);
    }
    conf_set_str(sftp->ssh_conf, CONF_remote_cmd, "sftp");
    conf_set_bool(sftp->ssh_conf, CONF_ssh_subsys, true);
    conf_set_bool(sftp->ssh_conf, CONF_nopty, true);
    conf_set_str(sftp->ssh_conf, CONF_remote_cmd2,
                 "test -x /usr/lib/sftp-server &&"
                 " exec /usr/lib/sftp-server\n"
                 "test -x /usr/local/lib/sftp-server &&"
                 " exec /usr/local/lib/sftp-server\n"
                 "exec sftp-server");
    conf_set_bool(sftp->ssh_conf, CONF_ssh_subsys2, false);
}

static void execute(Sftp *sftp) {
    if (sftp->args.argv == NULL) {
        return;
    }
    const SftpCmdVtable *vt = sftpcmd_vt_from_name(sftp->args.argv[0]);
    if (vt == NULL) {
        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "Unknown command: %s", sftp->args.argv[0]);
        sftpargs_free(&sftp->args);
        return;
    }
    sftp->cmd = sftpcmd_init(vt, sftp);
    if (sftp->cmd == NULL) {
        sftpargs_free(&sftp->args);
        return;
    }
    sftp->cmd->vt = vt;
}

bool receive_pkt(Sftp *sftp, struct sftp_packet **pkt) {
    if (!sftp->receiving_pkt) {
        char x[4];
        if (!bufchain_try_fetch_consume(&sftp->received_data, x, 4)) {
            return false;
        }
        unsigned pktlen = GET_32BIT_MSB_FIRST(x);
        if (pktlen > (1<<20)) {
            *pkt = NULL;
            return true;
        }
        sftp->receiving_pkt = sftp_recv_prepare(pktlen);
        sftp->receiving_pkt_fetched = 0;
    }

    struct sftp_packet *p = sftp->receiving_pkt;
    sftp->receiving_pkt_fetched += bufchain_fetch_consume_up_to(&sftp->received_data,
                                      p->data+sftp->receiving_pkt_fetched,
                                      p->length-sftp->receiving_pkt_fetched);
    if (sftp->receiving_pkt_fetched != p->length) {
        return false;
    }
    sftp->receiving_pkt = NULL;
    sftp->receiving_pkt_fetched = 0;
    if (!sftp_recv_finish(p)) {
        sftp_pkt_free(p);
        *pkt = NULL;
        return true;
    }
    *pkt = p;
    return true;
}

static void clear_command(Sftp *sftp) {
    bool is_completion = sftp->cmd->vt == &sftpcompletion_readdir_vt;
    sftpcmd_free(sftp->cmd);
    sftp->cmd = NULL;
    sftp_free_pending_requests(sftp);
    if (!is_completion) {
        sftpargs_free(&sftp->args);
        sftpcli_start(sftp->cli, sftp->width, sftp->lpwd, sftp->pwd);
    }
}

static size_t sshseat_output(Seat *seat, SeatOutputType type, const void *data, size_t len)
{
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    if (type != SEAT_OUTPUT_STDOUT) {
        seat_output(sftp->seat, type, data, len);
        return 0;
    }

    bufchain_add(&sftp->received_data, data, len);
    struct sftp_packet *pkt;
    while (receive_pkt(sftp, &pkt)) {
        if (!pkt) {
            // connection close, malformed response
            return 0;
        }
        if (!sftp->cmd) { // unwanted response, no command is running
            sftp_pkt_free(pkt);
            return 0;
        }
        if (sftp->cmd->req) {
            if (sftp->cmd->req != sftp_find_request(pkt)) { // unwanted response, not for the current command
                sftp_pkt_free(pkt);
                return 0;
            }
        }

        if (!sftpcmd_process_pkt(sftp->cmd, sftp, pkt)) {
            clear_command(sftp);
        }
    }
    return 0;
}

static SeatPromptResult sshseat_get_userpass_input(Seat *seat, prompts_t *p)
{
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return sftp->seat->vt->get_userpass_input(sftp->seat, p);
}

static void sshseat_notify_session_started(Seat *seat)
{
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    sftp->cmd = sftpcmd_init(&sftpinit_vt, sftp);
    sftp->cmd->vt = &sftpinit_vt;
}

static void sshseat_notify_remote_exit(Seat *seat) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    seat_notify_remote_exit(sftp->seat);
}

static void sshseat_connection_fatal(Seat *seat, const char *msg) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    seat_connection_fatal(sftp->seat, msg);
}

static SeatPromptResult sshseat_confirm_ssh_host_key(
    Seat *seat, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx,
    void (*callback)(void *ctx, SeatPromptResult result), void *cbctx) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return sftp->seat->vt->confirm_ssh_host_key(sftp->seat, host, port, keytype, keystr, text, helpctx, callback, cbctx);
}

static SeatPromptResult sshseat_confirm_weak_crypto_primitive(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return sftp->seat->vt->confirm_weak_crypto_primitive(sftp->seat, text, callback, ctx);
}

static SeatPromptResult sshseat_confirm_weak_cached_hostkey(
    Seat *seat, SeatDialogText *text,
    void (*callback)(void *ctx, SeatPromptResult result), void *ctx) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return sftp->seat->vt->confirm_weak_cached_hostkey(sftp->seat, text, callback, ctx);
}

static StripCtrlChars *sshseat_stripctrl_new(
    Seat *seat, BinarySink *bs_out, SeatInteractionContext sic) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return seat_stripctrl_new(sftp->seat, bs_out, sic);
}

static const SeatDialogPromptDescriptions *sshseat_prompt_descriptions(Seat *seat) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    return seat_prompt_descriptions(sftp->seat);
}

static void sshseat_notify_remote_disconnect(Seat *seat) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    seat_notify_remote_disconnect(sftp->seat);
}

static void sshseat_set_trust_status(Seat *seat, bool trusted) {
    Sftp *sftp = container_of(seat, Sftp, sshseat);
    seat_set_trust_status(sftp->seat, trusted);
}

static const SeatVtable sshseat_vt = {
    .output = sshseat_output,
    .eof = nullseat_eof,
    .sent = nullseat_sent,
    .banner = nullseat_banner_to_stderr,
    .get_userpass_input = sshseat_get_userpass_input,
    .notify_session_started = sshseat_notify_session_started,
    .notify_remote_exit = sshseat_notify_remote_exit,
    .notify_remote_disconnect = sshseat_notify_remote_disconnect,
    .connection_fatal = sshseat_connection_fatal,
    .update_specials_menu = nullseat_update_specials_menu,
    .get_ttymode = nullseat_get_ttymode,
    .set_busy_status = nullseat_set_busy_status,
    .confirm_ssh_host_key = sshseat_confirm_ssh_host_key,
    .confirm_weak_crypto_primitive = sshseat_confirm_weak_crypto_primitive,
    .confirm_weak_cached_hostkey = sshseat_confirm_weak_cached_hostkey,
    .prompt_descriptions = sshseat_prompt_descriptions,
    .is_utf8 = nullseat_is_never_utf8,
    .echoedit_update = nullseat_echoedit_update,
    .get_x_display = nullseat_get_x_display,
    .get_windowid = nullseat_get_windowid,
    .get_window_pixel_size = nullseat_get_window_pixel_size,
    .stripctrl_new = sshseat_stripctrl_new,
    .set_trust_status = sshseat_set_trust_status,
    .can_set_trust_status = nullseat_can_set_trust_status_yes,
    .has_mixed_input_stream = nullseat_has_mixed_input_stream_no,
    .verbose = nullseat_verbose_yes,
    .interactive = nullseat_interactive_yes,
    .get_cursor_position = nullseat_get_cursor_position,
};

static char *sftpbe_init(const BackendVtable *vt, Seat *seat,
                      Backend **backend_handle, LogContext *logctx,
                      Conf *conf, const char *host, int port,
                      char **realhost, bool nodelay, bool keepalive)
{
    Sftp *sftp;
    sftp = snew(Sftp);
    memset(sftp, 0, sizeof(Sftp));

    sftp->cli = sftpcli_create(seat);
    sftp->completion = sftpcompletion_create(sftp);
    sftp_init_requests(sftp);

    sftp->seat = seat;
    sftp->backend.vt = vt;
    sftp->backend.interactor = NULL;

    sftp->sshseat.vt = &sftp->sshseat_vt;
    sftp->sshseat_vt = sshseat_vt;
    sftp->sshseat_vt.notify_remote_disconnect = seat->vt->notify_remote_disconnect;

    prepare_conf(sftp, conf);

    char *err = backend_init(&ssh_backend,
                       &sftp->sshseat, &sftp->ssh, logctx, sftp->ssh_conf,
                       host,
                       port,
                       realhost, 0,
                       keepalive);
    if (err != NULL) {
        return err;
    }
    sftp->backend.interactor = sftp->ssh->interactor;

    *backend_handle = &sftp->backend;
    return NULL;
}

static void sftpbe_free(Backend *be)
{
    Sftp *sftp = container_of(be, Sftp, backend);

    if (sftp->receiving_pkt) {
        sftp_pkt_free(sftp->receiving_pkt);
    }
    if (sftp->ssh) {
        backend_free(sftp->ssh);
    }
    if (sftp->ssh_conf) {
        conf_free(sftp->ssh_conf);
    }
    if (sftp->cmd) {
        sftpcmd_free(sftp->cmd);
        sftpargs_free(&sftp->args);
        sftp_free_pending_requests(sftp);
    }
    sftp_uninit_requests(sftp);
    sftpcompletion_free(sftp->completion);
    sftpcli_free(sftp->cli);
    sfree((void *)sftp->homedir);
    sfree((void *)sftp->pwd);
    sfree((void *)sftp->lpwd);
    sfree(sftp);
}

static void sftpbe_reconfig(Backend *be, Conf *conf)
{
}

static void sftpbe_send(Backend *be, const char *buf, size_t len)
{
    Sftp *sftp = container_of(be, Sftp, backend);

    if (sftp->cmd) {
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == 0x03) {
                clear_command(sftp);
                break;
            }
        }
        return;
    }
    if (sftpcompletion_is_paging_displayed(sftp->completion)) {
        for (size_t i = 0; i < len; i++) {
            char c = buf[i];
            if (c == 'y' || c == 'Y' || c == ' ') {
                sftpcompletion_continue_paging(sftp->completion, sftp->height);
                break;
            }
            if (c == 'n' || c == 'N' || c == '\x08' || c == '\x03' || c == 'q' || c == 'Q') {
                sftpcompletion_cancel_paging(sftp->completion);
                sftpcli_refresh(sftp->cli);
                break;
            }
            if (c == '\r' || c == '\n') {
                sftpcompletion_continue_paging(sftp->completion, 1);
                break;
            }
        }
        return;
    }

    SftpCliState state = sftpcli_feed(sftp->cli, buf, len);
    if (state == SFTPCLISTATE_CONTINUE) {
        if (sftpcompletion_is_paging(sftp->completion)) {
            sftpcompletion_cancel_paging(sftp->completion);
        }
        return;
    } else if (state == SFTPCLISTATE_COMPLETION) {
        const SftpCmdVtable *vt = sftpcompletion_start_completion(sftp->completion);
        if (vt != NULL) {
            sftp->cmd = sftpcmd_init(vt, sftp);
            sftp->cmd->vt = vt;
        }
        return;
    } else if (state == SFTPCLISTATE_COMPLETION_AGAIN) {
        if (sftpcompletion_is_paging(sftp->completion)) {
            sftpcompletion_continue_paging(sftp->completion, sftp->height);
        }
        return;
    }

    if (sftpcompletion_is_paging(sftp->completion)) {
        sftpcompletion_cancel_paging(sftp->completion);
    }
    seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
    if (state == SFTPCLISTATE_FINISH_CANCEL) {
        sftpcli_start(sftp->cli, sftp->width, sftp->lpwd, sftp->pwd);
    } else if (state == SFTPCLISTATE_FINISH) {
        sftpargs_parse(sftpcli_copy_line(sftp->cli, false), &sftp->args, false);
        execute(sftp);
        if (!sftp->cmd) {
            sftpcli_start(sftp->cli, sftp->width, sftp->lpwd, sftp->pwd);
        }
    } else if (state == SFTPCLISTATE_FINISH_EXIT) {
        seat_notify_remote_exit(sftp->seat);
    }
}

static size_t sftpbe_sendbuffer(Backend *be)
{
    Sftp *sftp = container_of(be, Sftp, backend);
    return sftpcli_get_unprocessed_feed(sftp->cli);
}

static void sftpbe_size(Backend *be, int width, int height)
{
    Sftp *sftp = container_of(be, Sftp, backend);
    sftp->width = width;
    sftp->height = height;
    if (sftpcompletion_is_paging(sftp->completion)) {
        sftpcompletion_cancel_paging(sftp->completion);
    }
    if (sftp->homedir && (!sftp->cmd || sftp->cmd->vt == &sftpcompletion_readdir_vt)) {
        sftpcli_change_columns(sftp->cli, width);
    }
}

static void sftpbe_special(Backend *be, SessionSpecialCode code, int arg)
{
}

static const SessionSpecial *sftpbe_get_specials(Backend *be)
{
    return NULL;
}

static bool sftpbe_connected(Backend *be)
{
    Sftp *sftp = container_of(be, Sftp, backend);
    return backend_connected(sftp->ssh);
}

static bool sftpbe_sendok(Backend *be)
{
    Sftp *sftp = container_of(be, Sftp, backend);
    return sftp->homedir;
}

static void sftpbe_unthrottle(Backend *be, size_t backlog)
{
}

static bool sftpbe_ldisc(Backend *be, int option)
{
    return false;
}

static void sftpbe_provide_ldisc(Backend *be, Ldisc *ldisc)
{
}

static int sftpbe_exitcode(Backend *be)
{
    return 0;
}

static int sftpbe_cfg_info(Backend *be)
{
    return 0;
}

const BackendVtable sftp_backend = {
    .init = sftpbe_init,
    .free = sftpbe_free,
    .reconfig = sftpbe_reconfig,
    .send = sftpbe_send,
    .sendbuffer = sftpbe_sendbuffer,
    .size = sftpbe_size,
    .special = sftpbe_special,
    .get_specials = sftpbe_get_specials,
    .connected = sftpbe_connected,
    .exitcode = sftpbe_exitcode,
    .sendok = sftpbe_sendok,
    .ldisc_option_state = sftpbe_ldisc,
    .provide_ldisc = sftpbe_provide_ldisc,
    .unthrottle = sftpbe_unthrottle,
    .cfg_info = sftpbe_cfg_info,
    .id = "sftp",
    .displayname_tc = "SFTP",
    .displayname_lc = "SFTP",
    .protocol = -1,
    .default_port = 22,
};
