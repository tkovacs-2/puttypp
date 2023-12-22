/*
 * Small implementation of Seat and LogPolicy shared between window.c
 * and windlg.c.
 */

#define LOGEVENT_INITIAL_MAX 128
#define LOGEVENT_CIRCULAR_MAX 128

struct eventlog_stuff {
    char *events_initial[LOGEVENT_INITIAL_MAX];
    char *events_circular[LOGEVENT_CIRCULAR_MAX];
    int ninitial, ncircular, circular_first;
};

const SeatDialogPromptDescriptions *win_seat_prompt_descriptions(Seat *seat);

SeatPromptResult dlg_confirm_ssh_host_key(
    HWND hwnd, const char *host, int port, const char *keytype,
    char *keystr, SeatDialogText *text, HelpCtx helpctx);
SeatPromptResult dlg_confirm_weak_crypto_primitive(
    SeatDialogText *text);
SeatPromptResult dlg_confirm_weak_cached_hostkey(
    SeatDialogText *text);

void dlg_eventlog(eventlog_stuff *es, const char *string);
int dlg_askappend(Filename *filename);

#define PROT_CONPTY PROTOCOL_LIMIT
