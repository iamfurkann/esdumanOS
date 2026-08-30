#ifndef USBKBD_H
#define USBKBD_H

#include "types.h"

/**
 * @file usbkbd.h
 * @brief The keyboard on a machine that has no PS/2 controller.
 *
 * This is the smaller half of the release it arrives in, and the reason it is
 * small is that the seam it needs was already there. drivers/keyboard.c is 376
 * lines and exactly one of them touches hardware - the inb(0x60) inside the IRQ1
 * handler. Everything else, the layouts and the Ctrl fold and the escape
 * sequences and the E0 rules, is reached through keyboard_handle_scancode(),
 * which was split out for testability long before anything needed two backends.
 *
 * So this file does not add a backend to the keyboard driver. It translates HID
 * usage codes into the set-1 scancodes that driver already speaks, and calls it.
 * The Turkish layout, AltGr, Ctrl-D, the arrow keys and the fake-shift rule all
 * work on a USB keyboard without one line of drivers/keyboard.c changing.
 *
 * Translating through a representation from 1981 is worth being honest about.
 * It costs the few HID usages set 1 has no code for - they are dropped - and it
 * buys every rule the PS/2 path spent nine releases getting right, plus the ten
 * assertions in test_kbd.c which now cover both backends without being touched.
 *
 * What this file does not know: anything about xHCI. It is handed eight bytes
 * and told nothing about where they came from. The controller driver does not
 * know what a keyboard is either; the only join between them is
 * usbkbd_report(), named on both sides rather than registered in a table,
 * because there is exactly one consumer and a table of one is a table nobody
 * needs.
 */

/**
 * @brief Bytes in a HID boot keyboard report.
 *
 * Fixed by the boot protocol, which is the whole reason this release does not
 * parse a report descriptor: a device in boot protocol promises this shape. Byte
 * 0 is a modifier bitmap, byte 1 is reserved, and bytes 2 to 7 are the usages of
 * up to six keys currently held.
 */
#define USBKBD_REPORT_LEN 8

/**
 * @brief The usage a report slot carries when the keyboard has given up.
 *
 * ErrorRollOver. A boot keyboard that has more keys held than it can report
 * fills every slot with this rather than telling a partial truth, and a report
 * like that must be dropped rather than diffed - diffing it would look exactly
 * like the user releasing every key at once.
 */
#define USBKBD_ROLLOVER 0x01

/**
 * @brief The first usage that names a key rather than an error.
 *
 * Usages 1, 2 and 3 are ErrorRollOver, POSTFail and ErrorUndefined. They are
 * states of the keyboard, not keys on it, and a slot holding one of them is not
 * a key that was pressed.
 */
#define USBKBD_FIRST_KEY 0x04

/**
 * @brief The usage the lowest modifier bit stands for.
 *
 * The modifier byte's bits are not an invention of this driver: bit b of byte 0
 * is the usage 0xE0 + b, in order - left control, left shift, left alt, left
 * GUI, then the four right-hand ones. Encoding that relationship instead of a
 * second lookup table is what lets modifiers go through the same translation
 * every other key does.
 */
#define USBKBD_MODIFIER_BASE 0xE0

/**
 * @brief Set in a translated scancode that has to be sent behind an E0 prefix.
 *
 * Set 1 puts the keys the original PC did not have - the arrows, the navigation
 * block, the right-hand modifiers, keypad Enter - behind an escape byte. The
 * table returns one value for both halves of that and this bit says which kind
 * it is, because a scancode of 0x48 means Up with the prefix and keypad 8
 * without it.
 *
 * Safe as a flag because no set-1 make code reaches 0x80: the high bit is what
 * set 1 itself uses to mean "released".
 */
#define USBKBD_E0 0x80

/**
 * @brief The set-1 scancode a HID usage translates to.
 *
 * Exposed rather than kept private because it is the whole of the translation
 * and it is a pure function - which is what makes the table testable without a
 * controller, a device or a single register read.
 *
 * @param usage A usage from the keyboard page, including the modifiers at
 *              0xE0 to 0xE7.
 * @return 0 when set 1 has no code for that usage, otherwise the make code,
 *         with USBKBD_E0 set when it needs the prefix.
 */
uint8_t usbkbd_scancode_for(uint8_t usage);

/**
 * @brief Takes one boot report and emits whatever changed since the last one.
 *
 * A boot report says which keys are held right now, not which ones just moved,
 * so a key held across two reports appears in both and must produce nothing the
 * second time. That is the trap the shape of this protocol sets: a driver that
 * emitted what it saw would repeat every held key at every poll, which at one
 * report per timer tick is a hundred repeats a second.
 *
 * Releases are emitted before presses. A keyboard that reports a key going down
 * in the same report as another going up gives no ordering of its own, and
 * letting go before pressing is the order that cannot produce a chord the user
 * did not type.
 *
 * @param report USBKBD_REPORT_LEN bytes. Ignored when 0, and ignored whole when
 *               it reports rollover.
 */
void usbkbd_report(const uint8_t *report);

/**
 * @brief Forgets the previous report.
 *
 * Called when a keyboard is configured, because a newly attached device has no
 * previous state and diffing against another keyboard's would emit releases for
 * keys this one never had. The test module uses it for the same reason: so that
 * a case begins from a known state rather than from whatever ran before it.
 */
void usbkbd_reset(void);

/**
 * @brief Whether a USB keyboard has been configured and is being polled.
 *
 * Reported by the boot log and by lsusb. It is not a claim that anybody typed
 * anything - only that the endpoint is open and its transfers are being queued.
 */
int usbkbd_attached(void);

/**
 * @brief Records that a keyboard is now configured, and clears the state.
 *
 * Called by the controller driver once the interrupt endpoint is open. The two
 * halves of that - forgetting the old report and marking the driver live - are
 * one call because doing either without the other is a bug rather than a
 * choice.
 */
void usbkbd_attach(void);

#endif // USBKBD_H
