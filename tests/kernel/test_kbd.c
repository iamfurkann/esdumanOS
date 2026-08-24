/*
 * File: test_kbd.c
 * Purpose: Scancode translation - what each key puts in the input ring.
 *
 * Every keystroke in the system goes through one function, and until v0.8.2 that
 * function had no coverage at all: it begins by reading a hardware port, and
 * there is no way to write one from a test. The translation is now separate from
 * the port, so the rules can be driven with a scancode of one's choosing - which
 * matters more than usual in this release, because it changes all of them.
 *
 * What is under test is the ring, not the screen. A key either puts bytes in the
 * input ring or it does not, and the ones that do not - the terminal switches,
 * the scrollback, Ctrl-C, Ctrl-Z - are as much the point as the ones that do:
 * a key the driver consumes is a key no program will ever see, and getting that
 * list wrong is how the arrows came to be unreachable in the first place.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "keyboard.h"
#include "libft.h"
#include "process.h"
#include "tty.h"

/*
 * The ring itself. Not in keyboard.h because nothing outside the driver has any
 * business moving these - this module does, to put the ring in a state a test
 * could not otherwise reach: nearly full.
 */
extern volatile char kbd_buffer[];
extern volatile int kbd_head;
extern volatile int kbd_tail;

#define KBD_RING_SIZE 256

/** Scancodes used below, named so the tests read as key presses. */
#define SC_CTRL_DOWN   0x1D
#define SC_CTRL_UP     0x9D
#define SC_SHIFT_DOWN  0x2A
#define SC_SHIFT_UP    0xAA
#define SC_A           0x1E
#define SC_D           0x20
#define SC_C           0x2E
#define SC_Z           0x2C
#define SC_UP          0x48
#define SC_DOWN        0x50
#define SC_LEFT        0x4B
#define SC_RIGHT       0x4D
#define SC_HOME        0x47
#define SC_END         0x4F
#define SC_INSERT      0x52
#define SC_DELETE      0x53
#define SC_PGUP        0x49
#define SC_PGDN        0x51

/**
 * @brief Empties the input ring.
 *
 * Between assertions, so that one key's bytes cannot be read as the next one's.
 * Also at the start: this is a real ring shared with a real keyboard, and a key
 * pressed while the suite runs would otherwise be indistinguishable from a
 * translation bug.
 */
static void kbd_drain(void) {
    while (get_keyboard_char() != 0) { }
}

/**
 * @brief Takes everything waiting in the ring as a string.
 *
 * @param out Receives the bytes, null-terminated.
 * @param max Capacity of out, including the terminator.
 * @return How many bytes were taken.
 */
static int kbd_take(char *out, int max) {
    int n = 0;
    char c;

    while (n < max - 1 && (c = get_keyboard_char()) != 0) out[n++] = c;
    out[n] = '\0';
    return n;
}

/**
 * @brief Presses and releases one key, and reports what reached the ring.
 *
 * @param scancode Make code.
 * @param out Receives the bytes.
 * @param max Capacity of out.
 */
static void kbd_press(uint8_t scancode, char *out, int max) {
    kbd_drain();
    keyboard_handle_scancode(scancode);
    kbd_take(out, max);
}

/**
 * @brief Asserts that a key produces exactly one expected sequence.
 *
 * @param scancode Make code.
 * @param expect The bytes it should produce.
 * @param msg Assertion text.
 */
static void assert_key(uint8_t scancode, const char *expect, const char *msg) {
    char got[16];

    kbd_press(scancode, got, sizeof(got));
    KTEST_ASSERT(ft_strcmp(got, expect) == 0, msg);
}

/**
 * @brief Verifies what each key puts in the input ring.
 *
 * Expected behavior:
 * - Every navigation key produces the sequence a terminal sends for it.
 * - Shift with a Page key scrolls and reaches no program.
 * - Ctrl folds a letter to its control code, and Ctrl-C and Ctrl-Z are consumed.
 * - A sequence that does not fit is not written at all.
 *
 * Edge cases covered:
 * - A ring with room for part of a sequence but not all of it.
 * - Key releases, which must produce nothing.
 */
void run_kbd_tests(void) {
    printk("\n--- Keyboard Translation Tests ---\n");

    /*
     * Ctrl-C and Ctrl-Z are tested below and both send a signal to the
     * foreground group. Pointing that at nobody first is not tidiness: this
     * suite runs with real tasks in the list, and a test that interrupts them is
     * a test that ends the run it is part of. send_signal_to_group() refuses
     * group 0, which is exactly the "nobody" this needs.
     */
    uint32_t saved_fg = foreground_pgid;
    foreground_pgid = 0;

    kbd_drain();

    /* ------------------------------------------------------------------
     * The navigation keys, which reached no program at all before this.
     * ------------------------------------------------------------------ */
    assert_key(SC_UP,     "\033[A",  "[KBD] the up arrow sends CUU");
    assert_key(SC_DOWN,   "\033[B",  "[KBD] the down arrow sends CUD");
    assert_key(SC_RIGHT,  "\033[C",  "[KBD] the right arrow sends CUF");
    assert_key(SC_LEFT,   "\033[D",  "[KBD] the left arrow sends CUB");
    assert_key(SC_HOME,   "\033[H",  "[KBD] Home sends the sequence a terminal sends");
    assert_key(SC_END,    "\033[F",  "[KBD] End sends the sequence a terminal sends");
    assert_key(SC_INSERT, "\033[2~", "[STRICT] [KBD] Insert sends its numbered sequence");
    assert_key(SC_DELETE, "\033[3~", "[STRICT] [KBD] Delete sends its numbered sequence");
    assert_key(SC_PGUP,   "\033[5~", "[STRICT] [KBD] Page Up sends its numbered sequence");
    assert_key(SC_PGDN,   "\033[6~", "[STRICT] [KBD] Page Down sends its numbered sequence");

    /* ------------------------------------------------------------------
     * Shift with a Page key is the scrollback, and reaches nobody.
     *
     * The view is put back afterwards. Scrolling really does move what the
     * screen shows, and leaving it moved would hide the rest of this suite's own
     * output behind the assertion that moved it.
     * ------------------------------------------------------------------ */
    char got[16];

    keyboard_handle_scancode(SC_SHIFT_DOWN);

    kbd_press(SC_PGUP, got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0', "[STRICT] [KBD] Shift with Page Up scrolls and sends nothing");

    kbd_press(SC_PGDN, got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0', "[STRICT] [KBD] Shift with Page Down scrolls and sends nothing");

    keyboard_handle_scancode(SC_SHIFT_UP);
    terminal_scroll_down();

    /* Without Shift the same key is the program's again. */
    assert_key(SC_PGUP, "\033[5~", "[KBD] and without Shift it reaches the program");

    /* ------------------------------------------------------------------
     * A key release produces nothing at all.
     * ------------------------------------------------------------------ */
    kbd_press(SC_A | 0x80, got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0', "[STRICT] [KBD] a key release puts nothing in the ring");

    /* ------------------------------------------------------------------
     * Ordinary keys, and what Ctrl does to them.
     * ------------------------------------------------------------------ */
    assert_key(SC_A, "a", "[KBD] a letter arrives as itself");

    keyboard_handle_scancode(SC_CTRL_DOWN);

    kbd_press(SC_D, got, sizeof(got));
    KTEST_ASSERT(got[0] == 0x04 && got[1] == '\0',
                 "[STRICT] [KBD] Ctrl folds a letter to its control code");

    /*
     * Ctrl-C and Ctrl-Z are consumed rather than delivered. A program that wants
     * the interrupt asks for it by catching the signal; leaving the byte in the
     * ring as well would hand every reader a stray control character after every
     * keypress.
     */
    kbd_press(SC_C, got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0', "[STRICT] [KBD] Ctrl-C is consumed, not delivered as a byte");

    kbd_press(SC_Z, got, sizeof(got));
    KTEST_ASSERT(got[0] == '\0', "[STRICT] [KBD] Ctrl-Z is consumed, not delivered as a byte");

    keyboard_handle_scancode(SC_CTRL_UP);

    /* And the fold is gone once Ctrl is released. */
    assert_key(SC_D, "d", "[STRICT] [KBD] releasing Ctrl restores the plain letter");

    /* ------------------------------------------------------------------
     * A sequence is written whole or not at all.
     *
     * A reader handed the first two bytes of an arrow key has no way to know the
     * rest was dropped: it waits for a byte that is not coming, or reads the next
     * unrelated key as the tail of the sequence. The ring is put one byte short
     * of what the sequence needs, which is a state no amount of typing could
     * arrange on purpose.
     * ------------------------------------------------------------------ */
    kbd_drain();

    kbd_tail = 0;
    kbd_head = KBD_RING_SIZE - 3;   /* two free slots; CUU needs three */

    keyboard_handle_scancode(SC_UP);
    KTEST_ASSERT(kbd_head == KBD_RING_SIZE - 3,
                 "[STRICT] [KBD] a sequence that does not fit is not written at all");

    kbd_head = 0;
    kbd_tail = 0;

    /* And it fits again once there is room. */
    assert_key(SC_UP, "\033[A", "[KBD] the same key is written once the ring has room");

    kbd_drain();
    foreground_pgid = saved_fg;
}
