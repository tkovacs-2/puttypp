#include "testremote.h"

void sftp_clear_sending_backend();

struct TestRemoteFile {
    TestRemoteFile *parent;
    const char *name;
    struct fxp_attrs attrs;
    size_t size;
    bool is_dir;
    TestRemoteFile **dir_content;
    size_t capacity;
    size_t readdir_current;
    size_t create_size;
};

static void unlink_file(TestRemoteFile *file)
{
    TestRemoteFile *parent = file->parent;
    if (!parent) {
        return;
    }
    size_t i = 0;
    for (; i<parent->size; i++) {
        if (parent->dir_content[i] == file) {
            break;
        }
    }
    if (i == parent->size) {
        return;
    }
    for (size_t j=i+1; j<parent->size; j++) {
        parent->dir_content[j-1] = parent->dir_content[j];
    }
    parent->size--;
    if (parent->readdir_current > i) {
        parent->readdir_current--;
    }
}

static void link_file(TestRemoteFile *parent, TestRemoteFile *file)
{
    sgrowarray(parent->dir_content, parent->capacity, parent->size);
    parent->dir_content[parent->size++] = file;
    file->parent = parent;
}

static void free_file(TestRemoteFile *file);

static void free_dir_content(TestRemoteFile *file)
{
    assert(file->is_dir);
    for (size_t i=0; i<file->size; i++) {
        free_file(file->dir_content[i]);
    }
    sfree(file->dir_content);
}

static void free_file(TestRemoteFile *file)
{
    if (file->is_dir) {
        free_dir_content(file);
    }
    sfree((void *)file->name);
    sfree(file);
}

static const char *dup(const char *begin, const char *end)
{
    char *s = snewn(end-begin+1, char);
    char *d = s;
    while (begin != end) {
        *d++ = *begin++;
    }
    *d = 0;
    return s;
}

static bool cmp(const char *begin, const char *end, const char *s)
{
    while ((begin != end || *s != 0) && *begin == *s) {
        s++;
        begin++;
    }
    return (begin == end && *s == 0);
}

static TestRemoteFile *find_file_generic(TestRemoteFile *parent, const char *name, size_t length, bool create)
{
    int begin = 0;
    while (begin < length && name[begin]) {
        if (name[begin] == '/') {
            begin++;
            continue;
        }
        int end = begin;
        while (end < length && name[end] && name[end] != '/') {
            end++;
        }
        if (begin == end || cmp(&name[begin], &name[end], ".")) {
            begin = end;
            continue;
        } else if (cmp(&name[begin], &name[end], "..")) {
            parent = parent->parent ? parent->parent : parent;
            begin = end;
            continue;
        } else {
            if (parent->is_dir) {
                for (size_t i=0; i<parent->size; i++) {
                    if (cmp(&name[begin], &name[end], parent->dir_content[i]->name)) {
                        return find_file_generic(parent->dir_content[i], &name[end], length-end, create);
                    }
                }
            }
            if (!create) {
                return NULL;
            }
            if (!parent->is_dir) {
                parent->is_dir = true;
                parent->size = 0;
                parent->create_size = 0;
                PUT_PERMISSIONS(parent->attrs, parent->attrs.permissions|0111);
            }
            TestRemoteFile *file = snew(TestRemoteFile);
            file->name = dup(&name[begin], &name[end]);
            file->is_dir = false;
            file->dir_content = NULL;
            file->size = 0;
            file->create_size = 0;
            file->capacity = 0;
            memset(&file->attrs, 0, sizeof(struct fxp_attrs));
            PUT_PERMISSIONS(file->attrs, 0644);
            link_file(parent, file);
            return find_file_generic(file, &name[end], length-end, true);
        }
    }
    return parent;
}

static TestRemoteFile *find_file_ptrlen(TestRemoteFile *parent, ptrlen name, bool create)
{
    return find_file_generic(parent, name.ptr, name.len, create);
}

static TestRemoteFile *find_file(TestRemoteFile *parent, const char *name, bool create)
{
    return find_file_generic(parent, name, INT_MAX, create);
}

static TestRemoteFile *get_find_parent(TestRemote *tr, const char *name)
{
    return (*name == '/' ? tr->root : tr->home);
}

static TestRemoteFile *get_find_parent_ptrlen(TestRemote *tr, ptrlen name)
{
    return (name.len == 0 ? tr->home : get_find_parent(tr, name.ptr));
}

static struct fxp_attrs get_attrs(TestRemoteFile *file)
{
    struct fxp_attrs attrs = file->attrs;
    attrs.flags |= SSH_FILEXFER_ATTR_SIZE;
    attrs.size = file->size;
    if (file->is_dir && (attrs.flags&SSH_FILEXFER_ATTR_PERMISSIONS)) {
        attrs.permissions |= PERMS_DIRECTORY;
    }
    return attrs;
}

static void set_attrs(TestRemoteFile *file, struct fxp_attrs attrs)
{
    if (attrs.flags&SSH_FILEXFER_ATTR_PERMISSIONS) {
        PUT_PERMISSIONS(file->attrs, attrs.permissions);
    }
}

const struct SftpServerVtable srv_vt;

void testremote_init(TestRemote *tr)
{
    tr->srv.vt = &srv_vt;
    bufchain_init(&tr->received_data);
    sftp_clear_sending_backend();
    tr->root = snew(TestRemoteFile);
    tr->root->parent = NULL;
    tr->root->name = NULL;
    tr->root->size = 0;
    tr->root->is_dir = true;
    tr->root->dir_content = NULL;
    tr->root->capacity = 0;
    memset(&tr->root->attrs, 0, sizeof(struct fxp_attrs));
    PUT_PERMISSIONS(tr->root->attrs, 0755);
    tr->home = find_file(tr->root, "/sftp", true);
    tr->home->size = 0;
    tr->home->is_dir = true;
    PUT_PERMISSIONS(tr->home->attrs, 0755);
}

void testremote_uninit(TestRemote *tr)
{
    free_file(tr->root);
    testremote_drop_requests(tr);
}

void testremote_add_file(TestRemote *tr, const char *name, size_t size)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, true);
    if (file->is_dir) {
        free_dir_content(file);
        file->dir_content = NULL;
        file->capacity = 0;
        file->is_dir = false;
    }
    PUT_PERMISSIONS(file->attrs, 0644);
    file->size = size;
    file->create_size = size;
}

void testremote_add_dir(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, true);
    if (!file->is_dir) {
        file->size = 0;
        file->is_dir = true;
    }
    PUT_PERMISSIONS(file->attrs, 0755);
}

bool testremote_check_file(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (!file || file->is_dir) {
        return false;
    }
    return true;
}

bool testremote_check_dir(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (!file || !file->is_dir) {
        return false;
    }
    return true;
}

void testremote_set_permissions(TestRemote *tr, const char *name, unsigned long permissions)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (file) {
        PUT_PERMISSIONS(file->attrs, permissions);
    }
}

unsigned long testremote_check_permissions(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (!file) {
        return 0;
    }
    return GET_PERMISSIONS(file->attrs, 0);
}

size_t testremote_check_size(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (!file || file->is_dir) {
        return 0;
    }
    return file->size;
}

size_t testremote_check_create_size(TestRemote *tr, const char *name)
{
    TestRemoteFile *file = find_file(get_find_parent(tr, name), name, false);
    if (!file || file->is_dir) {
        return 0;
    }
    return file->create_size;
}

void testremote_startsession(TestRemote *tr)
{
    seat_notify_session_started(tr->client_seat);
}

void testremote_drop_requests(TestRemote *tr)
{
    bufchain_clear(&tr->received_data);
}

struct sftp_packet *testremote_get_request(TestRemote *tr)
{
    char x[4];
    if (!bufchain_try_fetch_consume(&tr->received_data, x, 4)) {
        return NULL;
    }
    unsigned pktlen = GET_32BIT_MSB_FIRST(x);
    struct sftp_packet *pkt = sftp_recv_prepare(pktlen);
    if (bufchain_fetch_consume_up_to(&tr->received_data, pkt->data, pkt->length) != pkt->length) {
        sftp_pkt_free(pkt);
        return NULL;
    }
    sftp_recv_finish(pkt);
    sftp_clear_sending_backend();
    return pkt;
}

void testremote_process_request(TestRemote *tr, struct sftp_packet *req)
{
    struct sftp_packet *reply = sftp_handle_request(&tr->srv, req);
    sftp_pkt_free(req);
    sftp_send_prepare(reply);
    seat_output(tr->client_seat, SEAT_OUTPUT_STDOUT, reply->data, reply->length);
    sftp_pkt_free(reply);
}

void testremote_process(TestRemote *tr)
{
    struct sftp_packet *req;
    while ((req = testremote_get_request(tr)) != NULL) {
        testremote_process_request(tr, req);
    }
}

void testremote_connection_fatal(TestRemote *tr)
{
    seat_connection_fatal(tr->client_seat, "test connection fatal");
}

static void srv_realpath(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file or directory");
        return;
    }
    if (file == tr->root) {
        ptrlen t = {"/", 1};
        fxp_reply_simple_name(reply, t);
        return;
    }

    size_t full_length = 0;
    TestRemoteFile *f = file;
    do {
        full_length += strlen(f->name)+1;
        f = f->parent;
    } while (f->parent != NULL);

    char *realpath = snewn(full_length+1, char);
    char *d = realpath+full_length;
    f = file;
    do {
        size_t l = strlen(f->name);
        d -= l;
        memcpy(d, f->name, l);
        *--d = '/';
        f = f->parent;
    } while (f->parent != NULL);

    ptrlen t = {realpath, full_length};
    fxp_reply_simple_name(reply, t);
    sfree(realpath);
}

static void srv_open(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path, unsigned flags, struct fxp_attrs attrs)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (file) {
        if (file->is_dir) {
            fxp_reply_error(reply, SSH_FX_FAILURE, "path is a directory");
            return;
        }
        if ((flags&(SSH_FXF_TRUNC|SSH_FXF_CREAT)) == (SSH_FXF_TRUNC|SSH_FXF_CREAT)) {
            file->size = 0;
            file->create_size = 0;
        }
    } else {
        if (flags&SSH_FXF_CREAT) {
            const char *name = path.ptr;
            size_t end = path.len;
            size_t begin = end;
            while (begin >= 0 && name[begin] != '/') {
                begin--;
            }
            if (begin == end) {
                fxp_reply_error(reply, SSH_FX_FAILURE, "path is a directory");
                return;
            }
            ptrlen parent_name = {name, begin};
            TestRemoteFile *parent = find_file_ptrlen(get_find_parent(tr, name), parent_name, false);
            if (!parent || !parent->is_dir) {
                fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file");
                return;
            }
            ptrlen file_name = {name+begin, end-begin};
            file = find_file_ptrlen(parent, file_name, true);
        }
    }
    if (!file) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file");
        return;
    }
    ptrlen handle = {&file, sizeof(file)};
    fxp_reply_handle(reply, handle);
}

static void srv_opendir(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file || !file->is_dir) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such directory");
        return;
    }
    file->readdir_current = 0;
    ptrlen handle = {&file, sizeof(file)};
    fxp_reply_handle(reply, handle);
}

static void srv_close(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle)
{
    TestRemoteFile *file = *((TestRemoteFile **)handle.ptr);
    if (file->is_dir) {
        file->readdir_current = 0;
    }
    fxp_reply_ok(reply);
}

static void srv_mkdir(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path, struct fxp_attrs attrs)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    const char *name = path.ptr;
    size_t end = path.len;
    size_t begin = end;
    while (end > 0 && name[end-1] == '/') {
        end--;
    }
    while (begin >= 0 && name[begin] != '/') {
        begin--;
    }
    ptrlen parent_name = {name, begin};
    TestRemoteFile *parent = find_file_ptrlen(get_find_parent(tr, name), parent_name, false);
    if (!parent || !parent->is_dir) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "parent is not a directory");
        return;
    }
    ptrlen file_name = {name+begin, path.len-begin};
    TestRemoteFile *file = find_file_ptrlen(parent, file_name, false);
    if (file) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "path already exists");
        return;
    }
    file = find_file_ptrlen(parent, file_name, true);
    file->is_dir = true;
    fxp_reply_ok(reply);
}

static void srv_rmdir(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file || !file->is_dir || file->size != 0) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "path doesn't exist, not a directory or not empty");
        return;
    }
    unlink_file(file);
    free_file(file);
    fxp_reply_ok(reply);
}

static void srv_remove(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file || file->is_dir) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "path doesn't exist or directory");
        return;
    }
    unlink_file(file);
    free_file(file);
    fxp_reply_ok(reply);
}

static void srv_rename(SftpServer *srv, SftpReplyBuilder *reply, ptrlen srcpath, ptrlen dstpath)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *srcfile = find_file_ptrlen(get_find_parent_ptrlen(tr, srcpath), srcpath, false);
    if (!srcfile) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file or directory");
        return;
    }
    const char *name = dstpath.ptr;
    size_t end = dstpath.len;
    if (srcfile->is_dir) {
        while (end > 0 && name[end-1] == '/') {
            end--;
        }
    }
    while (end > 0 && name[end-1] != '/') {
        end--;
    }
    ptrlen parent_name = {name, end};
    TestRemoteFile *parent = find_file_ptrlen(get_find_parent(tr, name), parent_name, false);
    if (!parent || !parent->is_dir) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "parent is not a directory");
        return;
    }
    ptrlen file_name = {name+end, dstpath.len-end};
    TestRemoteFile *file = find_file_ptrlen(parent, file_name, false);
    if (file) {
        fxp_reply_error(reply, SSH_FX_FAILURE, "destination already exists");
        return;
    }
    unlink_file(srcfile);
    link_file(parent, srcfile);
    sfree((void *)srcfile->name);
    srcfile->name = dup(file_name.ptr, file_name.ptr+file_name.len);
    fxp_reply_ok(reply);
}

static void srv_stat(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path, bool follow_symlinks)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file or directory");
        return;
    }
    fxp_reply_attrs(reply, get_attrs(file));
}

static void srv_fstat(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle)
{
    TestRemoteFile *file = *((TestRemoteFile **)handle.ptr);
    fxp_reply_attrs(reply, get_attrs(file));
}

static void srv_setstat(SftpServer *srv, SftpReplyBuilder *reply, ptrlen path, struct fxp_attrs attrs)
{
    TestRemote *tr = container_of(srv, TestRemote, srv);
    TestRemoteFile *file = find_file_ptrlen(get_find_parent_ptrlen(tr, path), path, false);
    if (!file) {
        fxp_reply_error(reply, SSH_FX_NO_SUCH_FILE, "no such file or directory");
        return;
    }
    set_attrs(file, attrs);
    fxp_reply_ok(reply);
}

static void srv_fsetstat(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle, struct fxp_attrs attrs)
{
    TestRemoteFile *file = *((TestRemoteFile **)handle.ptr);
    set_attrs(file, attrs);
    fxp_reply_ok(reply);
}

static void srv_read(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle, uint64_t offset, unsigned length)
{
    TestRemoteFile *file = *((TestRemoteFile **)handle.ptr);
    if (offset >= file->size) {
      fxp_reply_error(reply, SSH_FX_EOF, "");
      return;
    }
    static const unsigned char buffer[32768] = {0};
    ptrlen data = {&buffer, min(length, file->size-offset)};
    assert(data.len <= sizeof(buffer));
    fxp_reply_data(reply, data);
}

static void srv_write(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle, uint64_t offset, ptrlen data)
{
    TestRemoteFile *file = *((TestRemoteFile **)handle.ptr);
    if (offset + data.len > file->size) {
        file->size = offset + data.len;
    }
    fxp_reply_ok(reply);
}

static void srv_readdir(SftpServer *srv, SftpReplyBuilder *reply, ptrlen handle, int max_entries, bool omit_longname)
{
    TestRemoteFile *dir = *((TestRemoteFile **)handle.ptr);
    if (dir->readdir_current >= dir->size) {
        fxp_reply_error(reply, SSH_FX_EOF, "");
        return;
    }
    fxp_reply_name_count(reply, 1);
    TestRemoteFile *file = dir->dir_content[dir->readdir_current++];

    static const char * const bits[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    static char t[1024];
    ptrlen name = {file->name, strlen(file->name)};
    ptrlen longname = {t, 0};
    if (!omit_longname) {
        longname.len = snprintf(t, sizeof(t), "%c%s%s%s %5d %s", (file->is_dir ? 'd': '-'), bits[(file->attrs.permissions&0700)>>6], bits[(file->attrs.permissions&0070)>>3], bits[(file->attrs.permissions&0007)], file->size, file->name);
    }
    fxp_reply_full_name(reply, name, longname, get_attrs(file));
}

const struct SftpServerVtable srv_vt = {
    .new = NULL,
    .free = NULL,
    .realpath = srv_realpath,
    .open = srv_open,
    .opendir = srv_opendir,
    .close = srv_close,
    .mkdir = srv_mkdir,
    .rmdir = srv_rmdir,
    .remove = srv_remove,
    .rename = srv_rename,
    .stat = srv_stat,
    .fstat = srv_fstat,
    .setstat = srv_setstat,
    .fsetstat = srv_fsetstat,
    .read = srv_read,
    .write = srv_write,
    .readdir = srv_readdir
};

static char *dummyssh_init(const BackendVtable *vt, Seat *seat,
                      Backend **backend_handle, LogContext *logctx,
                      Conf *conf, const char *host, int port,
                                char **realhost, bool nodelay, bool keepalive)
{
    TestRemote *tr = (TestRemote *)host;
    tr->client_seat = seat;
    tr->dummyssh.vt = vt;
    tr->dummyssh.interactor = NULL;
    *backend_handle = &tr->dummyssh;
    return NULL;
}

static void dummyssh_free(Backend *be)
{
}

static void dummyssh_send(Backend *be, const char *buf, size_t len)
{
    TestRemote *tr = container_of(be, TestRemote, dummyssh);
    bufchain_add(&tr->received_data, buf, len);
}

static size_t dummyssh_sendbuffer(Backend *be)
{
    TestRemote *tr = container_of(be, TestRemote, dummyssh);
    return bufchain_size(&tr->received_data);
}

static bool dummyssh_connected(Backend *be)
{
    return true;
}

const BackendVtable ssh_backend = {
    .init = dummyssh_init,
    .free = dummyssh_free,
    .reconfig = NULL,
    .send = dummyssh_send,
    .sendbuffer = dummyssh_sendbuffer,
    .size = NULL,
    .special = NULL,
    .get_specials = NULL,
    .connected = dummyssh_connected,
    .exitcode = NULL,
    .sendok = NULL,
    .ldisc_option_state = NULL,
    .provide_ldisc = NULL,
    .unthrottle = NULL,
    .cfg_info = NULL,
    .id = "dummyssh",
    .displayname_tc = "DummySsh",
    .displayname_lc = "DummySsh",
    .protocol = PROT_SSH,
    .default_port = 0,
};
