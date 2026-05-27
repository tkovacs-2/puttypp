#ifndef SFTPBE_H
#define SFTPBE_H

#include "putty.h"
#include "sftpargs.h"

typedef struct SftpCmd SftpCmd;
typedef struct SftpCli SftpCli;
typedef struct SftpCompletion SftpCompletion;

typedef struct Sftp Sftp;
struct Sftp {
    bufchain received_data;
    struct sftp_packet *receiving_pkt;
    unsigned int receiving_pkt_fetched;

    const char *pwd;
    const char *lpwd;

    SftpCmd *cmd;
    SftpArgs args;

    Seat *seat;
    Backend backend;

    SftpCli *cli;
    int width;
    int height;

    Seat sshseat;
    SeatVtable sshseat_vt;
    Backend *ssh;
    Conf *ssh_conf;

    const char *line_homedir;
    const char *line_pwd;

    int line_codepage;
    const char *line_codepage_name;
    const char *reconfig_line_codepage_name;

    void *requests;

    SftpCompletion *completion;
};

#endif
