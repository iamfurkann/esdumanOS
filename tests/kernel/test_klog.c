/*
 * File: test_klog.c
 * Purpose: The log as a record - its fields, its numbering, what it drops, and
 *          the two ways user space reaches it.
 *
 * The ring used to hold bytes, and every question about a record was a question
 * about parsing a string: when did this happen, how many did we lose, show me
 * only the errors. None of them could be answered, so none of them were asked,
 * and so none of them were tested. This file is what those questions look like
 * once the answers exist.
 *
 * Self-contained on purpose. It writes its own markers and asserts on those
 * rather than on whatever the boot happened to leave behind, because it runs
 * after a module that deliberately fills the ring - and a test that depends on
 * another module's leftovers passes in one order and fails in another.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "klog.h"
#include "devfs.h"
#include "process.h"
#include "libft.h"
#include "errno.h"

/**
 * @brief Finds a marker in a rendered record by index.
 *
 * @param index  Record position, counted from the oldest still held.
 * @param needle Text to look for.
 * @return Non-zero when the record at @p index contains @p needle.
 */
static int record_has(int index, const char *needle) {
    char line[KLOG_LINE_MAX];
    int got = klog_read(line, (int)sizeof(line), index);
    if (got <= 0) return 0;

    int nlen = 0;
    while (needle[nlen]) nlen++;

    for (int i = 0; i + nlen <= got; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            if (line[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/**
 * @brief Verifies the record ring, its counters, and /dev/kmsg.
 *
 * Expected behavior:
 * - A record keeps its level, module and text as fields rather than as a prefix.
 * - Sequence numbers rise and are never reused, so a gap is detectable.
 * - Every record carries when it happened.
 * - Clearing empties the log without renumbering it.
 * - The threshold suppresses what is below it, and can be moved.
 * - A user record is marked as one and carries its author.
 * - /dev/kmsg reads records in order and stops when the reader catches up.
 *
 * Edge cases covered:
 * - A sequence number that never existed, and one that has been overwritten.
 * - The rate limit on user records.
 * - A record longer than the field that holds it.
 */
void run_klog_tests(void) {
    printk("\n--- Kernel Log (record ring) Tests ---\n");

    int saved_level = klog_get_level();
    klog_set_level(LOG_LEVEL_DEBUG);

    /* ------------------------------------------------------------------
     * A record is fields, not a prefix on a string.
     * ------------------------------------------------------------------ */
    uint32_t before_seq = klog_next_seq();
    klog_record(LOG_LEVEL_WARN, "KTEST", "fieldprobe");
    uint32_t after_seq = klog_next_seq();

    KTEST_ASSERT(after_seq == before_seq + 1, "[KLOG] writing a record advances the sequence by one");

    const klog_record_t *rec = klog_by_seq(before_seq);
    KTEST_ASSERT(rec != 0, "[KLOG] a record can be fetched by its sequence number");

    if (rec != 0) {
        KTEST_ASSERT(rec->level == LOG_LEVEL_WARN,
                     "[STRICT] [KLOG] the level is a field, not the first seven characters");
        KTEST_ASSERT(ft_strcmp(rec->module, "KTEST") == 0, "[KLOG] the module is kept whole");
        KTEST_ASSERT(ft_strcmp(rec->text, "fieldprobe") == 0, "[KLOG] and so is the text");
        KTEST_ASSERT(rec->from_user == 0, "[KLOG] a kernel record is not marked as coming from a program");
    }

    /* ------------------------------------------------------------------
     * Sequence numbers that never existed, and the time a record carries.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(klog_by_seq(0) == 0, "[STRICT] [KLOG] sequence zero is never a record");
    KTEST_ASSERT(klog_by_seq(klog_next_seq()) == 0,
                 "[STRICT] [KLOG] a sequence not yet handed out is not a record");
    KTEST_ASSERT(klog_by_seq(klog_next_seq() + 1000u) == 0,
                 "[KLOG] and neither is one far in the future");

    uint32_t t0 = klog_next_seq();
    klog_record(LOG_LEVEL_INFO, "KTEST", "timeprobe-a");
    klog_record(LOG_LEVEL_INFO, "KTEST", "timeprobe-b");

    const klog_record_t *ra = klog_by_seq(t0);
    const klog_record_t *rb = klog_by_seq(t0 + 1u);

    if (ra != 0 && rb != 0) {
        KTEST_ASSERT(rb->ticks >= ra->ticks,
                     "[STRICT] [KLOG] time does not run backwards between records");
        KTEST_ASSERT(rb->seq == ra->seq + 1u, "[KLOG] consecutive records get consecutive numbers");
    }

    /* ------------------------------------------------------------------
     * A message longer than the field that holds it.
     * ------------------------------------------------------------------ */
    char long_text[KLOG_TEXT_MAX + 64];
    for (uint32_t i = 0; i < sizeof(long_text) - 1; i++) long_text[i] = 'x';
    long_text[sizeof(long_text) - 1] = '\0';

    uint32_t long_seq = klog_next_seq();
    klog_record(LOG_LEVEL_INFO, "KTEST", long_text);
    const klog_record_t *rl = klog_by_seq(long_seq);

    if (rl != 0) {
        uint32_t len = 0;
        while (rl->text[len]) len++;
        KTEST_ASSERT(len == KLOG_TEXT_MAX - 1u,
                     "[STRICT] [KLOG] an over-long message is truncated to the field, not past it");
        KTEST_ASSERT(rl->text_len == (uint8_t)len, "[KLOG] and the recorded length agrees with it");
    }

    /* ------------------------------------------------------------------
     * The rendered forms.
     * ------------------------------------------------------------------ */
    char line[KLOG_LINE_MAX];
    int n = klog_format(rec ? rec : ra, line, (int)sizeof(line));

    KTEST_ASSERT(n > 0 && line[0] == '[', "[KLOG] a rendered record opens with its timestamp");
    KTEST_ASSERT(line[n - 1] == '\n', "[KLOG] and ends its own line");

    char kline[KLOG_LINE_MAX];
    int kn = klog_format_kmsg(rec ? rec : ra, kline, (int)sizeof(kline));

    KTEST_ASSERT(kn > 0 && kline[0] >= '0' && kline[0] <= '4',
                 "[STRICT] [KMSG] the structured form leads with the level as a number");

    int semi = -1;
    for (int i = 0; i < kn; i++) if (kline[i] == ';') { semi = i; break; }
    KTEST_ASSERT(semi > 0, "[KMSG] the machine-readable fields are separated from the text");

    /* ------------------------------------------------------------------
     * The threshold suppresses what is below it.
     * ------------------------------------------------------------------ */
    klog_set_level(LOG_LEVEL_ERROR);
    KTEST_ASSERT(klog_get_level() == LOG_LEVEL_ERROR, "[KLOG] the threshold reads back as it was set");

    uint32_t quiet_seq = klog_next_seq();
    klog_record(LOG_LEVEL_DEBUG, "KTEST", "suppressed-debug");
    KTEST_ASSERT(klog_next_seq() == quiet_seq,
                 "[STRICT] [KLOG] a record below the threshold is not stored at all");

    klog_record(LOG_LEVEL_ERROR, "KTEST", "kept-error");
    KTEST_ASSERT(klog_next_seq() == quiet_seq + 1u, "[KLOG] and one at the threshold still is");

    klog_set_level(LOG_LEVEL_DEBUG);

    /* ------------------------------------------------------------------
     * A user record is marked as one.
     *
     * The distinction is the point: a log that cannot tell a program's words
     * from the kernel's is a log a program can put words into the kernel's
     * mouth in.
     * ------------------------------------------------------------------ */
    uint32_t user_seq = klog_next_seq();
    KTEST_ASSERT(klog_user_record(LOG_LEVEL_INFO, "userprobe", 1000) == E_OK,
                 "[KMSG] a user record is accepted");

    const klog_record_t *ru = klog_by_seq(user_seq);
    if (ru != 0) {
        KTEST_ASSERT(ru->from_user != 0, "[STRICT] [KMSG] and is marked as coming from a program");
        KTEST_ASSERT(ru->uid == 1000, "[STRICT] [KMSG] and carries the author's uid");
        KTEST_ASSERT(ru->from_user != 0 && ft_strcmp(ru->module, "USER") == 0,
                     "[KMSG] user records are filed under their own module");
    }

    /* ------------------------------------------------------------------
     * /dev/kmsg, through the device handlers a program reaches.
     * ------------------------------------------------------------------ */
    if (current_task != 0) {
        uint32_t saved_uid = current_task->uid;
        uint32_t saved_cursor = current_task->kmsg_seq;

        /* Read from the oldest held record forward, and stop. A reader that
         * never stopped would hang the shell rather than fail a test. */
        current_task->kmsg_seq = 0;

        uint8_t kbuf[KLOG_LINE_MAX];
        int reads = 0;
        int got;
        while ((got = dev_kmsg_read(kbuf, (int)sizeof(kbuf))) > 0) {
            reads++;
            if (reads > (int)KLOG_RECORDS + 16) break;
        }

        KTEST_ASSERT(reads > 0, "[KMSG] reading the device returns records");
        KTEST_ASSERT(reads <= (int)KLOG_RECORDS,
                     "[STRICT] [KMSG] a reader catches up and stops rather than looping forever");
        KTEST_ASSERT(dev_kmsg_read(kbuf, (int)sizeof(kbuf)) == 0,
                     "[KMSG] and stays caught up once it has");

        /* Write, then read it back: the round trip the read-only alternative
         * could not have tested at all. */
        current_task->uid = 0;
        const char *msg = "<3>kmsgroundtrip";
        /* The length the string actually is. Passing sizeof or a hand-counted
         * number here sends the terminator through as data. */
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;

        int wrote = dev_kmsg_write((const uint8_t *)msg, msg_len);
        KTEST_ASSERT(wrote > 0, "[KMSG] root can write a record");

        got = dev_kmsg_read(kbuf, (int)sizeof(kbuf));
        KTEST_ASSERT(got > 0, "[STRICT] [KMSG] and the reader sees the record it just wrote");

        if (got > 0) {
            /*
             * The needle's own length, not a number written next to it. This
             * bound was 16 against a 13-byte needle, and the text sits at the
             * end of the rendered line - so the last position the loop tried was
             * always two short of the only place the match could be, and the
             * search could never succeed however correct the kernel was.
             */
            const char *needle = "kmsgroundtrip";
            int need_len = 0;
            while (needle[need_len]) need_len++;

            int found = 0;
            for (int i = 0; i + need_len <= got; i++) {
                if (ft_memcmp(&kbuf[i], needle, (size_t)need_len) == 0) { found = 1; break; }
            }
            KTEST_ASSERT(found, "[STRICT] [KMSG] with the text it was given");
            KTEST_ASSERT(kbuf[0] == '3', "[KMSG] and the level the <N> prefix asked for");
        }

        /* Not root: refused. */
        current_task->uid = 1000;
        KTEST_ASSERT(dev_kmsg_write((const uint8_t *)"nope", 4) == E_PERM,
                     "[STRICT] [KMSG] a program that is not root cannot write to the log");

        current_task->uid = saved_uid;
        current_task->kmsg_seq = saved_cursor;
    }

    /* ------------------------------------------------------------------
     * Clearing empties the log without renumbering it.
     *
     * A reader holding a cursor from before the clear has to be able to tell
     * that what it pointed at is gone, rather than be handed a different record
     * wearing the same number.
     * ------------------------------------------------------------------ */
    uint32_t seq_before_clear = klog_next_seq();
    klog_clear();

    KTEST_ASSERT(klog_held() == 0, "[STRICT] [KLOG] clearing leaves nothing held");
    KTEST_ASSERT(klog_next_seq() == seq_before_clear,
                 "[STRICT] [KLOG] and does not reset the numbering");
    KTEST_ASSERT(klog_by_seq(seq_before_clear - 1u) == 0,
                 "[KLOG] a record from before the clear is no longer reachable");

    klog_record(LOG_LEVEL_INFO, "KTEST", "afterclear");
    KTEST_ASSERT(klog_held() == 1, "[KLOG] and the next record starts the log again");
    KTEST_ASSERT(record_has(0, "afterclear"), "[KLOG] at index zero");

    /* ------------------------------------------------------------------
     * Wrapping, and the count of what went.
     * ------------------------------------------------------------------ */
    uint32_t dropped_before = klog_dropped();

    for (uint32_t i = 0; i < KLOG_RECORDS + 4u; i++) {
        klog_record(LOG_LEVEL_INFO, "KTEST", "wrapfiller");
    }

    KTEST_ASSERT(klog_held() == KLOG_RECORDS,
                 "[STRICT] [KLOG] a full ring holds exactly its capacity");
    KTEST_ASSERT(klog_dropped() >= dropped_before + 4u,
                 "[STRICT] [KLOG] and counts every record it overwrote");
    KTEST_ASSERT(!record_has(0, "afterclear"),
                 "[KLOG] the oldest record really was overwritten, not preserved");

    /* ------------------------------------------------------------------
     * The rate limit, last of all and deliberately so.
     *
     * This test works by using the allowance up, which means everything after it
     * that writes a user record is refused until the window expires. It sat in
     * the middle of this file to begin with and took the /dev/kmsg round trip
     * down with it: the write was refused, the read found nothing to return, and
     * two assertions failed against a kernel that was behaving exactly as
     * designed.
     *
     * At the end it costs nothing here, and it also leaves the rest of the
     * kernel suite and the Ring 3 payload's startup between the exhausted window
     * and the payload's own kmsg write - which would otherwise have been the
     * same failure again, intermittently, depending on how fast the machine ran.
     *
     * Kernel records are deliberately not limited, so this only has to hold for
     * the ones a program can produce.
     * ------------------------------------------------------------------ */
    int refused = 0;
    for (int i = 0; i < 200; i++) {
        if (klog_user_record(LOG_LEVEL_INFO, "floodprobe", 1000) == E_AGAIN) { refused = 1; break; }
    }
    KTEST_ASSERT(refused, "[STRICT] [KMSG] a program cannot write the ring empty in a loop");

    klog_set_level(saved_level);
}
