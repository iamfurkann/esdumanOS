/*
 * File: test_tty.c
 * Purpose: ANSI escape sequences - what they do, where they land, and what they
 *          refuse to do.
 *
 * Driven against terminal 1, which nobody is looking at. Testing the parser on
 * the visible terminal is not possible in any useful sense: the suite reports
 * every one of these assertions through the same function, so the results would
 * scroll the screen and move the very cursor the next check was about to read.
 *
 * The assertions that carry this file are the coordinate ones. There are three
 * spaces in play - the 25 rows the hardware has, the 24 the screen shows because
 * row 0 is the status bar, and the 100 the scrollback buffer holds - and an
 * escape sequence names rows in the second while the cursor is stored in the
 * third. A full-screen program drawing one row off, or over the status bar, is
 * what getting that wrong looks like.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "tty.h"

/** The terminal these tests drive; 0 is the one the suite is printing on. */
#define T 1

/**
 * @brief Reads the cursor row after feeding a sequence.
 */
static size_t cursor_row(void) {
    size_t row = 0;
    terminal_cursor_at(T, &row, 0);
    return row;
}

/**
 * @brief Reads the cursor column after feeding a sequence.
 */
static size_t cursor_col(void) {
    size_t col = 0;
    terminal_cursor_at(T, 0, &col);
    return col;
}

/**
 * @brief Verifies the escape parser and the sequences it implements.
 *
 * Expected behavior:
 * - Ordinary text still prints, and a sequence prints nothing at all.
 * - Cursor positioning lands on the row it names, counted in visible rows.
 * - Relative motion is bounded by the screen rather than wrapping or running
 *   into the scrollback.
 * - Erasing clears what it says and leaves the rest.
 * - Colour changes apply to what is written after them.
 * - The cursor can be saved and restored.
 * - A scroll region bounds line insertion and deletion.
 *
 * Edge cases covered:
 * - A sequence this does not implement, which must leave no trace.
 * - A sequence cut short by a control character, which must not swallow
 *   everything printed afterwards.
 * - Positions outside the screen, which must be clamped rather than followed.
 */
void run_tty_tests(void) {
    printk("\n--- Terminal / ANSI Escape Tests ---\n");

    terminal_reset(T);

    /* ------------------------------------------------------------------
     * Ordinary text, and a sequence that prints nothing.
     * ------------------------------------------------------------------ */
    terminal_write_to(T, "AB");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == 'A' && terminal_char_at(T, 1, 2) == 'B',
                 "[TTY] ordinary characters land where they are written");
    KTEST_ASSERT(cursor_row() == 1 && cursor_col() == 3,
                 "[TTY] and the cursor follows them");

    terminal_reset(T);
    terminal_write_to(T, "\033[7mX");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == 'X',
                 "[STRICT] [TTY] a sequence prints none of its own bytes");

    /* ------------------------------------------------------------------
     * Absolute positioning, in the space the screen shows.
     *
     * The load-bearing check of this release. Row 1 is the first row of text,
     * which is hardware row 1 because row 0 is the status bar - and it is
     * counted from the top of the *view*, not from the top of the hundred-row
     * scrollback buffer the cursor actually lives in.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "\033[5;10HZ");

    KTEST_ASSERT(terminal_char_at(T, 5, 10) == 'Z',
                 "[STRICT] [TTY] the cursor lands on the row and column it was given");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == ' ',
                 "[STRICT] [TTY] and nothing was written on the way there");

    terminal_reset(T);
    terminal_write_to(T, "\033[HA");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == 'A',
                 "[TTY] a bare position sequence means the top left corner");

    /* A row past the bottom is clamped, not followed into the scrollback. */
    terminal_reset(T);
    terminal_write_to(T, "\033[99;1HB");
    KTEST_ASSERT(terminal_char_at(T, 24, 1) == 'B',
                 "[STRICT] [TTY] a row past the last one is clamped to the last one");
    KTEST_ASSERT(cursor_row() == 24,
                 "[TTY] and the cursor stays on the screen");

    /* ------------------------------------------------------------------
     * Relative motion, bounded by the screen.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "\033[10;10H\033[3A\033[2B\033[4C\033[1D");
    KTEST_ASSERT(cursor_row() == 9, "[TTY] up three and down two is one row up");
    KTEST_ASSERT(cursor_col() == 13, "[TTY] forward four and back one is three columns on");

    terminal_reset(T);
    terminal_write_to(T, "\033[1;1H\033[9A\033[9D");
    KTEST_ASSERT(cursor_row() == 1 && cursor_col() == 1,
                 "[STRICT] [TTY] motion off the top left corner stops there");

    /* ------------------------------------------------------------------
     * Erasing.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "HELLO\033[1;3H\033[K");
    KTEST_ASSERT(terminal_char_at(T, 1, 2) == 'E', "[TTY] erasing to the end keeps what is before it");
    KTEST_ASSERT(terminal_char_at(T, 1, 3) == ' ' && terminal_char_at(T, 1, 5) == ' ',
                 "[STRICT] [TTY] and clears from the cursor to the end of the line");

    terminal_reset(T);
    terminal_write_to(T, "\033[3;1HKEEP\033[10;1HGONE\033[2J");
    KTEST_ASSERT(terminal_char_at(T, 3, 1) == ' ' && terminal_char_at(T, 10, 1) == ' ',
                 "[STRICT] [TTY] erasing the screen clears all of it");
    KTEST_ASSERT(cursor_row() == 10,
                 "[TTY] and deliberately does not move the cursor");

    /* ------------------------------------------------------------------
     * Colour applies to what comes after it.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "d\033[31mr\033[0mp");

    uint8_t plain = terminal_color_at(T, 1, 1);
    uint8_t red = terminal_color_at(T, 1, 2);
    uint8_t back = terminal_color_at(T, 1, 3);

    KTEST_ASSERT(red != plain, "[TTY] a colour sequence changes what is written next");
    KTEST_ASSERT((red & 0x0F) == VGA_COLOR_RED,
                 "[STRICT] [TTY] and ANSI's colour numbers map to the right VGA ones");
    KTEST_ASSERT(back == plain, "[TTY] and a reset puts it back");

    /* ------------------------------------------------------------------
     * Save and restore.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "\033[7;7H\033[s\033[20;40H\033[uM");
    KTEST_ASSERT(terminal_char_at(T, 7, 7) == 'M',
                 "[STRICT] [TTY] a restored cursor is where it was saved");

    /* ------------------------------------------------------------------
     * Scroll region, and the line operations it bounds.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "\033[5;10r");
    KTEST_ASSERT(cursor_row() == 5 && cursor_col() == 1,
                 "[TTY] setting a scroll region homes the cursor inside it");

    terminal_reset(T);
    terminal_write_to(T, "\033[1;1HTOP\033[5;1HA\033[6;1HB\033[5;10r\033[5;1H\033[1L");
    KTEST_ASSERT(terminal_char_at(T, 5, 1) == ' ',
                 "[TTY] inserting a line opens a blank one at the cursor");
    KTEST_ASSERT(terminal_char_at(T, 6, 1) == 'A' && terminal_char_at(T, 7, 1) == 'B',
                 "[STRICT] [TTY] and pushes the rest of the region down");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == 'T',
                 "[STRICT] [TTY] while everything above the region is left alone");

    terminal_reset(T);
    terminal_write_to(T, "\033[5;1HA\033[6;1HB\033[7;1HC\033[5;10r\033[5;1H\033[1M");
    KTEST_ASSERT(terminal_char_at(T, 5, 1) == 'B' && terminal_char_at(T, 6, 1) == 'C',
                 "[STRICT] [TTY] deleting a line pulls the region up");

    /*
     * The cursor outside the region: nothing happens. A stale region left by a
     * program that exited badly must not let the next one shift rows it does not
     * own.
     */
    terminal_reset(T);
    terminal_write_to(T, "\033[2;1HX\033[5;10r\033[2;1H\033[1M");
    KTEST_ASSERT(terminal_char_at(T, 2, 1) == 'X',
                 "[STRICT] [TTY] line operations outside the scroll region do nothing");

    /* ------------------------------------------------------------------
     * The refusals.
     * ------------------------------------------------------------------ */
    terminal_reset(T);
    terminal_write_to(T, "\033[42;99ZQ");
    KTEST_ASSERT(terminal_char_at(T, 1, 1) == 'Q',
                 "[STRICT] [TTY] an unimplemented sequence leaves no trace and swallows nothing after it");

    /*
     * A sequence cut short. Without a control character breaking the parser out
     * of it, a program that emitted half a sequence and stopped would swallow
     * everything printed afterwards and the console would go silent with nothing
     * to show why.
     */
    terminal_reset(T);
    terminal_write_to(T, "\033[12\nAFTER");
    KTEST_ASSERT(terminal_char_at(T, 2, 1) == 'A',
                 "[STRICT] [TTY] a newline aborts an unfinished sequence rather than being eaten by it");

    terminal_reset(T);
}
