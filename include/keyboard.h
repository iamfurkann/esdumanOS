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