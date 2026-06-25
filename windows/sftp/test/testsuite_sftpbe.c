#include "testremote.h"
#include "testlocal.h"
#include "testassert.h"
#include "testsuite.h"
#include "psftp.h"
#include "sftpcli.h"

static void tc_framework(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "alma/korte", 13);
    ASSERT_FALSE(testremote_check_file(tr, "/alma/korte"));
    ASSERT_TRUE(testremote_check_file(tr, "/sftp/alma/korte"));
    ASSERT_TRUE(testremote_check_size(tr, "alma/korte") == 13);
    ASSERT_TRUE(testremote_check_create_size(tr, "alma/korte") == 13);
    ASSERT_TRUE(testremote_check_permissions(tr, "alma") == 0755);
    ASSERT_TRUE(testremote_check_permissions(tr, "alma/korte") == 0644);
    testremote_set_permissions(tr, "alma/korte", 0777);
    ASSERT_TRUE(testremote_check_permissions(tr, "alma/korte") == 0777);

    testremote_add_file(tr, "/alma/korte", 0);
    ASSERT_TRUE(testremote_check_dir(tr, "/alma"));
    ASSERT_TRUE(testremote_check_file(tr, "/alma/korte"));
    testremote_add_file(tr, "/alma", 0);
    ASSERT_TRUE(testremote_check_file(tr, "/alma"));
    ASSERT_FALSE(testremote_check_file(tr, "/alma/korte"));

    testlocal_add_file(tl, "alma", 100);
    testlocal_add_dir(tl, "korte");
    testlocal_add_file(tl, "korte/alma", 200);
    ASSERT_TRUE(testlocal_check_file(tl, "alma"));
    ASSERT_FALSE(testlocal_check_file(tl, "korte"));
    ASSERT_TRUE(testlocal_check_dir(tl, "korte"));
    ASSERT_TRUE(testlocal_check_size(tl, "korte/alma") == 200);
    ASSERT_FALSE(testlocal_check_file(tl, "alma/alma"));
    ASSERT_TRUE(testlocal_check_create_size(tl, "alma", 100));
    ASSERT_FALSE(testlocal_check_create_size(tl, "alma", 99));
    ASSERT_FALSE(testlocal_check_create_size(tl, "alma", 101));
    ASSERT_FALSE(testlocal_check_create_size(tl, "alma", 0));
}

static void tc_cd(TestLocal *tl, TestRemote *tr)
{
    testlocal_execute(tl, "pwd");
    ASSERT_TRUE(testlocal_find_output(&tl->output, "remote directory is /sftp", true));

    testremote_add_file(tr, "alma", 1234);
    testremote_add_dir(tr, "korte");

    testlocal_execute(tl, "ls");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "-rw-r--r--  1234 alma", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "drwxr-xr-x     0 korte", true));

    testlocal_execute(tl, "ls a*");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "-rw-r--r--  1234 alma", true));
    ASSERT_FALSE(testlocal_find_output(&tl->output, "korte", false));

    testlocal_execute(tl, "ls k*/a");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "Multiple-level wildcards are not supported", true));

    testlocal_execute(tl, "cd ../../../../");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "remote directory is /", true));

    testlocal_execute(tl, "cd test");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "no such file or directory", false));

    testlocal_execute(tl, "ls test");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "no such file or directory", false));

    testlocal_execute(tl, "cd");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "remote directory is /sftp", true));
}

static void tc_lcd(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_dir(tl, "korte");
    testlocal_add_file(tl, "korte/alma", 200);
    testlocal_execute(tl, "lcd korte\\..\\korte");
    testlocal_execute(tl, "lpwd");
    ASSERT_TRUE(testlocal_find_output(&tl->output, "\\l" "\xc3\xb6\xe4\xbd\xa0" "cal\\korte", false));
    testlocal_execute(tl, "put alma");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "alma"));

    testlocal_execute(tl, "lcd test");
    ASSERT_TRUE(testlocal_find_output(&tl->error, "unable to change directory", false));
}

static void tc_bye(TestLocal *tl, TestRemote *tr)
{
    testlocal_execute(tl, "bye");
    ASSERT_TRUE(tl->called_seat_function == SF_NOTIFY_REMOTE_EXIT);
}

static void tc_put(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_dir(tl, "x");
    testlocal_add_dir(tl, "x/x2");
    testlocal_add_file(tl, "x/3.html", 10);
    testlocal_add_file(tl, "x/1.txt", 11);
    testlocal_add_file(tl, "x/x2/2.txt", 40000);
    testlocal_add_dir(tl, "x/x2/x3");
    testremote_add_dir(tr, "w");

    testlocal_execute(tl, "put x\\3.html w/1.html");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "w/1.html"));

    testlocal_execute(tl, "put -r x");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "x/3.html"));
    ASSERT_TRUE(testremote_check_file(tr, "x/1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "x/x2/2.txt"));
    ASSERT_TRUE(testremote_check_dir(tr, "x/x2/x3"));

    testlocal_execute(tl, "put -r x y");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "y/3.html"));
    ASSERT_TRUE(testremote_check_file(tr, "y/1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "y/x2/2.txt"));
    ASSERT_TRUE(testremote_check_dir(tr, "y/x2/x3"));

    testlocal_execute(tl, "lcd x\\x2");
    testremote_process(tr);
    testlocal_execute(tl, "cd w");
    testremote_process(tr);
    testlocal_execute(tl, "put 2.txt");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "w/2.txt"));
    testlocal_execute(tl, "put ..\\x2\\2.txt ../w/3.txt");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_size(tr, "w/3.txt") == 40000);

    testlocal_execute(tl, "put ..\\x2\\3.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "unable to open", false));
}

static void tc_mput(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_dir(tl, "test1");
    testlocal_add_dir(tl, "test1/x");
    testlocal_add_dir(tl, "test1/y");
    testlocal_add_dir(tl, "test2");
    testlocal_add_dir(tl, "test3");
    testlocal_add_dir(tl, "x");
    testlocal_add_file(tl, "1.html", 10);
    testlocal_add_file(tl, "2.html", 10);
    testlocal_add_file(tl, "3.txt", 10);
    testlocal_add_file(tl, "test1/1.txt", 11);
    testlocal_add_file(tl, "test1/x/3.txt", 11);
    testlocal_add_dir(tl, "test1/y");
    testlocal_add_file(tl, "test1/2.txt", 11);
    testlocal_add_file(tl, "test2/4.html", 11);
    testlocal_add_file(tl, "test3/5.txt", 11);
    testlocal_add_file(tl, "x/5.txt", 11);
    testlocal_add_file(tl, "x/6.txt", 11);
    testlocal_add_file(tl, "x/7.html", 11);
    testremote_add_dir(tr, "w");

    testlocal_execute(tl, "cd w");
    testremote_process(tr);
    testlocal_execute(tl, "mput -r *.html test* x/*.html");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "w/1.html"));
    ASSERT_TRUE(testremote_check_file(tr, "w/2.html"));
    ASSERT_TRUE(testremote_check_file(tr, "w/test1/1.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "w/test1/x/3.txt"));
    ASSERT_TRUE(testremote_check_dir(tr, "w/test1/y"));
    ASSERT_TRUE(testremote_check_file(tr, "w/test1/2.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "w/test2/4.html"));
    ASSERT_TRUE(testremote_check_file(tr, "w/test3/5.txt"));
    ASSERT_TRUE(testremote_check_file(tr, "w/7.html"));

    testremote_set_clean(tr);
    testlocal_execute(tl, "mput .*");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "nothing matched", false));
    ASSERT_FALSE(testremote_is_dirty(tr));
}

static void tc_reput(TestLocal *tl, TestRemote *tr)
{
    testlocal_add_dir(tl, "test");
    testlocal_add_file(tl, "test/1.txt", 100);
    testlocal_add_file(tl, "test/2.txt", 100);
    testlocal_add_file(tl, "test/3.txt", 100);
    testlocal_add_file(tl, "test/4.txt", 100);

    testremote_add_file(tr, "test/1.txt", 100);
    testremote_add_file(tr, "test/2.txt", 100);
    testremote_add_file(tr, "test/3.txt", 50);

    testlocal_execute(tl, "reput -r test");
    testremote_process(tr);

    ASSERT_TRUE(testremote_check_size(tr, "test/1.txt") == 100);
    ASSERT_TRUE(testremote_check_size(tr, "test/2.txt") == 100);
    ASSERT_TRUE(testremote_check_size(tr, "test/3.txt") == 100);
    ASSERT_TRUE(testremote_check_size(tr, "test/4.txt") == 100);
    ASSERT_TRUE(testremote_check_create_size(tr, "test/1.txt") == 100);
    ASSERT_TRUE(testremote_check_create_size(tr, "test/2.txt") == 100);
    ASSERT_TRUE(testremote_check_create_size(tr, "test/3.txt") == 50);
    ASSERT_TRUE(testremote_check_create_size(tr, "test/4.txt") == 0);
}

static void tc_get(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "x/3.html", 10);
    testremote_add_file(tr, "x/1.txt", 11);
    testremote_add_file(tr, "x/x2/2.txt", 40000);
    testremote_add_dir(tr, "x/x2/x3");
    testremote_add_dir(tr, "x/c:\\");

    testlocal_add_dir(tl, "w");

    testlocal_execute(tl, "get x/3.html w\\1.html");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "w\\1.html"));

    testlocal_execute(tl, "get -r x");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "x/3.html"));
    ASSERT_TRUE(testlocal_check_file(tl, "x/1.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "x/x2/2.txt"));
    ASSERT_TRUE(testlocal_check_dir(tl, "x/x2/x3"));
    ASSERT_TRUE(testlocal_find_output(&tl->error, "ignoring potentially dangerous server-supplied filename 'c:\\'", true));

    testlocal_execute(tl, "get -r /sftp/x y");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "y/3.html"));
    ASSERT_TRUE(testlocal_check_file(tl, "y/1.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "y/x2/2.txt"));
    ASSERT_TRUE(testlocal_check_dir(tl, "y/x2/x3"));

    testlocal_execute(tl, "cd x/x2");
    testremote_process(tr);
    testlocal_execute(tl, "lcd w");
    testremote_process(tr);
    testlocal_execute(tl, "get 2.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_file(tl, "w/2.txt"));
    testlocal_execute(tl, "get ../x2/2.txt ..\\w\\3.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_check_size(tl, "w/3.txt") == 40000);

    testlocal_execute(tl, "get ../x2/3.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "unable to open", false));
}

static void tc_mget(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "1.html", 10);
    testremote_add_file(tr, "2.html", 10);
    testremote_add_file(tr, "3.txt", 10);
    testremote_add_file(tr, "test1/1.txt", 11);
    testremote_add_file(tr, "test1/x/3.txt", 11);
    testremote_add_dir(tr, "test1/y");
    testremote_add_file(tr, "test1/2.txt", 11);
    testremote_add_file(tr, "test2/4.html", 11);
    testremote_add_file(tr, "test3/5.txt", 11);
    testremote_add_file(tr, "x/5.txt", 11);
    testremote_add_file(tr, "x/6.txt", 11);
    testremote_add_file(tr, "x/7.html", 11);
    testlocal_add_dir(tl, "w");

    testlocal_execute(tl, "lcd w");
    testremote_process(tr);
    testlocal_execute(tl, "mget -r /sftp/*.html test* x/*.html");
    testremote_process(tr);

    ASSERT_TRUE(testlocal_check_file(tl, "w/1.html"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/2.html"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/test1/1.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/test1/x/3.txt"));
    ASSERT_TRUE(testlocal_check_dir(tl, "w/test1/y"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/test1/2.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/test2/4.html"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/test3/5.txt"));
    ASSERT_TRUE(testlocal_check_file(tl, "w/7.html"));

    testlocal_execute(tl, "mget test*/*.txt");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "Multiple-level wildcards are not supported in", false));
}

static void tc_reget(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "test/1.txt", 100);
    testremote_add_file(tr, "test/2.txt", 100);
    testremote_add_file(tr, "test/3.txt", 100);
    testremote_add_file(tr, "test/4.txt", 100);

    testlocal_add_dir(tl, "test");
    testlocal_add_file(tl, "test/1.txt", 100);
    testlocal_add_file(tl, "test/2.txt", 100);
    testlocal_add_file(tl, "test/3.txt", 50);

    testlocal_execute(tl, "reget -r test");
    testremote_process(tr);

    ASSERT_TRUE(testlocal_check_size(tl, "test/1.txt") == 100);
    ASSERT_TRUE(testlocal_check_size(tl, "test/2.txt") == 100);
    ASSERT_TRUE(testlocal_check_size(tl, "test/3.txt") == 100);
    ASSERT_TRUE(testlocal_check_size(tl, "test/4.txt") == 100);
    ASSERT_TRUE(testlocal_check_create_size(tl, "test/1.txt", 100));
    ASSERT_TRUE(testlocal_check_create_size(tl, "test/2.txt", 100));
    ASSERT_TRUE(testlocal_check_create_size(tl, "test/3.txt", 50));
}

static void tc_mkdir(TestLocal *tl, TestRemote *tr)
{
    testlocal_execute(tl, "mkdir /sftp/test test\\\"2\\\"");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_dir(tr, "test"));
    ASSERT_TRUE(testremote_check_dir(tr, "test\"2\""));

    testremote_add_dir(tr, "test/x");
    testlocal_execute(tl, "mkdir test/x");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "failure", false));
}

static void tc_rm(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "a.jpg", 10);
    testremote_add_file(tr, "b.jpg", 10);
    testremote_add_file(tr, "c.txt", 10);
    testremote_add_file(tr, "test/a.html", 10);
    testremote_add_file(tr, "test/b.jpg", 10);
    testremote_add_file(tr, "test/a.jpg", 10);

    testlocal_execute(tl, "rm /sftp/*.jpg test/a.*");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_file(tr, "a.jpg"));
    ASSERT_FALSE(testremote_check_file(tr, "b.jpg"));
    ASSERT_TRUE(testremote_check_file(tr, "c.txt"));
    ASSERT_FALSE(testremote_check_file(tr, "test/a.html"));
    ASSERT_TRUE(testremote_check_file(tr, "test/b.jpg"));
    ASSERT_FALSE(testremote_check_file(tr, "test/a.jpg"));

    testremote_add_dir(tr, "test1");
    testlocal_execute(tl, "rm test1");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_dir(tr, "test1"));

    testlocal_execute(tl, "rmdir test1");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_dir(tr, "test1"));

    testlocal_execute(tl, "rm /sftp/x");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "no such file or directory", false));

    testremote_add_file(tr, "dir/c:", 1);

    testlocal_execute(tl, "rm dir/*");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_file(tr, "dir/c:"));
    ASSERT_TRUE(testlocal_find_output(&tl->error, "ignoring potentially dangerous server-supplied filename 'c:'", true));
}

static void tc_mv(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "a.jpg", 10);
    testlocal_execute(tl, "mv /sftp/a.jpg b.jpg");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_file(tr, "a.jpg"));
    ASSERT_TRUE(testremote_check_file(tr, "b.jpg"));

    testremote_add_file(tr, "b.jpg", 10);
    testremote_add_file(tr, "a.jpg", 10);
    testremote_add_file(tr, "test/a.html", 10);
    testremote_add_dir(tr, "test1");
    testlocal_execute(tl, "mv *.jpg test test1");
    testremote_process(tr);
    ASSERT_FALSE(testremote_check_file(tr, "a.jpg"));
    ASSERT_FALSE(testremote_check_file(tr, "b.jpg"));
    ASSERT_FALSE(testremote_check_dir(tr, "test"));
    ASSERT_TRUE(testremote_check_file(tr, "test1/a.jpg"));
    ASSERT_TRUE(testremote_check_file(tr, "test1/b.jpg"));
    ASSERT_TRUE(testremote_check_file(tr, "test1/test/a.html"));

    testlocal_execute(tl, "mv test test2");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "no such file or directory", false));
}

static void tc_chmod(TestLocal *tl, TestRemote *tr)
{
    testremote_add_file(tr, "a.jpg", 10);
    testremote_add_file(tr, "b.jpg", 10);
    testremote_set_permissions(tr, "b.jpg", 0466);
    testlocal_execute(tl, "chmod go+w,u-w /sftp/a.jpg b.jpg");
    testremote_process(tr);
    ASSERT_TRUE(testremote_check_permissions(tr, "a.jpg") == 0466);
    ASSERT_TRUE(testremote_check_permissions(tr, "b.jpg") == 0466);

    testlocal_execute(tl, "chmod abc test");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "contains unrecognised user/group/other specifier", false));

    testlocal_execute(tl, "chmod 777 test");
    testremote_process(tr);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "no such file or directory", false));
}

static void tc_ctrlc(TestLocal *tl, TestRemote *tr)
{
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    testlocal_execute(tl, "ls test");
    ASSERT_TRUE(sftp->cmd);
    backend_send(tl->sftp, "\x03", 1);
    ASSERT_FALSE(sftp->cmd);
    testremote_drop_requests(tr);

    testremote_add_file(tr, "a.jpg", 1000000);
    testlocal_execute(tl, "get a.jpg");
    testremote_process_request(tr, testremote_get_request(tr)); // realpath
    testremote_process_request(tr, testremote_get_request(tr)); // stat
    testremote_process_request(tr, testremote_get_request(tr)); // open
    backend_send(tl->sftp, "\x03", 1);
    ASSERT_TRUE(testlocal_check_file(tl, "a.jpg"));
    ASSERT_TRUE(testlocal_check_size(tl, "a.jpg") == 0);
    testremote_drop_requests(tr);

    testlocal_allow_cli_output(tl, true);
    testlocal_add_file(tl, "b.jpg", 1000000);
    testlocal_execute(tl, "put b.jpg");
    testremote_process_request(tr, testremote_get_request(tr)); // open
    backend_send(tl->sftp, "\x03", 1);
    ASSERT_TRUE(testremote_check_file(tr, "b.jpg"));
    ASSERT_TRUE(testremote_check_size(tr, "b.jpg") == 0);
    testremote_drop_requests(tr);
    testlocal_allow_cli_output(tl, false);
}

static void tc_connection_fatal(TestLocal *tl, TestRemote *tr)
{
    testlocal_execute(tl, "ls test");
    testremote_connection_fatal(tr);
    ASSERT_TRUE(tl->called_seat_function == SF_CONNECTION_FATAL);
    ASSERT_TRUE(testlocal_find_output(&tl->error, "test connection fatal", true));
}

static void tc_pwdline(TestLocal *tl, TestRemote *tr)
{
#define S_HAO "\xe5\xa5\xbd\xe5\xa5\xbd\xe5\xa5\xbd\xe5\xa5\xbd\xe5\xa5\xbd"
#define S_DA "\xe5\xa4\xa7\xe5\xa4\xa7\xe5\xa4\xa7"
#define S_ELLIPSIS "\xe2\x80\xa6"

    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    const char *lpwd = "C:\\N-" S_HAO "-Data\\tkovacs\\Downloads";
    const char *pwd = "/root/tkovacs/build" S_DA "_rocky";

    struct {int columns; const char *expected;} testcases[] = {
        {80, "C:\\N-" S_HAO "-Data\\tkovacs\\Downloads <> /root/tkovacs/build" S_DA "_rocky"},
        {74, S_ELLIPSIS "\\N-" S_HAO "-Data\\tkovacs\\Downloads <> /root/tkovacs/build" S_DA "_rocky"},
        {71, "C:\\N-" S_HAO "-Data\\tkovacs\\Downloads <> " S_ELLIPSIS "/tkovacs/build" S_DA "_rocky"},
        {70, S_ELLIPSIS "\\N-" S_HAO "-Data\\tkovacs\\Downloads <> " S_ELLIPSIS "/tkovacs/build" S_DA "_rocky"},
        {68, S_ELLIPSIS "\\tkovacs\\Downloads <> " S_ELLIPSIS "/tkovacs/build" S_DA "_rocky"},
        {51, S_ELLIPSIS "\\tkovacs\\Downloads <> " S_ELLIPSIS "/build" S_DA "_rocky"},
        {43, S_ELLIPSIS "\\Downloads <> " S_ELLIPSIS "/build" S_DA "_rocky"}
    };

    testlocal_allow_cli_output(tl, true);
    for (size_t i = 0; i < sizeof(testcases) / sizeof(testcases[0]); i++) {
        testlocal_clear_output(tl);
        sftpcli_start(sftp->cli, testcases[i].columns, lpwd, pwd);
        printf("\n");
        ASSERT_TRUE(testlocal_find_output(&tl->output, testcases[i].expected, false));
    }
    testlocal_clear_output(tl);
    sftpcli_start(sftp->cli, 35, lpwd, pwd);
    printf("\n");
    ASSERT_TRUE(testlocal_empty_output(&tl->output));
}

static void completion_send(Sftp *sftp, TestRemote *tr, const char *data, size_t len)
{
    backend_send(&sftp->backend, data, len);
    testremote_process(tr);
}

static void completion_cancel_line(Sftp *sftp)
{
    backend_send(&sftp->backend, "\x03", 1);
}

static void completion_tab_open_paging(Sftp *sftp)
{
    backend_send(&sftp->backend, "\x09", 1);
}

static void tc_completion(TestLocal *tl, TestRemote *tr)
{
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    testremote_add_file(tr, "alma", 0);
    testremote_add_file(tr, "apple", 0);
    testremote_add_dir(tr, "korte");
    testremote_add_file(tr, "/usr/bin/ls", 0);
    testlocal_add_file(tl, "foo0", 0);
    testlocal_add_file(tl, "foo1", 0);
    testlocal_add_file(tl, "foo2", 0);
    testlocal_add_file(tl, "foo3", 0);
    testlocal_add_dir(tl, "bar0");
    testlocal_add_dir(tl, "bar1");
    testlocal_add_dir(tl, "bar2");
    testlocal_add_dir(tl, "bar3");
    backend_size(tl->sftp, 80, 24);
    testlocal_allow_cli_output(tl, true);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "chm\x09", 4);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> chmod ", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "l\x09", 2);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "lcd   lpwd  ls", true));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "lcd ba\x09", 7);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> lcd bar", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put -r fo\x09", 10);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put -r foo", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get al\x09", 7);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get alma ", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "cd k\x09", 5);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> cd korte/", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get a\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "alma   apple", true));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "help chm\x09", 9);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> help chmod", false));
    completion_cancel_line(sftp);

    completion_send(sftp, tr, "lcd q\x09", 6);
    testlocal_clear_output(tl);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_empty_output(&tl->output));
    completion_cancel_line(sftp);

    completion_send(sftp, tr, "cd q\x09", 6);
    testlocal_clear_output(tl);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_empty_output(&tl->output));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get /u\x09", 7);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get /usr/", false));
    completion_cancel_line(sftp);

    backend_send(&sftp->backend, " # cd ", 6);
    testlocal_clear_output(tl);
    completion_send(sftp, tr, "\x09", 1);
    ASSERT_TRUE(testlocal_empty_output(&tl->output));
    completion_cancel_line(sftp);

    testlocal_allow_cli_output(tl, false);
}

static void tc_completion_quotes(TestLocal *tl, TestRemote *tr)
{
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    testremote_add_file(tr, "/usr/bin/ls", 0);
    testremote_add_dir(tr, "dir with space");
    testremote_add_file(tr, "d\"ir with quote and space", 0);
    testlocal_add_file(tl, "file with space", 0);
    testlocal_allow_cli_output(tl, true);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get di\x09", 7);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get \"dir with space\"/", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get d\\\"i\x09", 9);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get \"d\\\"ir with quote and space\"", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put fi\x09", 7);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put \"file with space\"", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put fi\"le w\"i\x09", 14);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put fi\"le w\"\"ith space\"", false));
    completion_cancel_line(sftp);

    printf("\n");
    testlocal_execute(tl, "cd /usr/bin");
    testremote_process(tr);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get \x09", 5);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get ls", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "get  \"l\x09", 8);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> get  \"ls", false));
    completion_cancel_line(sftp);

    testlocal_allow_cli_output(tl, false);
}

static void tc_completion_paging(TestLocal *tl, TestRemote *tr)
{
    Sftp *sftp = container_of(tl->sftp, Sftp, backend);
    testlocal_add_file(tl, "p0age", 0);
    testlocal_add_file(tl, "p1age", 0);
    testlocal_add_dir(tl, "p2age");
    testlocal_add_dir(tl, "p3age");
    testlocal_add_file(tl, "p4age", 0);
    testlocal_add_file(tl, "p5age", 0);
    testlocal_add_file(tl, "p6age", 0);
    testlocal_add_file(tl, "p7age", 0);
    testlocal_add_dir(tl, "p8age");
    backend_size(tl->sftp, 14, 2);
    testlocal_allow_cli_output(tl, true);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put p\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p2age\\ p3age\\", true));
    testlocal_clear_output(tl);
    completion_send(sftp, tr, "\r", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p4age  p5age", true));
    ASSERT_FALSE(testlocal_find_output(&tl->output, "p6age  p7age", true));
    completion_send(sftp, tr, "q", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put p", false));
    testlocal_clear_output(tl);
    completion_send(sftp, tr, "\x09", 1);
    ASSERT_FALSE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put p\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p2age\\ p3age\\", true));
    testlocal_clear_output(tl);
    completion_send(sftp, tr, "y", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p4age  p5age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p6age  p7age", true));
    testlocal_clear_output(tl);
    completion_send(sftp, tr, " ", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p8age\\", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put p", false));
    testlocal_clear_output(tl);
    completion_send(sftp, tr, "\x09", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    completion_cancel_line(sftp);
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "lcd p\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_FALSE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p2age\\ p3age\\", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p8age\\", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> lcd p", false));
    completion_send(sftp, tr, "a", 1);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> lcd pa", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put p\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p0age  p1age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p2age\\ p3age\\", true));
    testlocal_clear_output(tl);
    backend_size(tl->sftp, 9, 3);
    ASSERT_FALSE(testlocal_find_output(&tl->output, "p4age  p5age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "sftp> put p", false));
    completion_cancel_line(sftp);

    testlocal_clear_output(tl);
    completion_send(sftp, tr, "put p\x09", 6);
    completion_tab_open_paging(sftp);
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p0age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p1age", true));
    ASSERT_TRUE(testlocal_find_output(&tl->output, "p2age\\", true));
    completion_cancel_line(sftp);

    testlocal_allow_cli_output(tl, false);
}

void testsuite_sftpbe()
{
    BEGIN_TESTSUITE(testsuite_sftpbe, TestLocal *tl, TestRemote *tr)
    ADD_TESTCASE(tc_framework)
    ADD_TESTCASE(tc_cd)
    ADD_TESTCASE(tc_put)
    ADD_TESTCASE(tc_mput)
    ADD_TESTCASE(tc_reput)
    ADD_TESTCASE(tc_get)
    ADD_TESTCASE(tc_mget)
    ADD_TESTCASE(tc_reget)
    ADD_TESTCASE(tc_mkdir)
    ADD_TESTCASE(tc_rm)
    ADD_TESTCASE(tc_mv)
    ADD_TESTCASE(tc_chmod)
    ADD_TESTCASE(tc_lcd)
    ADD_TESTCASE(tc_bye)
    ADD_TESTCASE(tc_ctrlc)
    ADD_TESTCASE(tc_connection_fatal)
    ADD_TESTCASE(tc_pwdline)
    ADD_TESTCASE(tc_completion)
    ADD_TESTCASE(tc_completion_quotes)
    ADD_TESTCASE(tc_completion_paging)
    END_TESTSUITE()

    size_t i = 0;
    while (testsuite_sftpbe[i].function) {
        printf("-- Executing test case: %s\n", testsuite_sftpbe[i].name);
        TestRemote tr;
        TestLocal tl;
        testremote_init(&tr);
        testlocal_init(&tl, &tr, "UTF-8");
        testsuite_sftpbe[i].function(&tl, &tr);
        testlocal_uninit(&tl);
        testremote_uninit(&tr);
        i++;
    }
}
