#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

void keyboard_interrupt_handler(void);
char get_keyboard_char(void);
extern int current_layout;

#endif // KEYBOARD_H