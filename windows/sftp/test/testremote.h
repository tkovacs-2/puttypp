#ifndef TESTREMOTE_H
#define TESTREMOTE_H

#include "putty.h"
#include "ssh/sftp.h"

typedef struct TestRemoteFile TestRemoteFile;

typedef struct TestRemote {
  SftpServer srv;
  Backend dummyssh;

  Seat *client_seat;
  bufchain received_data;

  TestRemoteFile *root;
  TestRemoteFile *home;
} TestRemote;

void testremote_init(TestRemote *tr);
void testremote_uninit(TestRemote *tr);

void testremote_add_file(TestRemote *tr, const char *name, size_t size);
void testremote_add_dir(TestRemote *tr, const char *name);
bool testremote_check_file(TestRemote *tr, const char *name);
bool testremote_check_dir(TestRemote *tr, const char *name);

void testremote_set_permissions(TestRemote *tr, const char *name, unsigned long permissions);
unsigned long testremote_check_permissions(TestRemote *tr, const char *name);

size_t testremote_check_size(TestRemote *tr, const char *name);
size_t testremote_check_create_size(TestRemote *tr, const char *name);

void testremote_startsession(TestRemote *tr);

void testremote_drop_requests(TestRemote *tr);
struct sftp_packet *testremote_get_request(TestRemote *tr);
void testremote_process_request(TestRemote *tr, struct sftp_packet *req);
void testremote_process(TestRemote *tr);

void testremote_connection_fatal(TestRemote *tr);
#endif
