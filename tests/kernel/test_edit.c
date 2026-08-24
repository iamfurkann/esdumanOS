/*
 * File: test_edit.c
 * Purpose: The line arithmetic /bin/edit is built on.
 *
 * An editor is wrong in ways that look right. An off-by-one in "where does this
 * line start" moves the cursor one column for the rest of the session, and
 * nothing on screen says which of the two numbers - the one the editor thinks
 * and the one the user sees - is the wrong one. So the arithmetic is tested
 * here, away from any screen, with buffers whose every offset can be written
 * down by hand.
 *
 * It can be tested at all because it lives in a header. /bin/edit is a
 * freestanding translation unit with no link step, like every program in /bin,
 * so the only way the suite can reach its logic is for the logic to be
 * includable - which is why include/editbuf.h makes no system calls and touches
 * no globals. umalloc.h is header-only for the same reason.
 *
 * Undo and search were written into that header rather than into the editor for
 * exactly this reason. An undo that is wrong is worse than no undo - it hands
 * back a file that is not the one the user had - and the only way to know it is
 * right is to run it somewhere the screen is not involved.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "editbuf.h"
#include "libft.h"

/** Room for every fixture below, with space to insert into. */
#define EB_TEST_CAP 256

static char eb_storage[EB_TEST_CAP];

/*
 * The undo log's storage, and deliberately small.
 *
 * Eight records and thirty-two bytes are not what the editor gives it - that is
 * 256 and 8 KB - but the interesting behaviour is what happens when the log runs
 * out, and a log sized like the editor's could only be filled by a test that
 * takes longer to read than the code it checks.
 */
#define EB_UNDO_RECS  8
#define EB_UNDO_ARENA 32

static ebrec_t eb_undo_recs[EB_UNDO_RECS];
static char eb_undo_arena[EB_UNDO_ARENA];

/**
 * @brief Points a log at the storage above and empties it.
 *
 * @param u Log to set up.
 */
static void eb_undo_setup(ebundo_t *u) {
    u->recs = eb_undo_recs;
    u->rec_cap = EB_UNDO_RECS;
    u->arena = eb_undo_arena;
    u->arena_cap = EB_UNDO_ARENA;
    eb_undo_reset(u);
}

/**
 * @brief Fills a buffer with a string.
 *
 * @param b Buffer to set up.
 * @param text Contents; the terminator is not part of the buffer.
 */
static void eb_load(editbuf_t *b, const char *text) {
    int n = 0;

    while (text[n] != '\0' && n < EB_TEST_CAP - 1) { eb_storage[n] = text[n]; n++; }

    b->data = eb_storage;
    b->len = n;
    b->cap = EB_TEST_CAP;
}

/**
 * @brief Whether a buffer holds exactly this text.
 *
 * @param b Buffer.
 * @param text Expected contents.
 * @return 1 when they match, length included.
 */
static int eb_is(const editbuf_t *b, const char *text) {
    int n = 0;

    while (text[n] != '\0') n++;
    if (b->len != n) return 0;

    for (int i = 0; i < n; i++) {
        if (b->data[i] != text[i]) return 0;
    }
    return 1;
}

/**
 * @brief Verifies the undo log: what it records, groups, and gives back.
 *
 * Expected behavior:
 * - Every edit that changes the buffer can be taken back exactly.
 * - A run of typing is one record and one undo, not one per keystroke.
 * - A group is the unit of undo; changes in different groups come back apart.
 * - The cursor returned is where the group started, not where it ended.
 *
 * Edge cases covered:
 * - A log with no storage at all, which must still edit.
 * - Running out of records, and running out of arena, which drop whole groups.
 * - Deleting the last line of a file with no trailing newline, where the
 *   deletion takes a newline that is not on the line being deleted.
 */
static void run_edit_undo_tests(void) {
    editbuf_t b;
    ebundo_t u;

    /* ------------------------------------------------------------------
     * An edit with no log is still an edit. The editor opens this way when
     * the allocation fails, and it has to keep working.
     * ------------------------------------------------------------------ */
    eb_load(&b, "ab");
    KTEST_ASSERT(eb_undo_insert(&b, 0, 0, 'z', 0) == 1 && eb_is(&b, "zab"),
                 "[STRICT] [EDIT] an edit with no undo log still edits");
    KTEST_ASSERT(eb_undo_apply(&b, 0) == -1,
                 "[STRICT] [EDIT] and there is nothing to undo");

    /* ------------------------------------------------------------------
     * One insert, taken back.
     * ------------------------------------------------------------------ */
    eb_load(&b, "ac");
    eb_undo_setup(&u);
    KTEST_ASSERT(eb_undo_apply(&b, &u) == -1,
                 "[EDIT] an empty log has nothing to undo");

    eb_undo_group(&u);
    eb_undo_insert(&b, &u, 1, 'b', 1);
    KTEST_ASSERT(eb_is(&b, "abc"), "[EDIT] a recorded insert still inserts");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 1 && eb_is(&b, "ac"),
                 "[EDIT] and undo puts the buffer back and says where the cursor was");

    /* ------------------------------------------------------------------
     * A run of typing is one record. This is what makes u usable: a sentence
     * is one thing the user did, and giving it back a letter at a time would
     * mean pressing u once per keystroke.
     * ------------------------------------------------------------------ */
    eb_load(&b, "");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    for (int i = 0; i < 5; i++) eb_undo_insert(&b, &u, b.len, (char)('a' + i), b.len);

    KTEST_ASSERT(eb_is(&b, "abcde"), "[EDIT] typing inserts what was typed");
    KTEST_ASSERT(u.nrecs == 1,
                 "[STRICT] [EDIT] a run of typing costs one record, not one per key");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 0 && eb_is(&b, ""),
                 "[STRICT] [EDIT] and one undo takes the whole run back");

    /* Only a run, though. An insert somewhere other than where the last one
     * ended is a separate thing to remember, even inside the same group. */
    eb_load(&b, "xy");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    eb_undo_insert(&b, &u, 0, 'a', 0);
    eb_undo_insert(&b, &u, 3, 'b', 3);
    KTEST_ASSERT(eb_is(&b, "axyb") && u.nrecs == 2,
                 "[STRICT] [EDIT] an insert away from the run starts a new record");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 0 && eb_is(&b, "xy"),
                 "[STRICT] [EDIT] and the group still comes back in one undo, oldest cursor first");

    /* ------------------------------------------------------------------
     * Groups are the unit of undo.
     * ------------------------------------------------------------------ */
    eb_load(&b, "");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    eb_undo_insert(&b, &u, 0, 'a', 0);
    eb_undo_group(&u);
    eb_undo_insert(&b, &u, 1, 'b', 1);

    KTEST_ASSERT(u.nrecs == 2, "[EDIT] a new group does not join the run before it");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 1 && eb_is(&b, "a"),
                 "[STRICT] [EDIT] undo takes back the newest group only");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 0 && eb_is(&b, ""),
                 "[EDIT] and the one before it next");

    /* ------------------------------------------------------------------
     * Deletes, including the one whose span is not the line it was asked about.
     * ------------------------------------------------------------------ */
    eb_load(&b, "abc");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    KTEST_ASSERT(eb_undo_delete(&b, &u, 1, 1) == 1 && eb_is(&b, "ac"),
                 "[EDIT] a recorded delete still deletes");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 1 && eb_is(&b, "abc"),
                 "[EDIT] and undo puts the byte back where it was");

    eb_load(&b, "one\ntwo\nthree");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    KTEST_ASSERT(eb_undo_delete_line(&b, &u, 5, 5) == 4 && eb_is(&b, "one\nthree"),
                 "[EDIT] a recorded line delete still takes the line and its newline");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 5 && eb_is(&b, "one\ntwo\nthree"),
                 "[EDIT] and undo puts both back");

    /*
     * The case the deletion itself had to be corrected for. Deleting the last
     * line of a file with no trailing newline takes the newline *before* it, so
     * what undo has to give back starts one byte earlier than the line does -
     * which it only gets right because it saves the span eb_delete_line() uses
     * rather than working one out of its own.
     */
    eb_load(&b, "one\ntwo");
    eb_undo_setup(&u);
    eb_undo_group(&u);
    KTEST_ASSERT(eb_undo_delete_line(&b, &u, 5, 5) == 0 && eb_is(&b, "one"),
                 "[EDIT] deleting the last line takes the newline before it");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 5 && eb_is(&b, "one\ntwo"),
                 "[STRICT] [EDIT] and undo puts that newline back too");

    /* ------------------------------------------------------------------
     * Running out of records. The oldest group goes, not the newest, and not
     * half of one.
     * ------------------------------------------------------------------ */
    eb_load(&b, "");
    eb_undo_setup(&u);
    for (int i = 0; i < EB_UNDO_RECS + 1; i++) {
        eb_undo_group(&u);
        eb_undo_insert(&b, &u, b.len, (char)('a' + i), b.len);
    }

    KTEST_ASSERT(eb_is(&b, "abcdefghi"),
                 "[EDIT] a full log does not stop the buffer being edited");
    KTEST_ASSERT(u.nrecs == EB_UNDO_RECS && u.lost == 1,
                 "[STRICT] [EDIT] it drops a change to make room, and says it did");

    for (int i = 0; i < EB_UNDO_RECS; i++) eb_undo_apply(&b, &u);
    KTEST_ASSERT(eb_is(&b, "a"),
                 "[STRICT] [EDIT] what it dropped was the oldest change, not the newest");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == -1,
                 "[EDIT] and there is nothing left in the log");

    /* ------------------------------------------------------------------
     * Running out of arena. Three line deletions of eleven bytes each do not
     * fit in thirty-two, so the first one's bytes are given up - and the
     * records that remain have to be told their bytes moved.
     * ------------------------------------------------------------------ */
    eb_load(&b, "aaaaaaaaaa\nbbbbbbbbbb\ncccccccccc\n");
    eb_undo_setup(&u);
    for (int i = 0; i < 3; i++) {
        eb_undo_group(&u);
        eb_undo_delete_line(&b, &u, 0, 0);
    }

    KTEST_ASSERT(eb_is(&b, "") && u.lost == 1,
                 "[EDIT] the arena runs out and says so");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 0 && eb_is(&b, "cccccccccc\n"),
                 "[STRICT] [EDIT] the newest deletion comes back whole");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == 0 && eb_is(&b, "bbbbbbbbbb\ncccccccccc\n"),
                 "[STRICT] [EDIT] and so does the one before it, from bytes that were moved");
    KTEST_ASSERT(eb_undo_apply(&b, &u) == -1,
                 "[STRICT] [EDIT] the oldest is the one that was given up");
}

/**
 * @brief Verifies the edit buffer's line arithmetic, its edits and its searches.
 *
 * Expected behavior:
 * - A line's start and end bracket exactly the characters on it.
 * - The offset one past the last byte is a valid cursor position.
 * - A vertical move keeps the column it aims for, clamped to short lines.
 * - Insert and delete move the bytes after them and nothing else.
 * - Deleting a line takes its newline with it, from whichever side has one.
 * - A search moves off the match it started on, and wraps rather than failing.
 *
 * Edge cases covered:
 * - An empty buffer, which is one line.
 * - A buffer ending in a newline, which is not one line more.
 * - An offset on the newline itself, which belongs to the line before it.
 * - An empty pattern, and one longer than the buffer.
 *
 * The undo log is checked by run_edit_undo_tests(), called at the end.
 */
void run_edit_tests(void) {
    printk("\n--- Edit Buffer Tests ---\n");

    editbuf_t b;

    /* ------------------------------------------------------------------
     * An empty buffer is one line, and offset 0 is where the cursor sits.
     * ------------------------------------------------------------------ */
    eb_load(&b, "");
    KTEST_ASSERT(eb_line_start(&b, 0) == 0 && eb_line_end(&b, 0) == 0,
                 "[EDIT] an empty buffer is one empty line");
    KTEST_ASSERT(eb_line_count(&b) == 1,
                 "[STRICT] [EDIT] and counts as one line, not none");
    KTEST_ASSERT(eb_next_line(&b, 0) == -1 && eb_prev_line(&b, 0) == -1,
                 "[STRICT] [EDIT] with no line before or after it");

    /* ------------------------------------------------------------------
     * Three lines. Offsets written out: "ab\ncd\nef"
     *                                    01 2 34 5 67
     * ------------------------------------------------------------------ */
    eb_load(&b, "ab\ncd\nef");

    KTEST_ASSERT(eb_line_start(&b, 0) == 0 && eb_line_end(&b, 0) == 2,
                 "[EDIT] the first line brackets its own characters");
    KTEST_ASSERT(eb_line_start(&b, 4) == 3 && eb_line_end(&b, 4) == 5,
                 "[EDIT] and so does a line in the middle");
    KTEST_ASSERT(eb_line_start(&b, 7) == 6 && eb_line_end(&b, 7) == 8,
                 "[EDIT] the last line ends at the end of the buffer");

    /*
     * The newline belongs to the line it ends. A cursor on it is at that line's
     * end, not at the start of the next one - which is the distinction that
     * decides whether pressing End then Right moves down a line or not.
     */
    KTEST_ASSERT(eb_line_start(&b, 2) == 0,
                 "[STRICT] [EDIT] a newline belongs to the line it ends");
    KTEST_ASSERT(eb_col(&b, 2) == 2,
                 "[STRICT] [EDIT] so its column is the length of that line");

    KTEST_ASSERT(eb_col(&b, 0) == 0 && eb_col(&b, 1) == 1 && eb_col(&b, 4) == 1,
                 "[EDIT] columns count from the start of the line");

    KTEST_ASSERT(eb_next_line(&b, 0) == 3 && eb_next_line(&b, 3) == 6,
                 "[EDIT] the next line begins after the newline");
    KTEST_ASSERT(eb_next_line(&b, 6) == -1,
                 "[STRICT] [EDIT] and there is none after the last");
    KTEST_ASSERT(eb_prev_line(&b, 6) == 3 && eb_prev_line(&b, 3) == 0,
                 "[EDIT] and the previous line begins where it begins");
    KTEST_ASSERT(eb_prev_line(&b, 0) == -1,
                 "[STRICT] [EDIT] with none before the first");

    KTEST_ASSERT(eb_line_count(&b) == 3, "[EDIT] three lines are counted as three");
    KTEST_ASSERT(eb_goto_line(&b, 1) == 0 && eb_goto_line(&b, 2) == 3 &&
                 eb_goto_line(&b, 3) == 6,
                 "[EDIT] a line number resolves to that line's first byte");
    KTEST_ASSERT(eb_goto_line(&b, 99) == 6,
                 "[STRICT] [EDIT] and a number past the end lands on the last line");

    /* ------------------------------------------------------------------
     * A trailing newline adds an empty line, and it has to.
     *
     * The other reading - a trailing newline belongs to the line before it, the
     * way vi presents a file - is the one this was written with first, and the
     * assertion below caught it disagreeing with the rest of the header. It does
     * not survive a flat buffer: pressing Enter at the end of the last line
     * produces exactly this shape, and a count that said "still one line" would
     * leave the cursor standing somewhere the buffer says does not exist.
     * ------------------------------------------------------------------ */
    eb_load(&b, "ab\n");
    KTEST_ASSERT(eb_line_count(&b) == 2,
                 "[STRICT] [EDIT] a trailing newline leaves an empty line after it");
    KTEST_ASSERT(eb_next_line(&b, 0) == 3,
                 "[STRICT] [EDIT] and that line can be moved to");
    KTEST_ASSERT(eb_next_line(&b, 3) == -1 && eb_line_end(&b, 3) == 3,
                 "[STRICT] [EDIT] it is the last line, and it is empty");
    KTEST_ASSERT(eb_prev_line(&b, 3) == 0,
                 "[EDIT] with the text line before it");

    /* ------------------------------------------------------------------
     * A vertical move keeps the column it wants, and clamps where it cannot.
     * ------------------------------------------------------------------ */
    eb_load(&b, "abcdef\nxy\nlonger");

    KTEST_ASSERT(eb_clamp_to_line(&b, 7, 4) == 9,
                 "[STRICT] [EDIT] a column past a short line's end lands at its end");
    KTEST_ASSERT(eb_clamp_to_line(&b, 7, 1) == 8,
                 "[EDIT] and a column that fits is taken as it is");
    KTEST_ASSERT(eb_clamp_to_line(&b, 10, 4) == 14,
                 "[EDIT] moving on to a longer line reaches the column again");

    /* ------------------------------------------------------------------
     * Insert and delete.
     * ------------------------------------------------------------------ */
    eb_load(&b, "ac");
    KTEST_ASSERT(eb_insert(&b, 1, 'b') == 1 && eb_is(&b, "abc"),
                 "[EDIT] an insert moves everything after it up");
    KTEST_ASSERT(eb_insert(&b, 3, 'd') == 1 && eb_is(&b, "abcd"),
                 "[EDIT] and inserting at the end appends");
    KTEST_ASSERT(eb_insert(&b, 99, 'z') == 0 && eb_is(&b, "abcd"),
                 "[STRICT] [EDIT] an offset past the end is refused, not clamped");

    KTEST_ASSERT(eb_delete(&b, 0) == 1 && eb_is(&b, "bcd"),
                 "[EDIT] a delete moves everything after it down");
    KTEST_ASSERT(eb_delete(&b, 2) == 1 && eb_is(&b, "bc"),
                 "[EDIT] and deleting the last byte shortens the buffer");
    KTEST_ASSERT(eb_delete(&b, 2) == 0 && eb_is(&b, "bc"),
                 "[STRICT] [EDIT] deleting at the end deletes nothing");

    /*
     * A full buffer refuses rather than overruns. The editor's ceiling is the
     * most the file system will write back, and the byte that would take it over
     * has to be turned away here rather than somewhere in the VFS.
     */
    b.len = b.cap - 1;
    KTEST_ASSERT(eb_insert(&b, 0, 'x') == 0,
                 "[STRICT] [EDIT] a full buffer refuses an insert");

    /* ------------------------------------------------------------------
     * Deleting a line takes a newline with it, from whichever side has one.
     * ------------------------------------------------------------------ */
    eb_load(&b, "one\ntwo\nthree");
    KTEST_ASSERT(eb_delete_line(&b, 5) == 4 && eb_is(&b, "one\nthree"),
                 "[EDIT] deleting a middle line takes its newline with it");

    eb_load(&b, "one\ntwo");
    KTEST_ASSERT(eb_delete_line(&b, 5) == 0 && eb_is(&b, "one"),
                 "[STRICT] [EDIT] deleting the last line takes the newline before it instead");

    /*
     * And only when there really is no newline of its own. A file that ends in
     * one reaches the end of the buffer too, and taking the newline before as
     * well would swallow the line above - which is what "the span reaches the
     * end" tested, and why it is now "the line has no newline".
     */
    eb_load(&b, "one\ntwo\n");
    KTEST_ASSERT(eb_delete_line(&b, 5) == 4 && eb_is(&b, "one\n"),
                 "[STRICT] [EDIT] deleting a line from a file that ends in a newline keeps the line above");

    eb_load(&b, "only");
    KTEST_ASSERT(eb_delete_line(&b, 2) == 0 && eb_is(&b, ""),
                 "[EDIT] deleting the only line empties the buffer");
    KTEST_ASSERT(eb_line_count(&b) == 1,
                 "[STRICT] [EDIT] and what is left is still one line");

    /* ------------------------------------------------------------------
     * Searching. Offsets written out: "abc\nxabc\nq"
     *                                  012 3 4567 8 9
     * ------------------------------------------------------------------ */
    eb_load(&b, "abc\nxabc\nq");

    KTEST_ASSERT(eb_find_next(&b, "abc", -1) == 0,
                 "[EDIT] a search from before the buffer finds the first match");
    KTEST_ASSERT(eb_find_next(&b, "abc", 0) == 5,
                 "[STRICT] [EDIT] and from a match it moves past it rather than finding it again");
    KTEST_ASSERT(eb_find_prev(&b, "abc", 5) == 0,
                 "[EDIT] searching backwards finds the match before the offset");

    /*
     * Wrapping, in both directions. Without it the last match in a file answers
     * "not found" to n, and a file with one occurrence stops being findable the
     * moment the cursor is past it.
     */
    KTEST_ASSERT(eb_find_next(&b, "abc", 5) == 0,
                 "[STRICT] [EDIT] a forward search past the last match wraps to the top");
    KTEST_ASSERT(eb_find_next(&b, "abc", b.len) == 0,
                 "[STRICT] [EDIT] and so does one starting at the very end");
    KTEST_ASSERT(eb_find_prev(&b, "abc", 0) == 5,
                 "[STRICT] [EDIT] a backward search past the first match wraps to the bottom");

    KTEST_ASSERT(eb_find_next(&b, "zzz", 0) == -1 && eb_find_prev(&b, "zzz", 0) == -1,
                 "[EDIT] a pattern that is not there is not found either way");
    KTEST_ASSERT(eb_find_next(&b, "", 0) == -1,
                 "[STRICT] [EDIT] an empty pattern matches nothing rather than everything");
    KTEST_ASSERT(eb_find_next(&b, "abc\nxabc\nqq", 0) == -1,
                 "[STRICT] [EDIT] and a pattern longer than the buffer cannot match");

    /* A match at the very end is reachable, which is the off-by-one this loop
     * gets wrong when the bound is written as off < b->len. */
    KTEST_ASSERT(eb_find_next(&b, "q", 0) == 9,
                 "[STRICT] [EDIT] the last byte of the buffer can be matched");

    run_edit_undo_tests();
}
