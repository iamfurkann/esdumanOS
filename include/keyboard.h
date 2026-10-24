#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

/**
 * @brief The byte Ctrl-D places in the keyboard buffer: ASCII EOT.
 *
 * Shared with sys_read(), which turns it into a read of zero bytes - the same
 * end-of-file a pipe reports once its last writer closes. It has to be a byte
 * rather than a flag because it travels through the keyboard ring buffer, and it
 * cannot be 0: get_keyboard_char() already returns 0 for "buffer empty".
 */
#define KBD_EOT 0x04

/**
 * @brief Handles keyboard hardware interrupts (IRQ1).
 * 
 * This routine is invoked directly by the interrupt descriptor table (IDT) 
 * whenever a keyboard event (key press or release) triggers a hardware interrupt.
 */
void keyboard_interrupt_handler(void);

/** @brief Status port bit 0: a byte is waiting in the output buffer. */
#define KBD_STATUS_OBF 0x01
/** @brief Status port bit 5: that byte came from the pointing device. */
#define KBD_STATUS_AUX 0x20

/**
 * @brief The most bytes one pass will take before returning.
 *
 * A bound for the reason pci.c's bus walk and the xHCI capability chain are
 * bounded: a controller that reports a full output buffer for ever would hang
 * this loop, and this loop runs in interrupt context. Sixteen is far more than a
 * keystroke burst and far less than a hang.
 */
#define KBD_DRAIN_MAX 16

/**
 * @brief Takes every byte the controller is holding, not just one.
 *
 * The i8042 has one output buffer and two devices behind it, and until v1.13.0
 * the handler read one byte per interrupt and left whatever else was there. On
 * every machine this project could be tested on that was indistinguishable from
 * correct: QEMU raises an interrupt per byte and has no PS/2 pointing device, so
 * there was never a second byte to leave.
 *
 * A laptop has both. Its touchpad is on the same controller, nothing here reads
 * IRQ12, and a pointer byte nobody takes sits in the shared buffer blocking the
 * keystroke behind it - which arrives only when some later interrupt happens to
 * push things along. The keyboard works, one keypress behind, and everything
 * else on the machine is fast: the symptom is a keyboard that feels broken while
 * the clock ticks perfectly.
 *
 * So the buffer is drained rather than sampled, and bytes belonging to the
 * pointing device are read and dropped - dropped because nothing here wants
 * them, read because that is the only way to get them out of the way.
 *
 * This cannot be asserted from the test suite. It reads a port, there is no way
 * to write one from a test, and the machine the suite runs on cannot produce the
 * condition it exists for - the same standing as xhci_take_ownership(), and
 * worth saying rather than leaving to be discovered.
 *
 * @param credit_entropy Non-zero only from IRQ1, where the arrival time of the
 *                       first byte is a real measurement. A byte collected by
 *                       the timer tick arrived at the tick's convenience and its
 *                       timing says nothing, so it is not credited.
 */
void keyboard_drain(int credit_entropy);

/**
 * @brief Turns one scancode into whatever it means, with no hardware involved.
 *
 * The whole of the translation, called by the handler above with the byte it
 * read. Separate so that it can be driven with a scancode of one's choosing:
 * there is no way to write the keyboard controller's output port, so every rule
 * in it - the escape sequences, the Ctrl fold, which keys are swallowed - was
 * untestable, in the one path every keystroke in the system takes.
 *
 * @param scancode Make or break code as the controller delivered it.
 */
void keyboard_handle_scancode(uint8_t scancode);

/**
 * @brief Whether a byte is waiting in the input ring.
 *
 * Answers "would a read block" without performing one, which is what the poll
 * syscall needs and what tells the Escape key from the start of an escape
 * sequence. get_keyboard_char() cannot be used for it: it consumes the byte, and
 * returns 0 both for an empty ring and for a key that produced nothing.
 *
 * @return 1 when at least one byte is waiting.
 */
int keyboard_has_input(void);

/**
 * @brief Fetches the next available character from the keyboard buffer.
 * 
 * Provides a blocking or non-blocking mechanism to retrieve ASCII characters
 * decoded from the raw keyboard scancodes.
 * 
 * @return char The retrieved keyboard character.
 */
char get_keyboard_char(void);

/**
 * @brief Tracks the current active keyboard layout.
 * 
 * Can be used to switch between different regional layouts (e.g., QWERTY vs AZERTY).
 */
extern int current_layout;

#endif // KEYBOARD_H