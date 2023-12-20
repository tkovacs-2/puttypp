#include <stdio.h>
#include "putty.h"

const char *const appname = STR(PuTTY++);

const int be_default_protocol = PROT_SSH;

BackendVtable conpty_backend_puttypp;

const struct BackendVtable *const backends[] = {
    &ssh_backend,
    &serial_backend,
    &telnet_backend,
    &rlogin_backend,
    &supdup_backend,
    &raw_backend,
    &sshconn_backend,
    &conpty_backend_puttypp,
    NULL
};

const size_t n_ui_backends = 2;
