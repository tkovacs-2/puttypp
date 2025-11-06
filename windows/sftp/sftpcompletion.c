#include "sftpcmd.h"
#include "sftpcli.h"
#include "sftpcompletion.h"
#include "psftp.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct Paging {
    const SftpCompletionName *names;
    size_t nnames;
    size_t nvisible;
    bool dir_only;
    size_t next_index;
    size_t col_width;
    size_t ncols;
    bool single_column;
} Paging;

typedef struct SftpCompletion {
    Sftp *sftp;
    const SftpCompletionName *command_cache;
    size_t command_cache_size;
    char local_path_separator;
    char remote_path_separator;
    const SftpCompletionName *remote_cache;
    size_t remote_cache_size;
    const char *remote_path;
    const char *remote_ctx_filename;
    SftpCmdArgInfo remote_ctx_arg_info;
    bool is_paging;
    bool is_paging_local;
    Paging paging;
} SftpCompletion;

extern const SftpCmdVtable sftpcompletion_readdir_vt;
const char *get_absolute_path(const char *pwd, const char *name);
char get_path_separator();

static void free_name_array(const SftpCompletionName *names, size_t nnames)
{
    for (size_t i = 0; i < nnames; i++) {
        sfree((void *)names[i].name);
    }
    sfree((void *)names);
}

static void free_remote_cache(SftpCompletion *completion)
{
    free_name_array(completion->remote_cache, completion->remote_cache_size);
    completion->remote_cache = NULL;
    completion->remote_cache_size = 0;
    sfree((void *)completion->remote_path);
    completion->remote_path = NULL;
}

/*
 * Bsearch a[from .. n) by strncmp(name, prefix, plen).
 * If first_mismatch is false: return smallest i with c >= 0, or n if all c < 0.
 * If first_mismatch is true: return smallest j in [from, n) with c != 0, or n if all c == 0.
 */
static size_t prefix_bsearch(const SftpCompletionName *a, size_t n, const char *prefix, size_t plen, size_t from, bool first_mismatch)
{
    size_t lo = from, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strncmp(a[mid].name, prefix, plen);
        if ((first_mismatch && c == 0) || (!first_mismatch && c < 0)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void shrink_names_to_prefix(const SftpCompletionName **names, size_t *nnames, const char *prefix)
{
    const SftpCompletionName *a = *names;
    size_t n = *nnames;
    const size_t plen = strlen(prefix);
    if (plen == 0) {
        return;
    }
    size_t lo = prefix_bsearch(a, n, prefix, plen, 0, false);
    if (lo == n || strncmp(a[lo].name, prefix, plen) != 0) {
        *nnames = 0;
        return;
    }
    size_t end = prefix_bsearch(a, n, prefix, plen, lo, true);
    *names = a + lo;
    *nnames = end - lo;
}

static size_t extend_prefix(const SftpCompletionName *names, size_t nnames, size_t prefix_length, size_t base_index, bool dir_only)
{
    assert(base_index < nnames);
    for (size_t p = 0;; p++) {
        char c = names[base_index].name[prefix_length + p];
        for (size_t i = 0; i < nnames; i++) {
            if (dir_only && !names[i].is_dir) {
                continue;
            }
            if (names[i].name[prefix_length + p] != c) {
                return p;
            }
        }
        if (c == 0) {
            return p;
        }
    }
    return 0;
}

static bool paging_print(SftpCompletion *completion, size_t max_lines)
{
    Sftp *sftp = completion->sftp;
    Paging *p = &completion->paging;
    size_t lines = 0;
    size_t index = p->next_index;

    if (index == 0) {
        seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
    }

    if (p->single_column) {
        while (index < p->nnames && lines < max_lines) {
            const SftpCompletionName *n = &p->names[index++];
            if (p->dir_only && !n->is_dir) {
                continue;
            }
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, n->name, strlen(n->name));
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, completion->is_paging_local ? &completion->local_path_separator : &completion->remote_path_separator, 1);
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
            lines++;
        }
    } else {
        char *buf = snewn(p->ncols*p->col_width, char);
        size_t pos = 0;
        size_t col = 0;
        size_t col_start = 0;
        while (index < p->nnames && lines < max_lines) {
            const SftpCompletionName *n = &p->names[index++];
            if (p->dir_only && !n->is_dir) {
                continue;
            }
            while (pos < col_start) {
                buf[pos++] = ' ';
            }
            for (size_t i = 0; n->name[i]; i++) {
                buf[pos++] = n->name[i];
            }
            if (n->is_dir) {
                buf[pos++] = completion->is_paging_local ? completion->local_path_separator : completion->remote_path_separator;
            }
            col++;
            if (col == p->ncols) {
                seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, buf, pos);
                seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
                pos = 0;
                col = 0;
                col_start = 0;
                lines++;
            } else {
                col_start += p->col_width;
            }
        }
        if (pos > 0) {
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, buf, pos);
            seat_output(sftp->seat, SEAT_OUTPUT_STDOUT, "\r\n", 2);
        }
        sfree(buf);
    }
    p->next_index = index;
    return (index < p->nnames);
}

static void paging_start(SftpCompletion *completion, const SftpCompletionName *names, size_t nnames,
                        bool local, bool dir_only)
{
    size_t max = 0;
    size_t nvisible = 0;
    for (size_t i = 0; i < nnames; i++) {
        if (dir_only && !names[i].is_dir) {
            continue;
        }
        size_t len = strlen(names[i].name);
        if (len > max) {
            max = len;
        }
        nvisible++;
    }

    Paging p;
    p.names = names;
    p.nnames = nnames;
    p.nvisible = nvisible;
    p.dir_only = dir_only;
    p.next_index = 0;
    p.col_width = max + 2;
    p.ncols = completion->sftp->width / p.col_width;
    if (p.ncols <= 1) {
        p.ncols = 1;
        p.single_column = true;
    } else {
        p.single_column = false;
    }
    completion->is_paging = true;
    completion->is_paging_local = local;
    completion->paging = p;
}

static bool apply_completion(SftpCompletion *completion,
                             const SftpCompletionName *names, size_t nnames, size_t prefix_length,
                             SftpCmdArgInfo arg_info)
{
    size_t nvisible = nnames;
    size_t base_index = 0;
    if (arg_info.dir_only) {
        nvisible = 0;
        for (size_t i = 0; i < nnames; i++) {
            if (names[i].is_dir) {
                nvisible++;
                base_index = i;
            }
        }
    }

    if (nvisible == 0) {
        return true;
    }
    Sftp *sftp = completion->sftp;
    bool local = arg_info.type == SFTPCMD_ARG_TYPE_LOCAL;
    if (nvisible == 1) {
        const char *feed = names[base_index].name + prefix_length;
        sftpcli_feed(sftp->cli, feed, strlen(feed));
        if (names[base_index].is_dir) {
            sftpcli_feed(sftp->cli, local ? &completion->local_path_separator : &completion->remote_path_separator, 1);
        } else if (arg_info.more_args) {
            sftpcli_feed(sftp->cli, " ", 1);
        }
    } else {
        size_t extension = extend_prefix(names, nnames, prefix_length, base_index, arg_info.dir_only);
        if (extension > 0) {
            sftpcli_feed(sftp->cli, names[base_index].name + prefix_length, extension);
        } else {
            paging_start(completion, names, nnames, local, arg_info.dir_only);
            return false;
        }
    }
    return true;
}

static void command_completion(SftpCompletion *completion, const char *command, bool more_args) {
    const SftpCompletionName *names = completion->command_cache;
    size_t nnames = completion->command_cache_size;
    shrink_names_to_prefix(&names, &nnames, command);
    apply_completion(completion, names, nnames, strlen(command), (SftpCmdArgInfo){SFTPCMD_ARG_TYPE_COMMAND, false, more_args});
}

static void local_completion(SftpCompletion *completion, const char *arg, SftpCmdArgInfo arg_info) {
    SftpCompletionName *names = NULL;
    size_t nnames = 0;
    size_t namesize = 0;

    const char *absolute_arg = get_absolute_path(completion->sftp->lpwd, arg);
    char *wildcard_arg = dupcat(absolute_arg, "*");
    const char *filename = stripslashes(absolute_arg, true);
    size_t parent_length = filename - absolute_arg;
    size_t prefix_length = strlen(filename);

    WildcardMatcher *wcm = begin_wildcard_matching(wildcard_arg);
    if (!wcm) {
        return;
    }
    sfree(wildcard_arg);
    sfree((void *)absolute_arg);
    while (true) {
        char *name = wildcard_get_filename(wcm);
        if (name == NULL) {
            break;
        }
        sgrowarray(names, namesize, nnames);
        names[nnames].is_dir = (file_type(name) == FILE_TYPE_DIRECTORY);
        names[nnames].name = name;
        memcpy(name, name+parent_length, strlen(name+parent_length)+1);
        nnames++;
    }
    finish_wildcard_matching(wcm);
    if (apply_completion(completion, names, nnames, prefix_length, arg_info)) {
        free_name_array(names, nnames);
    }
}

static void remote_completion_continue(SftpCompletion *completion, const char *filename, SftpCmdArgInfo arg_info)
{
    const SftpCompletionName *names = completion->remote_cache;
    size_t nnames = completion->remote_cache_size;
    shrink_names_to_prefix(&names, &nnames, filename);
    apply_completion(completion, names, nnames, strlen(filename), arg_info);
}

static const SftpCmdVtable *remote_completion(SftpCompletion *completion, const char *arg, SftpCmdArgInfo arg_info)
{
    const char *path = sftpcmd_get_absolute_path(completion->sftp->pwd, arg);
    char *filename = stripslashes(path, false);
    size_t parent_length = filename - path;

    if (completion->remote_path == NULL ||
        strncmp(path, completion->remote_path, parent_length) != 0 || completion->remote_path[parent_length] != 0) {
        free_remote_cache(completion);
        completion->remote_path = mkstr(make_ptrlen(path, parent_length));
        if (completion->remote_ctx_filename) {
            sfree((void *)completion->remote_ctx_filename);
        }
        completion->remote_ctx_filename = dupstr(filename);
        completion->remote_ctx_arg_info = arg_info;
        sfree((void *)path);
        return &sftpcompletion_readdir_vt;
    }
    remote_completion_continue(completion, filename, arg_info);
    sfree((void *)path);
    return NULL;
}

SftpCompletion *sftpcompletion_create(Sftp *sftp)
{
    SftpCompletion *completion = snew(SftpCompletion);
    completion->sftp = sftp;
    completion->remote_path = NULL;
    completion->remote_cache = NULL;
    completion->remote_cache_size = 0;
    completion->remote_ctx_filename = NULL;

    size_t command_count = sftpcmd_get_command_count();
    SftpCompletionName *commands = snewn(command_count, SftpCompletionName);
    for (size_t i = 0; i < command_count; i++) {
        commands[i].name = sftpcmd_get_command_name(i);
        commands[i].is_dir = false;
    }
    completion->command_cache = commands;
    completion->command_cache_size = command_count;
    completion->local_path_separator = get_path_separator();
    completion->remote_path_separator = '/';
    completion->is_paging = false;
    completion->is_paging_local = false;
    return completion;
}

void sftpcompletion_free(SftpCompletion *completion)
{
    if (sftpcompletion_is_paging(completion)) {
        sftpcompletion_cancel_paging(completion);
    }
    free_remote_cache(completion);
    sfree((void *)completion->command_cache);
    sfree((void *)completion->remote_ctx_filename);
    sfree(completion);
}

/*
 * Return the argv index of the first argument after leading options.
 * Skips argv[i] while argv[i][0] == '-', except an exact "--" ends option
 * parsing and the returned index is the one after "--".
 * Scan starts at 1 (argv[0] is the command). argc is at least 2.
 */
int skip_options(const char * const *argv, int argc)
{
    int i = 1;
    while (i < argc) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            i++;
            if (arg[1] == '-' && arg[2] == 0) {
                return i;
            }
        } else {
            return i;
        }
    }
    return argc;
}
SftpCmdArgInfo get_command_arg_info(const char *command, int arg_index)
{
    const SftpCmdVtable *vt = sftpcmd_vt_from_name(command);
    if (vt == NULL) {
        return sftpcmd_get_arg_info(arg_index);
    }
    return vt->get_arg_info(arg_index);
}

const SftpCmdVtable *sftpcompletion_start_completion(SftpCompletion *completion)
{
    SftpArgs args;
    sftpargs_parse(sftpcli_copy_line(completion->sftp->cli, true), &args, true);

    if (args.argc == 0) {
        sftpargs_free(&args);
        return NULL;
    }
    if (args.argc == 1) {
        command_completion(completion, args.argv[0], true);
        sftpargs_free(&args);
        return NULL;
    }

    int first_file_arg = skip_options(args.argv, args.argc);
    if (first_file_arg == args.argc) {
        sftpargs_free(&args);
        return NULL;
    }
    int arg_to_complete = args.argc-1;
    SftpCmdArgInfo arg_info = get_command_arg_info(args.argv[0], arg_to_complete-first_file_arg);
    if (arg_info.type == SFTPCMD_ARG_TYPE_REMOTE) {
        const SftpCmdVtable *vt = remote_completion(completion, args.argv[arg_to_complete], arg_info);
        sftpargs_free(&args);
        return vt;
    }
    if (arg_info.type == SFTPCMD_ARG_TYPE_LOCAL) {
        local_completion(completion, args.argv[arg_to_complete], arg_info);
        sftpargs_free(&args);
        return NULL;
    }
    if (arg_info.type == SFTPCMD_ARG_TYPE_COMMAND) {
        command_completion(completion, args.argv[arg_to_complete], arg_info.more_args);
        sftpargs_free(&args);
        return NULL;
    }
    sftpargs_free(&args);
    return NULL;
}

void sftpcompletion_continue_completion(SftpCompletion *completion, const SftpCompletionName *names, size_t nnames)
{
    assert(completion->remote_cache == NULL);
    completion->remote_cache = names;
    completion->remote_cache_size = nnames;
    remote_completion_continue(completion, completion->remote_ctx_filename, completion->remote_ctx_arg_info);
}

const char *sftpcompletion_get_remote_path(SftpCompletion *completion)
{
    return completion->remote_path;
}

void sftpcompletion_cancel_paging(SftpCompletion *completion)
{
    completion->is_paging = false;
    if (completion->is_paging_local) {
        free_name_array(completion->paging.names, completion->paging.nnames);
    }
}

void sftpcompletion_continue_paging(SftpCompletion *completion, int max_lines)
{
    if (!paging_print(completion, max_lines)) {
        completion->paging.next_index = 0;
        sftpcli_refresh(completion->sftp->cli);
    }
}

bool sftpcompletion_is_paging(SftpCompletion *completion)
{
    return completion->is_paging;
}

bool sftpcompletion_is_paging_displayed(SftpCompletion *completion)
{
    return completion->is_paging && completion->paging.next_index > 0;
}
