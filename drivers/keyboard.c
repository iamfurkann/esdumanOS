/*
 * File: keyboard.c
 * Purpose: PS/2 Keyboard driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "keyboard.h"
#include "io.h"
#include "tty.h"
#include "process.h"
#include "signal.h"
#include "entropy.h"

// 0: US layout, 1: TR layout
int current_layout = 0; 

/* RING BUFFER IMPLEMENTATION */
#define KBD_BUFFER_SIZE 256
volatile char kbd_buffer[KBD_BUFFER_SIZE];
volatile int kbd_head = 0; 
volatile int kbd_tail = 0; 

/**
 * @brief Retrieves the next character from the keyboard ring buffer.
 * @return The next character, or 0 if the buffer is empty.
 */
char get_keyboard_char(void) {
    char temp = 0;
    if (kbd_head != kbd_tail) {
        temp = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    }
    return temp;
}

/* LAYOUTS */
const char kbd_US[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',    
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',         
    0,  '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  0,           
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,         
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '<'         
};

const char kbd_US_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',    
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',          
    0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  0,           
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,         
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '>'         
};

const char kbd_TR[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '*', '-', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 'g', 'u', '\n',    
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 's', 'i', '"',          
    0,  ',', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'o', 'c', '.',  0,           
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,         
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '<'         
};

const char kbd_TR_shift[128] = {
    0,  27, '!', '\'', '^', '+', '%', '&', '/', '(', ')', '=', '?', '_', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 'G', 'U', '\n',    
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'S', 'I', 'e',          
    0,  ';', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 'O', 'C', ':',  0,           
    '*', 0, ' ',  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,         
    0,  0,  0, '-',  0,  0,  0, '+',  0,  0,  0,  0,  0,  0,  0, '>'         
};

const char kbd_TR_altgr[128] = {
    0,  27,  0,   0,  '#', '$',  0,   0,  '{', '[', ']', '}', '\\', '|', '\b',
  '\t', '@',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '~', '\n',    
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  '<',          
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,           
    '*', 0, ' ',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0,
    0,   0,   0, '-',  0,   0,   0, '+',  0,   0,   0,   0,   0,   0,   0, '|'
};

/* IRQ1 HANDLER */
static int shift_pressed = 0;
static int caps_lock = 0;
static int altgr_pressed = 0;
static int e0_mode = 0;
/* Ctrl was the one modifier this driver never tracked, which is why the console
 * had no end-of-file: there was no way to type one. Both Ctrl keys report
 * scancode 0x1D, the left one bare and the right one behind an 0xE0 prefix, and
 * neither is told apart from the other here. */
static int ctrl_pressed = 0;

/**
 * @brief Handles the keyboard interrupt request (IRQ1).
 */
void keyboard_interrupt_handler(void) {
    uint8_t scancode = inb(0x60);

    /*
     * Before the early returns, deliberately. Most paths through this handler
     * bail out - 0xE0 prefixes, modifiers, key releases, function keys - and the
     * timing of those events is entropy just the same as a printable character's.
     * Human keystroke intervals are the best source this machine has.
     */
    entropy_add_event(ENTROPY_SRC_KBD, scancode);

    if (scancode == 0xE0) { e0_mode = 1; return; }

    if (e0_mode) {
      e0_mode = 0;
      if (scancode == 0x38) { altgr_pressed = 1; return; }
      else if (scancode == (0x38 | 0x80)) { altgr_pressed = 0; return; }
    }

    if (scancode & 0x80) {
        uint8_t released_key = scancode & 0x7F;
        if (released_key == 0x2A || released_key == 0x36) shift_pressed = 0;
        if (released_key == 0x1D) ctrl_pressed = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0x1D) { ctrl_pressed = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }
    
    // F-keys and Scrollback
    if (scancode == 0x3B) { terminal_switch(0); return; }
    if (scancode == 0x3C) { terminal_switch(1); return; }
    if (scancode == 0x3D) { terminal_switch(2); return; }
    if (scancode == 0x48) { terminal_scroll_up(); return; }
    if (scancode == 0x50) { terminal_scroll_down(); return; }

    char c = 0;
    if (current_layout == 1) {
        if (altgr_pressed) c = kbd_TR_altgr[scancode];
        else if (shift_pressed) c = kbd_TR_shift[scancode];
        else c = kbd_TR[scancode];
    } else {
        if (shift_pressed) c = kbd_US_shift[scancode];
        else c = kbd_US[scancode];
    }
    if (caps_lock) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }

    /*
     * Ctrl folds a letter to its control code, which is the ASCII rule: clearing
     * the top three bits of 'D' or 'd' both give 0x04, so caps and shift above
     * cannot change the result. Ctrl-D is the one this release needs - it is the
     * end-of-file sys_read() answers with zero bytes - but the transform is
     * general because the alternative is a special case per key, and Ctrl-C will
     * want the same path once there are process groups to send it to.
     *
     * Restricted to letters deliberately. A real terminal swallows Ctrl with
     * digits and punctuation too, but folding those would turn ordinary keys
     * into control bytes nothing reads, and a Ctrl that got stuck - a release
     * event lost - would take the whole keyboard down rather than just the
     * letters. Passing them through unchanged is the smaller change.
     */
    if (ctrl_pressed && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        c &= 0x1F;
    }

    /*
     * Ctrl-C never reaches the input ring.
     *
     * The fold above has been turning it into a 0x03 since v0.5.3, and that byte
     * went into the buffer like any other - so whatever happened to be reading
     * received it as data, and a program that was not reading was not
     * interrupted at all. What was missing was never the key. It was somebody to
     * send it to: interrupting one process is not what Ctrl-C means, and until a
     * process could belong to a group there was no way to name everything the
     * user had started with a single command.
     *
     * Consumed rather than delivered, which is why this returns instead of
     * falling through. A program that wants the byte itself asks for it by
     * catching SIG_INT; leaving it in the ring as well would hand every reader a
     * stray control character after every interrupt.
     */
    if (c == 0x03) {
        /*
         * Echoed the way a terminal echoes it, so that pressing the key leaves a
         * mark whether or not anything was listening. A Ctrl-C that produced no
         * output at all was indistinguishable from a keyboard that had stopped
         * working.
         */
        terminal_writestring("^C\n");

        send_signal_to_group(foreground_pgid, SIG_INT);
        return;
    }

    if (c != 0) {
        int next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
        if (next_head != kbd_tail) {
            kbd_buffer[kbd_head] = c;
            kbd_head = next_head;
wakeup_tasks(WAIT_KBD);
        }
    }
}