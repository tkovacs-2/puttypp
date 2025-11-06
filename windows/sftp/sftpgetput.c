#include "sftpgetput.h"
#include "sftpcmd.h"

SftpDir *sftpdirstack_top(SftpDirStack *dirstack)
{
    if (dirstack->top == 0) {
        return NULL;
    }
    return &dirstack->stack[dirstack->top-1];
}

SftpDir *sftpdirstack_pop(SftpDirStack *dirstack)
{
    SftpDir *dir = sftpdirstack_top(dirstack);
    if (!dir) {
        return NULL;
    }
    sfree((void *)dir->fname);
    sfree((void *)dir->outfname);
    for (int i = 0; i < dir->nnames; i++) {
        sfree((void *)dir->ournames[i]);
    }
    sfree(dir->ournames);
    dirstack->top--;
    return sftpdirstack_top(dirstack);
}

SftpDir *sftpdirstack_push(SftpDirStack *dirstack)
{
    dirstack->top++;
    sgrowarray(dirstack->stack, dirstack->capacity, dirstack->top);
    SftpDir *dir = sftpdirstack_top(dirstack);
    memset(dir, 0, sizeof(SftpDir));
    return dir;
}

void sftpdirstack_init(SftpDirStack *dirstack)
{
    dirstack->top = 0;
    dirstack->capacity = 0;
    dirstack->stack = NULL;
}

void sftpdirstack_uninit(SftpDirStack *dirstack)
{
    while (sftpdirstack_pop(dirstack));
    sfree(dirstack->stack);
}

bool getput_parse_args(Sftp *sftp, int *first_file, bool *recurse)
{
    *recurse = false;
    int i = 1;
    while (i < sftp->args.argc && sftp->args.argv[i][0] == '-') {
        if (!strcmp(sftp->args.argv[i], "--")) {
            /* finish processing options */
            i++;
            break;
        } else if (!strcmp(sftp->args.argv[i], "-r")) {
            *recurse = true;
        } else {
            sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: unrecognised option '%s'", sftp->args.argv[0], sftp->args.argv[i]);
            return NULL;
        }
        i++;
    }

    if (i >= sftp->args.argc) {
        sftpcmd_printf(sftp->seat, SEAT_OUTPUT_STDERR, "%s: expects a filename", sftp->args.argv[0]);
        return NULL;
    }
    *first_file = i;
    return true;
}

static int bare_name_compare(const void *av, const void *bv)
{
    const char **a = (const char **) av;
    const char **b = (const char **) bv;
    return strcmp(*a, *b);
}

void getput_sort_dir_names(SftpDir *dir)
{
    qsort(dir->ournames, dir->nnames, sizeof(*dir->ournames), bare_name_compare);
}
