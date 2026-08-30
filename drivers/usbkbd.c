/*
 * File: usbkbd.c
 * Purpose: HID boot keyboard reports, turned into the scancodes this kernel
 *          has understood since its first release.
 *
 * This file is part of the esdumanOS test suite.
 *
 * It knows nothing about xHCI. It is handed eight bytes and told nothing about
 * where they came from, which is what lets the whole of it be tested with made
 * up reports and no hardware at all.
 *
 * The argument for translating into set 1 rather than inventing a key event of
 * this project's own is in usbkbd.h. The short version is that
 * keyboard_handle_scancode() already knows about the Turkish layout, AltGr, the
 * Ctrl fold, Ctrl-D, the navigation sequences and the controller's fake shift,
 * and none of that had to be written twice or moved.
 */
#include "usbkbd.h"
#include "keyboard.h"
#include "libft.h"

/** The last report, so that this one can be read as a difference. */
static uint8_t previous[USBKBD_REPORT_LEN];

static int attached = 0;

/**
 * @brief HID usage to set-1 make code, with USBKBD_E0 for the prefixed ones.
 *
 * Sparse on purpose. The gap between the keypad at 0x64 and the modifiers at
 * 0xE0 is a hundred and twenty bytes of zeroes, and it buys one lookup for every
 * key including the modifiers - because a modifier bit is a usage too, and
 * saying so once is better than a second table that has to be kept in step.
 *
 * The absences are deliberate rather than pending. PrintScreen and Pause are
 * not here: set 1 encodes them as multi-byte sequences that mean nothing to a
 * driver reading one code at a time, and this kernel has never done anything
 * with either. Everything not listed returns zero and the key is dropped, which
 * is the honest outcome for a usage the representation below cannot express.
 */
static const uint8_t usage_to_set1[0xE8] = {
    /* Letters. */
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20,
    [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22, [0x0B] = 0x23,
    [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19,
    [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14,
    [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,

    /* Digits, in the order the top row has them: 1 through 9, then 0. */
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05,
    [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09,
    [0x26] = 0x0A, [0x27] = 0x0B,

    /* The keys around them. */
    [0x28] = 0x1C,  /* Enter        */
    [0x29] = 0x01,  /* Escape       */
    [0x2A] = 0x0E,  /* Backspace    */
    [0x2B] = 0x0F,  /* Tab          */
    [0x2C] = 0x39,  /* Space        */
    [0x2D] = 0x0C,  /* -            */
    [0x2E] = 0x0D,  /* =            */
    [0x2F] = 0x1A,  /* [            */
    [0x30] = 0x1B,  /* ]            */
    [0x31] = 0x2B,  /* backslash    */
    [0x32] = 0x2B,  /* non-US #, the same key on a keyboard that has it there */
    [0x33] = 0x27,  /* ;            */
    [0x34] = 0x28,  /* '            */
    [0x35] = 0x29,  /* `            */
    [0x36] = 0x33,  /* ,            */
    [0x37] = 0x34,  /* .            */
    [0x38] = 0x35,  /* /            */
    [0x39] = 0x3A,  /* Caps Lock    */

    /* Function keys. F11 and F12 are not adjacent to F10 in set 1. */
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E,
    [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42,
    [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,

    [0x47] = 0x46,  /* Scroll Lock  */

    /*
     * The navigation block, all of it behind an E0 prefix - and this is the
     * half of the table that matters most to this kernel, because
     * kbd_sequence_for() turns exactly these into the escape sequences a
     * terminal sends. Without the prefix they would arrive as their keypad
     * twins instead.
     */
    [0x49] = 0x52 | USBKBD_E0,  /* Insert    */
    [0x4A] = 0x47 | USBKBD_E0,  /* Home      */
    [0x4B] = 0x49 | USBKBD_E0,  /* Page Up   */
    [0x4C] = 0x53 | USBKBD_E0,  /* Delete    */
    [0x4D] = 0x4F | USBKBD_E0,  /* End       */
    [0x4E] = 0x51 | USBKBD_E0,  /* Page Down */
    [0x4F] = 0x4D | USBKBD_E0,  /* Right     */
    [0x50] = 0x4B | USBKBD_E0,  /* Left      */
    [0x51] = 0x50 | USBKBD_E0,  /* Down      */
    [0x52] = 0x48 | USBKBD_E0,  /* Up        */

    /* The keypad, which shares its codes with the block above and is told apart
     * from it by the absence of the prefix. */
    [0x53] = 0x45,              /* Num Lock  */
    [0x54] = 0x35 | USBKBD_E0,  /* KP /      */
    [0x55] = 0x37,              /* KP *      */
    [0x56] = 0x4A,              /* KP -      */
    [0x57] = 0x4E,              /* KP +      */
    [0x58] = 0x1C | USBKBD_E0,  /* KP Enter  */
    [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B,
    [0x5D] = 0x4C, [0x5E] = 0x4D, [0x5F] = 0x47, [0x60] = 0x48,
    [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53,

    [0x64] = 0x56,  /* The extra key on a keyboard that is not US-shaped. */

    /*
     * The modifiers, which are usages like anything else. Right alt is the one
     * that earns its place here: it is AltGr, drivers/keyboard.c recognises it
     * only as E0 0x38, and the Turkish layout is unusable without it.
     */
    [0xE0] = 0x1D,              /* Left Control  */
    [0xE1] = 0x2A,              /* Left Shift    */
    [0xE2] = 0x38,              /* Left Alt      */
    [0xE3] = 0x5B | USBKBD_E0,  /* Left GUI      */
    [0xE4] = 0x1D | USBKBD_E0,  /* Right Control */
    [0xE5] = 0x36,              /* Right Shift   */
    [0xE6] = 0x38 | USBKBD_E0,  /* Right Alt, which is AltGr */
    [0xE7] = 0x5C | USBKBD_E0,  /* Right GUI     */
};

uint8_t usbkbd_scancode_for(uint8_t usage) {
    return usage_to_set1[usage];
}

void usbkbd_reset(void) {
    ft_memset(previous, 0, sizeof(previous));
}

int usbkbd_attached(void) {
    return attached;
}

void usbkbd_attach(void) {
    usbkbd_reset();
    attached = 1;
}

/**
 * @brief Sends one key's make or break code down the PS/2 driver's own path.
 *
 * A break code is the make code with bit 7 set, which is set 1's own rule and
 * the one keyboard_handle_scancode() already tests for. The prefix goes first
 * and on its own, because that is how the byte stream from a real controller
 * arrives and the driver's e0_mode is a state machine over exactly that.
 */
static void emit(uint8_t usage, int pressed) {
    uint8_t code = usbkbd_scancode_for(usage);

    if (code == 0) return;

    if (code & USBKBD_E0) keyboard_handle_scancode(0xE0);

    code &= (uint8_t)~USBKBD_E0;
    keyboard_handle_scancode(pressed ? code : (uint8_t)(code | 0x80));
}

/** @brief Whether a report's six key slots hold a given usage. */
static int holds(const uint8_t *report, uint8_t usage) {
    for (int i = 2; i < USBKBD_REPORT_LEN; i++) {
        if (report[i] == usage) return 1;
    }
    return 0;
}

void usbkbd_report(const uint8_t *report) {
    if (report == 0) return;

    /*
     * Rollover, and the whole report goes in the bin.
     *
     * A boot keyboard with more keys held than it can name fills all six slots
     * with ErrorRollOver. Diffing that against the previous report would emit a
     * release for every key that was down - the user would see everything they
     * were holding let go, at the exact moment they were holding the most. The
     * previous report is deliberately left alone too, so that the state this
     * driver believes in is the last one the keyboard could actually describe.
     */
    int rollover = 1;

    for (int i = 2; i < USBKBD_REPORT_LEN; i++) {
        if (report[i] != USBKBD_ROLLOVER) { rollover = 0; break; }
    }
    if (rollover) return;

    /*
     * Modifiers first, and by bit. Bit b of the modifier byte is usage
     * 0xE0 + b, which is the specification's own arrangement rather than this
     * driver's - so they go through the same table every other key does.
     */
    uint8_t changed = (uint8_t)(report[0] ^ previous[0]);

    for (int b = 0; b < 8; b++) {
        if (!(changed & (1 << b))) continue;
        emit((uint8_t)(USBKBD_MODIFIER_BASE + b), (report[0] >> b) & 1);
    }

    /*
     * Releases before presses. A report that shows one key going down and
     * another coming up carries no ordering of its own, and letting go first is
     * the order that cannot invent a chord the user did not type.
     */
    for (int i = 2; i < USBKBD_REPORT_LEN; i++) {
        uint8_t usage = previous[i];

        if (usage < USBKBD_FIRST_KEY) continue;
        if (!holds(report, usage)) emit(usage, 0);
    }

    for (int i = 2; i < USBKBD_REPORT_LEN; i++) {
        uint8_t usage = report[i];

        if (usage < USBKBD_FIRST_KEY) continue;
        if (!holds(previous, usage)) emit(usage, 1);
    }

    for (int i = 0; i < USBKBD_REPORT_LEN; i++) previous[i] = report[i];
}
