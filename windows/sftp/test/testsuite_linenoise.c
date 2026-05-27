#include "linenoise/linenoise.h"
#include "testassert.h"
#include "testsuite.h"
#include <string.h>
#include <stdbool.h>

/* UTF-8 for U+00E9 LATIN SMALL LETTER E WITH ACUTE */
#define S_EACUTE "\xC3\xA9"
/* UTF-8 for U+4F60 CJK UNIFIED IDEOGRAPH-4F60 (你), terminal width 2 */
#define S_NI "\xE4\xBD\xA0"
#define S_UP    "\x1b[A"
#define S_DOWN  "\x1b[B"

typedef struct {
    unsigned char data[4096];
    size_t len;
    size_t pos;
} byte_queue;

static void queue_clear(byte_queue *q)
{
    q->len = 0;
    q->pos = 0;
}

static void queue_push(byte_queue *q, const void *bytes, size_t n)
{
    ASSERT_TRUE(q->len + n <= sizeof(q->data));
    if (q->len + n > sizeof(q->data))
        return;
    memcpy(q->data + q->len, bytes, n);
    q->len += n;
}

static void queue_push_str(byte_queue *q, const char *s)
{
    queue_push(q, s, strlen(s));
}

static ssize_t mock_read(void *buffer, size_t count, void *ctx)
{
    byte_queue *q = ctx;
    unsigned char *out = buffer;
    size_t n = 0;

    while (n < count && q->pos < q->len)
        out[n++] = q->data[q->pos++];
    return (ssize_t)n;
}

static ssize_t mock_write(const void *buffer, size_t count, void *ctx)
{
    (void)buffer;
    (void)ctx;
    return (ssize_t)count;
}

/* Width 2 for typical East Asian full-width characters; 1 for Latin etc. */
static int test_wcwidth(unsigned int ucs)
{
    if (ucs < 0x20u) {
        return 0;
    }
    if (ucs >= 0x4E00u && ucs <= 0x9FFFu) {
        return 2;
    }
    return 1;
}

static bool expect_state(linenoiseState *l, const char *want_buf,
                         size_t want_len, size_t want_pos)
{
    int t = assert_fail_count;
    ASSERT_TRUE(l->len == want_len);
    ASSERT_TRUE(l->pos == want_pos);
    ASSERT_TRUE(strcmp(l->buf, want_buf) == 0);
    return assert_fail_count == t;
}

static bool feed_expect(linenoiseState *l, linenoiseFeedResult want)
{
    return linenoiseEditFeed(l) == want;
}

static bool feed_expect_more(linenoiseState *l, size_t count)
{
    int t = assert_fail_count;
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(feed_expect(l, LINENOISEFEED_MORE));
    }
    return assert_fail_count == t;
}

static void feed_line_commit(linenoiseState *l, byte_queue *q, const char *line)
{
    queue_push_str(q, line);
    queue_push_str(q, "\x0d");
    linenoiseFeedResult r;
    while ((r = linenoiseEditFeed(l)) == LINENOISEFEED_MORE);
    ASSERT_TRUE(r == LINENOISEFEED_FINISH);
}

static void feed_cancel(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "\x03");
    linenoiseFeedResult r;
    while ((r = linenoiseEditFeed(l)) == LINENOISEFEED_MORE);
    ASSERT_TRUE(r == LINENOISEFEED_CANCEL);
}

static void linenoise_edit_start(linenoiseState *l, byte_queue *q)
{
    queue_clear(q);
    linenoiseEditStart(l, mock_read, mock_write, q, "> ", 80, l->history, test_wcwidth);
}

static void setup_case(linenoiseState *l, linenoiseHistory *h, byte_queue *q)
{
    queue_clear(q);
    linenoiseInitHistory(h);
    linenoiseHistorySetMaxLen(h, 32);
    linenoiseEditStart(l, mock_read, mock_write, q, "> ", 80, h, test_wcwidth);
}

static void teardown_case(linenoiseState *l)
{
    linenoiseFreeHistory(l->history);
}

static void test_initial_buffer_after_start(linenoiseState *l, byte_queue *q)
{
    ASSERT_TRUE(l->buf[0] == '\0');
    ASSERT_TRUE(l->buflen == sizeof(l->t) - 1);
    ASSERT_TRUE(l->pos == 0);
    ASSERT_TRUE(l->len == 0);
}

static void test_insert_at_end(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "a" S_EACUTE "b" S_NI);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a", 1, 1));
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a" S_EACUTE, 3, 3));
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a" S_EACUTE "b", 4, 4));
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a" S_EACUTE "b" S_NI, 7, 7));
}

static void test_insert_in_middle(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "x" S_NI);
    ASSERT_TRUE(feed_expect_more(l, 2));
    ASSERT_TRUE(expect_state(l, "x" S_NI, 4, 4));
    queue_push_str(q, "\x1b[D"); /* ESC [ D left */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 4 && l->pos == 1);
    queue_push_str(q, "b");
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "xb" S_NI, 5, 2));
}

static void test_arrow_home_end(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "x" S_EACUTE S_NI);
    ASSERT_TRUE(feed_expect_more(l, 3));

    queue_push_str(q, "\x1b[D"); /* left */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 3);

    queue_push_str(q, "\x1b[C"); /* right */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 6);

    queue_push_str(q, "\x1b[H"); /* home */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 0);

    queue_push_str(q, "\x1b[F"); /* end */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 6);

    queue_push_str(q, "\x1bOH"); /* ESC O H home */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 0);

    queue_push_str(q, "\x1bOF"); /* ESC O F end */
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(l->len == 6 && l->pos == 6);
}

static void test_backspace(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "q" S_NI "\x7f");
    ASSERT_TRUE(feed_expect_more(l, 3));
    ASSERT_TRUE(expect_state(l, "q", 1, 1));

    queue_push_str(q, S_EACUTE "r\x08");
    ASSERT_TRUE(feed_expect_more(l, 3));
    ASSERT_TRUE(expect_state(l, "q" S_EACUTE, 3, 3));
}

static void test_ctrl_d_delete(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "a" S_NI "z\x1b[D\x04");
    ASSERT_TRUE(feed_expect_more(l, 5));
    ASSERT_TRUE(expect_state(l, "a" S_NI, 4, 4));
    queue_push_str(q, "\x1b[D\x1b[3~");
    ASSERT_TRUE(feed_expect_more(l, 2));
    ASSERT_TRUE(expect_state(l, "a", 1, 1));
}

static void test_ctrl_u_clears_line(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, S_EACUTE S_NI "h\x15");
    ASSERT_TRUE(feed_expect_more(l, 4));
    ASSERT_TRUE(expect_state(l, "", 0, 0));
}

static void test_ctrl_k_kills_suffix(linenoiseState *l, byte_queue *q)
{
    /* "ab你cd" — three left arrows land at pos 2 (after "ab"), Ctrl-K drops 你cd */
    queue_push_str(q, "ab" S_NI "cd\x1b[D\x1b[D\x1b[D\x0b");
    ASSERT_TRUE(feed_expect_more(l, 9));
    ASSERT_TRUE(expect_state(l, "ab", 2, 2));
}

static void test_ctrl_w_delete_prev_word(linenoiseState *l, byte_queue *q)
{
    /* "a" + é + ASCII space + 你 — Ctrl-W (0x17) deletes previous word (你) */
    queue_push_str(q, "a" S_EACUTE " " S_NI "\x17");
    ASSERT_TRUE(feed_expect_more(l, 6));
    ASSERT_TRUE(expect_state(l, "a" S_EACUTE " ", 4, 4));
}

static void test_ctrl_t_swap_codepoints(linenoiseState *l, byte_queue *q)
{
    /* "a" + é + 你 — one left lands between é and 你; Ctrl-T (0x14) swaps them */
    queue_push_str(q, "a" S_EACUTE S_NI "\x1b[D\x14");
    ASSERT_TRUE(feed_expect_more(l, 5));
    expect_state(l, "a" S_NI S_EACUTE, 6, 6);
}

static void test_ctrl_b_f_moves_cursor(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, S_EACUTE S_NI "\x02\x06");
    ASSERT_TRUE(feed_expect_more(l, 4));
    ASSERT_TRUE(expect_state(l, S_EACUTE S_NI, 5, 5));
}

static void test_ctrl_a_e(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "a" S_NI S_EACUTE "\x01\x05");
    ASSERT_TRUE(feed_expect_more(l, 5));
    ASSERT_TRUE(expect_state(l, "a" S_NI S_EACUTE, 6, 6));
    queue_push_str(q, "\x01");
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a" S_NI S_EACUTE, 6, 0));
}

static void test_enter(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "Z" S_NI S_EACUTE "\x0d");
    ASSERT_TRUE(feed_expect_more(l, 3));
    ASSERT_TRUE(feed_expect(l, LINENOISEFEED_FINISH));
    ASSERT_TRUE(expect_state(l, "Z" S_NI S_EACUTE, 6, 6));
}

static void test_ctrl_c(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "x" S_NI "\x03");
    ASSERT_TRUE(feed_expect_more(l, 2));
    ASSERT_TRUE(feed_expect(l, LINENOISEFEED_CANCEL));
    ASSERT_TRUE(expect_state(l, "x" S_NI, 4, 4));
}

static void test_ctrl_d_empty(linenoiseState *l, byte_queue *q)
{
    queue_push_str(q, "\x04");
    ASSERT_TRUE(feed_expect(l, LINENOISEFEED_EXIT));
    ASSERT_TRUE(expect_state(l, "", 0, 0));
}

/* Three single-letter lines committed; then browse with Up/Down. */
static void test_history(linenoiseState *l, byte_queue *q)
{
    feed_line_commit(l, q, "a");
    linenoise_edit_start(l, q);
    feed_line_commit(l, q, "b");
    linenoise_edit_start(l, q);
    feed_line_commit(l, q, "b");
    linenoise_edit_start(l, q);

    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "b", 1, 1));
    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a", 1, 1));
    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "a", 1, 1));

    queue_push_str(q, S_DOWN);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "b", 1, 1));
    queue_push_str(q, S_DOWN);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "", 0, 0));
    queue_push_str(q, S_DOWN);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "", 0, 0));

    queue_push_str(q, "draft");
    ASSERT_TRUE(feed_expect_more(l, 5));
    ASSERT_TRUE(expect_state(l, "draft", 5, 5));
    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "b", 1, 1));
    queue_push_str(q, S_DOWN);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "draft", 5, 5));

    feed_cancel(l, q);
    linenoise_edit_start(l, q);

    queue_push_str(q, S_UP S_UP);
    ASSERT_TRUE(feed_expect_more(l, 2));
    ASSERT_TRUE(expect_state(l, "a", 1, 1));
    queue_push_str(q, "mod");
    ASSERT_TRUE(feed_expect_more(l, 3));
    ASSERT_TRUE(expect_state(l, "amod", 4, 4));
    queue_push_str(q, S_DOWN);
    ASSERT_TRUE(feed_expect_more(l, 1));
    queue_push_str(q, "mod");
    ASSERT_TRUE(feed_expect_more(l, 3));
    ASSERT_TRUE(expect_state(l, "bmod", 4, 4));

    feed_cancel(l, q);
    linenoise_edit_start(l, q);

    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "b", 1, 1));
    queue_push_str(q, S_UP);
    ASSERT_TRUE(feed_expect_more(l, 1));
    ASSERT_TRUE(expect_state(l, "amod", 4, 4));
}

void testsuite_linenoise()
{
    BEGIN_TESTSUITE(testsuite_linenoise, linenoiseState *l, byte_queue *q)
    ADD_TESTCASE(test_initial_buffer_after_start)
    ADD_TESTCASE(test_insert_at_end)
    ADD_TESTCASE(test_insert_in_middle)
    ADD_TESTCASE(test_arrow_home_end)
    ADD_TESTCASE(test_backspace)
    ADD_TESTCASE(test_enter)
    ADD_TESTCASE(test_ctrl_a_e)
    ADD_TESTCASE(test_ctrl_b_f_moves_cursor)
    ADD_TESTCASE(test_ctrl_c)
    ADD_TESTCASE(test_ctrl_d_delete)
    ADD_TESTCASE(test_ctrl_d_empty)
    ADD_TESTCASE(test_ctrl_k_kills_suffix)
    ADD_TESTCASE(test_ctrl_u_clears_line)
    ADD_TESTCASE(test_ctrl_w_delete_prev_word)
    ADD_TESTCASE(test_ctrl_t_swap_codepoints)
    ADD_TESTCASE(test_history)
    END_TESTSUITE();

    printf("-- Executing linenoise tests\n");

    linenoiseState l;
    linenoiseHistory h;
    byte_queue q;
    for (int i = 0; testsuite_linenoise[i].function; i++) {
        setup_case(&l, &h, &q);
        testsuite_linenoise[i].function(&l, &q);
        teardown_case(&l);
    }
}
