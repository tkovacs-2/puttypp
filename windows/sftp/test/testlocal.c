#include "testlocal.h"
#include "psftp.h"

extern const BackendVtable sftp_backend;
static const SeatVtable testseat_vt;

void delete_file(const char *name);
void delete_directory(const char *name);

static void delete_directory_recurse(const char *name)
{
    const char *errmsg;
    char *filename;
    DirHandle *h = open_directory(name, &errmsg);
    if (!h) {
        return;
    }
    while ((filename = read_filename(h))) {
        char *path = dir_file_cat(name, filename);
        if (file_type(path) == FILE_TYPE_DIRECTORY) {
            delete_directory_recurse(path);
        } else {
            delete_file(path);
        }
        sfree(filename);
        sfree(path);
     }
     close_directory(h);
     delete_directory(name);
}

static void init_output(TestOutput *o)
{
    memset(o, 0, sizeof(TestOutput));
}

static void uninit_output(TestOutput *o)
{
    for (size_t i = 0; i < o->size; i++) {
        sfree((void *)o->lines[i]);
    }
    sfree(o->lines);
    sfree((void *)o->pending_line);
}

static void append_output(TestOutput *o, const char *data, size_t len)
{
    size_t i = 0;
    while (len > 0) {
        while (i < len && data[i] != '\r' && data[i] != '\n') {
            i++;
        }
        if (i > 0) {
            o->pending_line = srealloc(o->pending_line, o->pending_line_len+i+1);
            memcpy(o->pending_line+o->pending_line_len, data, i);
            o->pending_line_len += i;
        }
        if (i < len) {
            if (o->pending_line) {
                o->pending_line[o->pending_line_len] = 0;
                sgrowarray(o->lines, o->capacity, o->size);
                o->lines[o->size++] = o->pending_line;
                o->pending_line = NULL;
                o->pending_line_len = 0;
            }
            i++;
        }
        data += i;
        len -= i;
        i=0;
    }
}

void testlocal_init(TestLocal *tl, TestRemote *tr, const char *line_codepage)
{
    char *local_dir = "l" "\xc3\xb6\xe4\xbd\xa0" "cal";
    delete_directory_recurse(local_dir);
    create_directory(local_dir);
    psftp_lcd(local_dir);

    tl->testseat.vt = &testseat_vt;
    init_output(&tl->output);
    init_output(&tl->error);
    tl->called_seat_function = SF_NONE;
    tl->allow_cli_output = false;

    Conf *conf = conf_new();
    conf_set_int(conf, CONF_sshprot, 2);
    conf_set_str(conf, CONF_line_codepage, line_codepage);
    backend_init(&sftp_backend, &tl->testseat, &tl->sftp, NULL, conf,
                 (const char *)tr, 0, NULL, 0, false);
    conf_free(conf);
    backend_size(tl->sftp, 80, 1);

    testremote_startsession(tr);
    testremote_process_request(tr, testremote_get_request(tr)); // SSH_FXP_INIT
    testremote_process_request(tr, testremote_get_request(tr)); // SSH_FXP_REALPATH
}

void testlocal_uninit(TestLocal *tl)
{
    backend_free(tl->sftp);
    psftp_lcd("..");
    uninit_output(&tl->output);
    uninit_output(&tl->error);
}

void testlocal_execute(TestLocal *tl, const char *command)
{
    printf("---- Command: %s\n", command);
    fflush(stdout);
    uninit_output(&tl->output);
    uninit_output(&tl->error);
    init_output(&tl->output);
    init_output(&tl->error);
    backend_send(tl->sftp, command, strlen(command));
    backend_send(tl->sftp, "\r", 1);
}

void testlocal_add_file(TestLocal *, const char *name, size_t size)
{
    WFile *h = open_new_file(name, 0644);
    if (!h) {
        return;
    }
    if (size > 0) {
        static unsigned char buffer[32768] = {0};
        static unsigned char end_mark = 0xaa;
        size--;
        while (size > sizeof(buffer)) {
            write_to_file(h, buffer, sizeof(buffer));
            size -= sizeof(buffer);
        }
        write_to_file(h, buffer, size);
        write_to_file(h, &end_mark, 1);
    }
    close_wfile(h);
}

void testlocal_add_dir(TestLocal *, const char *name)
{
    create_directory(name);
}

bool testlocal_check_file(TestLocal *, const char *name)
{
    uint64_t s;
    RFile *h = open_existing_file(name, &s, NULL, NULL, NULL);
    if (!h) {
        return false;
    }
    close_rfile(h);
    return true;
}

bool testlocal_check_dir(TestLocal *, const char *name)
{
    const char *errmsg;
    DirHandle *h = open_directory(name, &errmsg);
    if (!h) {
        return false;
    }
    close_directory(h);
    return true;
}

size_t testlocal_check_size(TestLocal *, const char *name)
{
    uint64_t s;
    RFile *h = open_existing_file(name, &s, NULL, NULL, NULL);
    if (!h) {
        return 0;
    }
    close_rfile(h);
    return s;
}

bool testlocal_check_create_size(TestLocal *, const char *name, size_t size)
{
    if (size == 0) {
        return false;
    }
    uint64_t s;
    RFile *h = open_existing_file(name, &s, NULL, NULL, NULL);
    if (!h) {
        return false;
    }
    unsigned char t = 0;
    if (s >= size) {
        seek_file((WFile *)h, size-1, FROM_START);
        read_from_file(h, &t, 1);
    }
    close_rfile(h);
    return (t == 0xaa);
}

const char *testlocal_find_output(TestOutput *o, const char *pattern, bool exact_match)
{
    for (size_t i=0; i<o->size; i++) {
        if (exact_match) {
            if (strcmp(o->lines[i], pattern) == 0) {
                return o->lines[i];
            }
        } else {
            if (strstr(o->lines[i], pattern)) {
                return o->lines[i];
            }
        }
    }
    return NULL;
}

bool testlocal_empty_output(TestOutput *o)
{
    return o->size == 0;
}

void testlocal_allow_cli_output(TestLocal *tl, bool allow)
{
    if (tl->allow_cli_output && !allow) {
        printf("\n");
    }
    tl->allow_cli_output = allow;
}

void testlocal_clear_output(TestLocal *tl)
{
    uninit_output(&tl->output);
    uninit_output(&tl->error);
    init_output(&tl->output);
    init_output(&tl->error);
}

static size_t testseat_output(Seat *seat, SeatOutputType type, const void *data, size_t len)
{
    TestLocal *tl = container_of(seat, TestLocal, testseat);
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);

    if (sftp->args.argv == NULL && !tl->allow_cli_output) {
        return len;
    }
    if (type == SEAT_OUTPUT_STDOUT) {
        append_output(&tl->output, data, len);
    } else if (type == SEAT_OUTPUT_STDERR) {
        append_output(&tl->error, data, len);
    }
    fwrite(data, 1, len, stdout);
    fflush(stdout);
    return len;
}

static void testseat_notify_remote_exit(Seat *seat)
{
    TestLocal *tl = container_of(seat, TestLocal, testseat);
    tl->called_seat_function = SF_NOTIFY_REMOTE_EXIT;
}

static void testseat_connection_fatal(Seat *seat, const char *msg)
{
    TestLocal *tl = container_of(seat, TestLocal, testseat);
    tl->called_seat_function = SF_CONNECTION_FATAL;
    testseat_output(seat, SEAT_OUTPUT_STDERR, msg, strlen(msg));
    testseat_output(seat, SEAT_OUTPUT_STDERR, "\r\n", 2);
}

static void testseat_set_trust_status(Seat *seat, bool)
{
}

static const SeatVtable testseat_vt = {
    .output = testseat_output,
    .eof = NULL,
    .sent = NULL,
    .banner = NULL,
    .get_userpass_input = NULL,
    .notify_session_started = NULL,
    .notify_remote_exit = testseat_notify_remote_exit,
    .notify_remote_disconnect = NULL,
    .connection_fatal = testseat_connection_fatal,
    .update_specials_menu = NULL,
    .get_ttymode = NULL,
    .set_busy_status = NULL,
    .confirm_ssh_host_key = NULL,
    .confirm_weak_crypto_primitive = NULL,
    .confirm_weak_cached_hostkey = NULL,
    .prompt_descriptions = NULL,
    .is_utf8 = NULL,
    .echoedit_update = NULL,
    .get_x_display = NULL,
    .get_windowid = NULL,
    .get_window_pixel_size = NULL,
    .stripctrl_new = NULL,
    .set_trust_status = testseat_set_trust_status,
    .can_set_trust_status = NULL,
    .has_mixed_input_stream = NULL,
    .verbose = NULL,
    .interactive = NULL,
    .get_cursor_position = NULL
};
