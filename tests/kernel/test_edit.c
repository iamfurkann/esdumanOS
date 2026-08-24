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
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "editbuf.h"
#include "libft.h"

/** Room for every fixture below, with space to insert into. */
#define EB_TEST_CAP 256

static char eb_storage[EB_TEST_CAP];

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
 * @brief Verifies the edit buffer's line arithmetic and its two edits.
 *
 * Expected behavior:
 * - A line's start and end bracket exactly the characters on it.
 * - The offset one past the last byte is a valid cursor position.
 * - A vertical move keeps the column it aims for, clamped to short lines.
 * - Insert and delete move the bytes after them and nothing else.
 * - Deleting a line takes its newline with it, from whichever side has one.
 *
 * Edge cases covered:
 * - An empty buffer, which is one line.
 * - A buffer ending in a newline, which is not one line more.
 * - An offset on the newline itself, which belongs to the line before it.
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
}
