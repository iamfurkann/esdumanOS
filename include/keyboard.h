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