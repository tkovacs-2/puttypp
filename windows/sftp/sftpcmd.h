#ifndef SFTPCMD_H
#define SFTPCMD_H

#include "sftpbe.h"

typedef struct SftpCmdVtable SftpCmdVtable;

typedef struct SftpCmd SftpCmd;
struct SftpCmd {
    const SftpCmdVtable *vt;
    struct sftp_request *req;
    int req_type;
};

void sftpcmd_set_request(SftpCmd *cmd, int req_type, struct sftp_request *req);
void sftpcmd_clear_request(SftpCmd *cmd);

struct sftp_packet;

typedef enum SftpCmdArgType {
    SFTPCMD_ARG_TYPE_INVALID,
    SFTPCMD_ARG_TYPE_COMMAND,
    SFTPCMD_ARG_TYPE_REMOTE,
    SFTPCMD_ARG_TYPE_LOCAL
} SftpCmdArgType;

typedef struct SftpCmdArgInfo {
    SftpCmdArgType type;
    bool dir_only;
    bool more_args;
} SftpCmdArgInfo;

struct SftpCmdVtable {
    SftpCmd *(*init)(Sftp *sftp);
    void (*free)(SftpCmd *cmd);
    bool (*process_pkt)(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pkt);
    SftpCmdArgInfo (*get_arg_info)(int file_arg_index);
};

static inline SftpCmd *sftpcmd_init(const SftpCmdVtable *vt, Sftp *sftp) { return vt->init(sftp); }
static inline void sftpcmd_free(SftpCmd *cmd) { cmd->vt->free(cmd); }
static inline bool sftpcmd_process_pkt(SftpCmd *cmd, Sftp *sftp, struct sftp_packet *pkt) { return cmd->vt->process_pkt(cmd, sftp, pkt); }

const SftpCmdVtable *sftpcmd_vt_from_name(const char *name);

size_t sftpcmd_get_command_count();
const char *sftpcmd_get_command_name(size_t i);

SftpCmdArgInfo sftpcmd_get_arg_info(int);

#endif
