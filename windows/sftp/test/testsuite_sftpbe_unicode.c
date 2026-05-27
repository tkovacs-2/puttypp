#include "testremote.h"
#include "testlocal.h"
#include "testassert.h"
#include "testsuite.h"

#define UTF8_AG "\xc4\x85"
#define LINE_AG "\xb1"
#define UTF8_HAN "\xe5\xa5\xbd"

static void expect_convert_fail(TestLocal *tl)
{
    ASSERT_TRUE(testlocal_find_output(&tl->error, "error: failed to convert string to", false));
}

static void backend_reconfig_line_codepage(TestLocal *tl, const char *line_codepage)
{
    Conf *conf = conf_new();
    conf_set_str(conf, CONF_line_codepage, line_codepage);
    backend_reconfig(tl->sftp, conf);
    conf_free(conf);
}

static void tc_cd(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "d" LINE_AG "ir");
    testlocal_execute(tl, "cd d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "remote directory is /sftp/d" UTF8_AG "ir", true));

    testlocal_execute(tl, "cd d" UTF8_HAN "ir");
    testremote_process(tr);
    expect_convert_fail(tl);
}

static void tc_ls(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "d" LINE_AG "ir/foo" LINE_AG ".txt", 7);
    testlocal_execute(tl, "ls d" UTF8_AG "ir" );
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "foo" UTF8_AG ".txt", false));

    testlocal_execute(tl, "ls d" UTF8_HAN "ir");
    testremote_process(tr);
    expect_convert_fail(tl);
}

static void tc_mkdir(TestLocal *tl, TestRemote *tr)
{
    testlocal_execute(tl, "mkdir d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_dir(tr, "d" LINE_AG "ir"));

    testremote_set_clean(tr);
    testlocal_execute(tl, "mkdir dir d" UTF8_HAN "ir");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_rm(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "foo" LINE_AG ".txt", 3);
    testlocal_execute(tl, "rm foo" UTF8_AG ".txt");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_file(tr, "foo" LINE_AG ".txt"));

    testremote_add_file(tr, "foo.txt", 3);
    testremote_set_clean(tr);
    testlocal_execute(tl, "rm foo.txt foo" UTF8_HAN ".txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_rmdir(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "d" LINE_AG "ir");
    testlocal_execute(tl, "rmdir d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_dir(tr, "d" LINE_AG "ir"));

    testremote_add_dir(tr, "dir");
    testremote_set_clean(tr);
    testlocal_execute(tl, "rmdir dir d" UTF8_HAN "ir");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_mv(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "foo" LINE_AG ".txt", 5);
    testlocal_execute(tl, "mv foo" UTF8_AG ".txt foo" UTF8_AG "2.txt");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_file(tr, "foo" LINE_AG ".txt"));
    ASSERT_TRUE(testremote_check_file(tr, "foo" LINE_AG "2.txt"));

    testremote_add_file(tr, "foo.txt", 5);
    testremote_add_dir(tr, "dir");
    testremote_set_clean(tr);
    testlocal_execute(tl, "mv foo.txt foo" UTF8_HAN ".txt dir");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));

    testremote_set_clean(tr);
    testlocal_execute(tl, "mv foo.txt foo" UTF8_HAN ".txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_chmod(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "foo" LINE_AG ".txt", 2);
    testremote_set_permissions(tr, "foo" LINE_AG ".txt", 0644);
    testlocal_execute(tl, "chmod 600 foo" UTF8_AG ".txt");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_permissions(tr, "foo" LINE_AG ".txt") == 0600);

    testremote_add_file(tr, "foo.txt", 5);
    testremote_set_clean(tr);
    testlocal_execute(tl, "chmod 644 foo.txt foo" UTF8_HAN ".txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_get(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "foo" LINE_AG ".txt", 11);
    testlocal_execute(tl, "get foo" UTF8_AG ".txt foo" UTF8_AG "_local.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "foo" UTF8_AG "_local.txt"));

    testremote_add_dir(tr, "d" LINE_AG "ir/sub");
    testremote_add_file(tr, "d" LINE_AG "ir/sub/foo" LINE_AG ".txt", 12);
    testlocal_execute(tl, "get -r d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_size(tl, "d" UTF8_AG "ir/sub/foo" UTF8_AG ".txt") == 12);

    testlocal_execute(tl, "get foo" UTF8_HAN ".txt");
    testremote_process(tr);
    expect_convert_fail(tl);
}

static void tc_mget(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "foo" LINE_AG "1.txt", 4);
    testremote_add_file(tr, "foo" LINE_AG "2.txt", 5);
    testlocal_add_dir(tl, "dir");
    testlocal_execute(tl, "lcd dir");
    testlocal_execute(tl, "mget foo" UTF8_AG "1.txt foo" UTF8_AG "2.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "dir/foo" UTF8_AG "1.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "dir/foo" UTF8_AG "2.txt"));

    testremote_add_file(tr, "foo3.txt", 4);
    testremote_add_file(tr, "foo4.txt", 5);
    testlocal_execute(tl, "mget foo3.txt foo" UTF8_HAN "4.txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testlocal_check_file(tl, "foo3.txt"));
    ASSERT_FALSE(testlocal_check_file(tl, "foo4.txt"));
}

static void tc_put(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_file(tl, "foo.txt", 9);
    testlocal_execute(tl, "put foo.txt foo" UTF8_AG ".txt");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "foo" LINE_AG ".txt"));

    testlocal_add_dir(tl, "dir");
    testlocal_add_dir(tl, "dir/sub");
    testlocal_add_file(tl, "dir/sub/foo" UTF8_AG ".txt", 7);
    testlocal_execute(tl, "put -r dir d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_size(tr, "d" LINE_AG "ir/sub/foo" LINE_AG ".txt") == 7);

    testlocal_add_file(tl, "foo.txt", 3);
    testremote_set_clean(tr);
    testlocal_execute(tl, "put foo.txt foo" UTF8_HAN ".txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_is_dirty(tr));

    testlocal_add_dir(tl, "dirbad");
    testlocal_add_dir(tl, "dirbad/sub");
    testlocal_add_file(tl, "dirbad/sub/foo" UTF8_HAN ".txt", 3);
    testlocal_execute(tl, "put -r dirbad");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_FALSE(testremote_check_file(tr, "dirbad/sub/foo.txt"));
}

static void tc_mput(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_file(tl, "foo1.txt", 6);
    testlocal_add_file(tl, "foo2.txt", 7);
    testremote_add_dir(tr, "d" LINE_AG "ir");
    testlocal_execute(tl, "cd d" UTF8_AG "ir");
    testremote_process(tr);
    testlocal_execute(tl, "mput foo1.txt foo2.txt");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "d" LINE_AG "ir/foo1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "d" LINE_AG "ir/foo2.txt"));

    testlocal_add_file(tl, "foo.txt", 3);
    testlocal_execute(tl, "mput foo.txt foo" UTF8_HAN "2.txt");
    testremote_process(tr);
    expect_convert_fail(tl);
    ASSERT_TRUE(testremote_check_file(tr, "d" LINE_AG "ir/foo.txt"));
}

static void tc_reput(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_dir(tl, "dir");
    testlocal_add_file(tl, "dir/foo1.txt", 0);
    testlocal_add_file(tl, "dir/foo" UTF8_HAN ".txt", 0);
    testremote_add_dir(tr, "dir");
    testremote_add_file(tr, "dir/foo1.txt", 0);
    testlocal_execute(tl, "reput -r dir");
    testremote_process(tr);
    expect_convert_fail(tl);
 }

static void tc_sftpbe_reconfig(TestLocal *tl, TestRemote *tr)
{
    testremote_add_dir(tr, "d" UTF8_AG "ir");
    backend_reconfig_line_codepage(tl, "UTF-8");
    testlocal_execute(tl, "rmdir d" UTF8_AG "ir");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_dir(tr, "d" UTF8_AG "ir"));

    testremote_add_file(tr, "foo" LINE_AG ".txt", 20);
    testlocal_execute(tl, "get foo" UTF8_AG ".txt");
    backend_reconfig_line_codepage(tl, "ISO-8859-5:1999 (Latin/Cyrillic)");
    backend_reconfig_line_codepage(tl, "ISO-8859-2:1999 (Latin-2, East Europe)");
    testremote_process(tr);
    ASSERT_FALSE(testlocal_check_file(tl, "foo" UTF8_AG ".txt"));
    testlocal_execute(tl, "get foo" UTF8_AG ".txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "foo" UTF8_AG ".txt"));

    testremote_add_dir(tr, "d" LINE_AG "ir");
    backend_reconfig_line_codepage(tl, "ISO-8859-1:1998 (Latin-1, West Europe)");
    testlocal_execute(tl, "cd d\xC2\xB1ir");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "d\xC2\xB1ir", false));
    backend_reconfig_line_codepage(tl, "ISO-8859-2:1999 (Latin-2, East Europe)");
    testlocal_execute(tl, "pwd");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "d" UTF8_AG "ir", false));

    testlocal_allow_cli_output(tl, true);
    backend_reconfig_line_codepage(tl, "unknown");
    ASSERT_TRUE(testlocal_find_output(&tl->error, "remote codepage unknown is unknown", false));
}

static void tc_completion(TestLocal *tl, TestRemote *tr)
{
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    testlocal_allow_cli_output(tl, true);
    backend_send(&sftp->backend, "cd di" UTF8_HAN "r/a\x09", 12);
    expect_convert_fail(tl);
}

void testsuite_sftpbe_unicode(void)
{
    BEGIN_TESTSUITE(testsuite_sftpbe_unicode, TestLocal *tl, TestRemote *tr)
    ADD_TESTCASE(tc_cd)
    ADD_TESTCASE(tc_ls)
    ADD_TESTCASE(tc_mkdir)
    ADD_TESTCASE(tc_rm)
    ADD_TESTCASE(tc_rmdir)
    ADD_TESTCASE(tc_mv)
    ADD_TESTCASE(tc_chmod)
    ADD_TESTCASE(tc_get)
    ADD_TESTCASE(tc_mget)
    ADD_TESTCASE(tc_put)
    ADD_TESTCASE(tc_mput)
    ADD_TESTCASE(tc_reput)
    ADD_TESTCASE(tc_sftpbe_reconfig)
    ADD_TESTCASE(tc_completion)
    END_TESTSUITE()

    size_t i = 0;
    while (testsuite_sftpbe_unicode[i].function) {
        printf("-- Executing test case: %s (unicode)\n", testsuite_sftpbe_unicode[i].name);
        TestRemote tr;
        TestLocal tl;
        testremote_init(&tr);
        testlocal_init(&tl, &tr, "ISO-8859-2:1999 (Latin-2, East Europe)");
        testsuite_sftpbe_unicode[i].function(&tl, &tr);
        testlocal_uninit(&tl);
        testremote_uninit(&tr);
        i++;
    }
}
