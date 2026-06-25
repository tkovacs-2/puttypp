#include "testremote.h"
#include "testlocal.h"
#include "testassert.h"
#include "testsuite.h"

static void execute(TestLocal *tl, TestRemote *tr, int type, int skip, const char *command)
{
    testremote_fail_request(tr, type, skip);
    testlocal_clear_output(tl);
    testlocal_execute(tl, command);
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "permission denied", false));
}

static void tc_cd(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "dir");
    execute(tl, tr, SSH_FXP_OPENDIR, 0, "cd dir");
    testremote_fail_request(tr, SSH_FXP_REALPATH, 0);
    testlocal_clear_output(tl);
    testlocal_execute(tl, "cd ../sftp/dir");
    testremote_process(tr);
    ASSERT_FALSE(testlocal_find_output(&tl->error, "permission denied", false));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "remote directory is /sftp/../sftp/dir", false));
}

static void tc_ls(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "dir/file.txt", 1);
    testremote_add_file(tr, "dir/file2.txt", 1);
    testremote_add_file(tr, "dir/file3.txt", 1);
    execute(tl, tr, SSH_FXP_REALPATH, 0, "ls dir");
    execute(tl, tr, SSH_FXP_OPENDIR, 0, "ls dir");
    execute(tl, tr, SSH_FXP_READDIR, 0, "ls dir");
    execute(tl, tr, SSH_FXP_READDIR, 1, "ls dir");
}

static void tc_get(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "file.txt", 70000);
    execute(tl, tr, SSH_FXP_OPEN, 0, "get file.txt");
    execute(tl, tr, SSH_FXP_READ, 2, "get file.txt");

    testremote_add_file(tr, "dir/file.txt", 1);
    testremote_add_file(tr, "dir/file2.txt", 1);
    testremote_add_file(tr, "dir/file3.txt", 1);
    execute(tl, tr, SSH_FXP_OPENDIR, 0, "get -r dir");
    execute(tl, tr, SSH_FXP_READDIR, 1, "get -r dir");
}

static void tc_put(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_file(tl, "file.txt", 70000);
    execute(tl, tr, SSH_FXP_OPEN, 0, "put file.txt");
    execute(tl, tr, SSH_FXP_WRITE, 0, "put file.txt");

    testlocal_add_dir(tl, "dir");
    testlocal_add_file(tl, "dir/file.txt", 1);
    execute(tl, tr, SSH_FXP_MKDIR, 0, "put -r dir remote_dir");
    ASSERT_FALSE(testremote_check_dir(tr, "remote_dir"));
}

static void tc_reput(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_file(tl, "file.txt", 10);
    testremote_add_file(tr, "file.txt", 5);
    execute(tl, tr, SSH_FXP_FSTAT, 0, "reput file.txt");
}

static void tc_mkdir(TestLocal *tl, TestRemote *tr)
{
    execute(tl, tr, SSH_FXP_MKDIR, 1, "mkdir dir1 dir2");
    ASSERT_TRUE(testremote_check_dir(tr, "dir1"));
    ASSERT_FALSE(testremote_check_dir(tr, "dir2"));
}

static void tc_rm(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "file1.txt", 1);
    testremote_add_file(tr, "file2.txt", 1);
    execute(tl, tr, SSH_FXP_REMOVE, 1, "rm file1.txt file2.txt");
    ASSERT_FALSE(testremote_check_file(tr, "file1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "file2.txt"));
}

static void tc_rmdir(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "dir1");
    testremote_add_dir(tr, "dir2");
    execute(tl, tr, SSH_FXP_RMDIR, 1, "rmdir dir1 dir2");
    ASSERT_FALSE(testremote_check_dir(tr, "dir1"));
    ASSERT_TRUE(testremote_check_dir(tr, "dir2"));
}

static void tc_mv(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "dir");
    testremote_add_file(tr, "file1.txt", 1);
    testremote_add_file(tr, "file2.txt", 1);
    execute(tl, tr, SSH_FXP_RENAME, 0, "mv file1.txt file2.txt dir");
    ASSERT_FALSE(testremote_check_file(tr, "dir/file1.txt"));
    ASSERT_FALSE(testremote_check_file(tr, "dir/file2.txt"));
    execute(tl, tr, SSH_FXP_RENAME, 1, "mv file1.txt file2.txt dir");
    ASSERT_TRUE(testremote_check_file(tr, "dir/file1.txt"));
    ASSERT_FALSE(testremote_check_file(tr, "dir/file2.txt"));
}

static void tc_chmod(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "file.txt", 1);
    execute(tl, tr, SSH_FXP_STAT, 0, "chmod 600 file.txt");
    testremote_set_permissions(tr, "file.txt", 0644);
    execute(tl, tr, SSH_FXP_SETSTAT, 0, "chmod 600 file.txt");
    ASSERT_TRUE(testremote_check_permissions(tr, "file.txt") == 0644);
}

static void tc_wildcard_iterator(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "dir/file1.txt", 1);
    testremote_add_file(tr, "dir/file2.txt", 1);
    testremote_add_file(tr, "dir/file3.txt", 1);

    execute(tl, tr, SSH_FXP_REALPATH, 0, "rm dir/*.txt");
    execute(tl, tr, SSH_FXP_OPENDIR, 0, "rm dir/*.txt");
    execute(tl, tr, SSH_FXP_READDIR, 1, "rm dir/*.txt");
    ASSERT_FALSE(testremote_check_file(tr, "dir/file1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "dir/file2.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "dir/file3.txt"));
}

void testsuite_sftpbe_failures()
{
    BEGIN_TESTSUITE(testsuite_sftpbe_failures, TestLocal *tl, TestRemote *tr)
    ADD_TESTCASE(tc_cd)
    ADD_TESTCASE(tc_ls)
    ADD_TESTCASE(tc_get)
    ADD_TESTCASE(tc_put)
    ADD_TESTCASE(tc_reput)
    ADD_TESTCASE(tc_mkdir)
    ADD_TESTCASE(tc_rm)
    ADD_TESTCASE(tc_rmdir)
    ADD_TESTCASE(tc_mv)
    ADD_TESTCASE(tc_chmod)
    ADD_TESTCASE(tc_wildcard_iterator)
    END_TESTSUITE()

    size_t i = 0;
    while (testsuite_sftpbe_failures[i].function) {
        printf("-- Executing test case: %s (failures)\n", testsuite_sftpbe_failures[i].name);
        TestRemote tr;
        TestLocal tl;
        testremote_init(&tr);
        testlocal_init(&tl, &tr, "UTF-8");
        testsuite_sftpbe_failures[i].function(&tl, &tr);
        testlocal_uninit(&tl);
        testremote_uninit(&tr);
        i++;
    }
}
