#include <stdio.h>
#include "putty.h"

const char *const appname = STR(PuTTY++);

const int be_default_protocol = PROT_SSH;

const struct BackendVtable *const backends[] = {
    &ssh_backend,
    &serial_backend,
    &telnet_backend,
    &rlogin_backend,
    &supdup_backend,
    &raw_backend,
    &sshconn_backend,
    NULL
};

const size_t n_ui_backends = 2;
