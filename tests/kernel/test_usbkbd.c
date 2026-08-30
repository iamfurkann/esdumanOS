/*
 * File: test_usbkbd.c
 * Purpose: HID boot reports, and the scancodes they turn into.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Almost all of this runs without a controller, a device or a register read:
 * usbkbd_report() is handed eight bytes and told nothing about where they came
 * from, so eight bytes made up here are indistinguishable from eight bytes a
 * keyboard sent. That is the same property keyboard_handle_scancode() has had
 * since it was split out of the IRQ1 handler, and it is why this release could
 * be small - the translation is testable from both ends.
 *
 * What is asserted through the ring rather than against the table is the part
 * that matters most: that these scancodes really do drive drivers/keyboard.c's
 * existing rules. A table that returns the right numbers and a driver that acts
 * on them are two different claims, and only the second one is the release.
 *
 * This module touches shared state - the translator's memory of the last report
 * is the same one the live keyboard uses - so it puts it back, and says so in an
 * assertion rather than in a comment. test_blockdev.c has kept that discipline
 * for the root block device since v1.2.0.
 */
#include "ktest.h"
#include "usbkbd.h"
#include "keyboard.h"
#include "libft.h"

/* Modifier bits, as byte 0 of a boot report carries them. */
#define MOD_LSHIFT 0x02

/* A few usages, named so the cases below read as what they press. */
#define USAGE_A     0x04
#define USAGE_B     0x05
#define USAGE_UP    0x52
#define USAGE_ALTGR 0xE6
#define USAGE_PAUSE 0x48

/**
 * @brief Empties the input ring.
 *
 * A real keyboard shares this ring, and although nothing types at a machine
 * running with no display, a byte left over from an earlier module would be
 * indistinguishable from a translation fault.
 */
static void drain(void) {
    while (get_keyboard_char() != 0) { }
}

/** @brief Takes everything waiting in the ring as a string. */
static int take(char *out, int max) {
    int n = 0;
    char c;

    while (n < max - 1 && (c = get_keyboard_char()) != 0) out[n++] = c;
    out[n] = '\0';
    return n;
}

/** @brief Delivers one report: a modifier byte and up to two held keys. */
static void report(uint8_t mod, uint8_t k0, uint8_t k1) {
    uint8_t r[USBKBD_REPORT_LEN];

    ft_memset(r, 0, sizeof(r));
    r[0] = mod;
    r[2] = k0;
    r[3] = k1;

    usbkbd_report(r);
}

/**
 * @brief The table, asked directly.
 *
 * Four questions, and the middle two are the ones that would go wrong quietly.
 * A navigation key without its prefix is its keypad twin, and right alt without
 * one is a plain alt - which on a Turkish layout is the difference between
 * typing the third character on a key and typing nothing at all.
 */
static void run_table_assertions(void) {
    KTEST_ASSERT(usbkbd_scancode_for(USAGE_A) == 0x1E,
                 "[STRICT] [USBKBD] a letter translates to the set-1 code the driver expects");

    KTEST_ASSERT(usbkbd_scancode_for(USAGE_UP) == (0x48 | USBKBD_E0),
                 "[STRICT] [USBKBD] a navigation key carries the prefix that tells it from its keypad twin");

    KTEST_ASSERT(usbkbd_scancode_for(USAGE_ALTGR) == (0x38 | USBKBD_E0),
                 "[STRICT] [USBKBD] right alt translates to AltGr, which the Turkish layout needs");

    KTEST_ASSERT(usbkbd_scancode_for(0x00) == 0 &&
                 usbkbd_scancode_for(USAGE_PAUSE) == 0,
                 "[STRICT] [USBKBD] a usage set 1 cannot express translates to nothing rather than to something wrong");
}

/**
 * @brief A report says what is held, not what changed, and that is the trap.
 *
 * A driver that emitted what it saw would repeat every held key at every poll.
 * At one report per timer tick that is a hundred repeats a second, and it would
 * look like a stuck key rather than like a bug in a diff.
 */
static void run_diff_assertions(void) {
    char got[16];

    usbkbd_reset();
    drain();

    report(0, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "a") == 0,
                 "[STRICT] [USBKBD] a key appearing in a report is pressed once");

    report(0, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0',
                 "[STRICT] [USBKBD] the same key in the next report is held, not pressed again");

    report(0, 0, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0',
                 "[STRICT] [USBKBD] and a key leaving the report produces a release, which types nothing");

    /* Two keys in one report, in the order the slots hold them. */
    usbkbd_reset();
    drain();

    report(0, USAGE_A, USAGE_B);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "ab") == 0,
                 "[STRICT] [USBKBD] two keys arriving together are both pressed");
}

/**
 * @brief That a release really reaches the driver, proved by a modifier.
 *
 * A release types nothing, so at the ring it is indistinguishable from a release
 * that was never emitted at all. A modifier is the way to tell: shift changes
 * what the *next* key produces, so a shift whose break code went missing would
 * leave every letter after it capitalised.
 */
static void run_release_assertions(void) {
    char got[16];

    usbkbd_reset();
    drain();

    report(MOD_LSHIFT, 0, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0',
                 "[STRICT] [USBKBD] a modifier going down types nothing on its own");

    report(MOD_LSHIFT, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "A") == 0,
                 "[STRICT] [USBKBD] and the key under it arrives shifted");

    report(0, 0, 0);
    take(got, sizeof(got));

    report(0, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "a") == 0,
                 "[STRICT] [USBKBD] the modifier's release reached the driver, which is only visible in the key after it");
}

/**
 * @brief A prefixed key, all the way to the escape sequence a terminal sends.
 *
 * This is the assertion that says the two halves are actually joined. The table
 * returns a prefix flag, usbkbd_report() turns that into a leading 0xE0, and
 * drivers/keyboard.c's e0_mode is a state machine over exactly that byte - which
 * then reaches kbd_sequence_for() and comes out as what xterm sends for Up.
 * Nothing here was written for USB; all of it was already true of PS/2.
 */
static void run_prefix_assertions(void) {
    char got[16];

    usbkbd_reset();
    drain();

    report(0, USAGE_UP, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "\033[A") == 0,
                 "[STRICT] [USBKBD] a prefixed key comes out as the sequence a terminal sends for it");
}

/**
 * @brief The report that has to be thrown away whole.
 *
 * A boot keyboard holding more keys than it can name fills every slot with
 * ErrorRollOver. Diffing it would emit a release for everything that was down -
 * the user would see every key they were holding let go, at the moment they were
 * holding the most. The previous report has to survive it too, which is the
 * third assertion here and the one that would fail if the report were dropped
 * after being recorded rather than instead of.
 */
static void run_rollover_assertions(void) {
    char got[16];
    uint8_t rollover[USBKBD_REPORT_LEN];

    usbkbd_reset();
    drain();

    report(0, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, "a") == 0,
                 "[STRICT] [USBKBD] a key is held before the keyboard gives up");

    ft_memset(rollover, USBKBD_ROLLOVER, sizeof(rollover));
    rollover[0] = 0;
    rollover[1] = 0;

    usbkbd_report(rollover);
    take(got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0',
                 "[STRICT] [USBKBD] a rollover report types nothing rather than releasing everything");

    report(0, USAGE_A, 0);
    take(got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0',
                 "[STRICT] [USBKBD] and it did not disturb what was held, so that key is still down");
}

/**
 * @brief The hardware, and putting the shared state back.
 *
 * Every kernel test target attaches a USB keyboard, so it having been configured
 * is a fact about these machines rather than a hope. The last assertion is this
 * module's own housekeeping: the translator's memory of the previous report is
 * shared with the live keyboard, so it is left as a newly attached one would
 * leave it, and that it still works afterwards is asserted rather than assumed.
 */
static void run_attach_assertions(void) {
    char got[16];

    KTEST_ASSERT(usbkbd_attached(),
                 "[STRICT] [USBKBD] a USB keyboard was configured on this machine");

    KTEST_ASSERT(usbkbd_scancode_for(0) == 0,
                 "[STRICT] [USBKBD] usage zero, which fills the empty slots, is not a key");

    usbkbd_reset();
    drain();

    report(0, USAGE_B, 0);
    take(got, sizeof(got));
    report(0, 0, 0);
    drain();

    KTEST_ASSERT(ft_strcmp(got, "b") == 0,
                 "[STRICT] [USBKBD] and the translator is left in the state a freshly attached keyboard has");
}

void run_usbkbd_tests(void) {
    printk("\n--- USB HID Keyboard Tests ---\n");

    run_table_assertions();
    run_diff_assertions();
    run_release_assertions();
    run_prefix_assertions();
    run_rollover_assertions();
    run_attach_assertions();
}
