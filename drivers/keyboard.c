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

/**
 * @brief Whether a byte is waiting to be read.
 *
 * So that a caller can find out whether a read would block without performing
 * one. get_keyboard_char() cannot answer this: it returns 0 both for an empty
 * ring and for nothing in particular, and asking it consumes the byte.
 *
 * @return 1 when the ring holds at least one byte.
 */
int keyboard_has_input(void) {
    return kbd_head != kbd_tail;
}

/**
 * @brief How many more bytes the ring can take.
 *
 * One slot is always left empty: head meeting tail is how the ring says it is
 * empty, so filling the last slot would make a full ring indistinguishable from
 * an empty one.
 *
 * @return Free slots, 0 when full.
 */
static int kbd_free_space(void) {
    int used = (kbd_head - kbd_tail + KBD_BUFFER_SIZE) % KBD_BUFFER_SIZE;
    return KBD_BUFFER_SIZE - 1 - used;
}

/**
 * @brief Places a run of bytes in the input ring, all of it or none of it.
 *
 * The all-or-nothing part is the whole reason this is not a loop over
 * kbd_push(). A navigation key is three or four bytes that mean one thing, and a
 * reader handed the first two of them has no way to know the rest was dropped:
 * it waits for a byte that is never coming, or takes the next unrelated key as
 * the tail of the sequence. Refusing the key outright is a keystroke lost, which
 * is what a full ring means anyway.
 *
 * The wakeup is once per sequence rather than once per byte. Waking a reader
 * after the first byte would have it consume a partial sequence for exactly the
 * same reason.
 *
 * @param seq Null-terminated bytes to place.
 */
static void kbd_push_seq(const char *seq) {
    int len = 0;
    while (seq[len]) len++;

    if (len == 0 || kbd_free_space() < len) return;

    for (int i = 0; i < len; i++) {
        kbd_buffer[kbd_head] = seq[i];
        kbd_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
    }

    wakeup_tasks(WAIT_KBD);
}

/**
 * @brief Places a single byte in the input ring.
 *
 * @param c Byte to place; 0 is not placed, since that is what an empty ring
 *          reports.
 */
static void kbd_push(char c) {
    char one[2] = { c, '\0' };
    kbd_push_seq(one);
}

/**
 * @brief The escape sequence a navigation key stands for, or 0.
 *
 * This is what a real terminal sends, and sending anything else would mean every
 * program that ever reads a key needs a table of this system's own invention.
 * The names are xterm's, which is what the sequences the terminal already
 * *writes* were taken from in v0.8.0 - the two halves now speak the same
 * language in both directions.
 *
 * Until this existed these keys reached nobody at all: the arrows were bound to
 * the scrollback, and Home, End, Insert, Delete and both Page keys landed on
 * zero entries in the layout tables and were dropped without trace.
 *
 * The keypad shares these scancodes when Num Lock is off, and is deliberately
 * not told apart: the driver does not track Num Lock, and keypad 8 meaning "up"
 * is the behaviour a user expects from it anyway.
 *
 * @param scancode Make code from the keyboard.
 * @return A null-terminated sequence, or 0 when the key is not a navigation key.
 */
static const char *kbd_sequence_for(uint8_t scancode) {
    switch (scancode) {
        case 0x48: return "\033[A";    /* Up        */
        case 0x50: return "\033[B";    /* Down      */
        case 0x4D: return "\033[C";    /* Right     */
        case 0x4B: return "\033[D";    /* Left      */
        case 0x47: return "\033[H";    /* Home      */
        case 0x4F: return "\033[F";    /* End       */
        case 0x52: return "\033[2~";   /* Insert    */
        case 0x53: return "\033[3~";   /* Delete    */
        case 0x49: return "\033[5~";   /* Page Up   */
        case 0x51: return "\033[6~";   /* Page Down */
        default:   return 0;
    }
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
     * Before the early returns, deliberately. Most paths through the handler
     * bail out - 0xE0 prefixes, modifiers, key releases, function keys - and the
     * timing of those events is entropy just the same as a printable character's.
     * Human keystroke intervals are the best source this machine has.
     *
     * It stays here rather than moving into the translation below, because what
     * it measures is when the interrupt arrived. A synthetic scancode from a test
     * carries no timing at all, and feeding the pool from one would be adding
     * entropy that does not exist.
     */
    entropy_add_event(ENTROPY_SRC_KBD, scancode);

    keyboard_handle_scancode(scancode);
}

/**
 * @brief Turns one scancode into whatever it means, with no hardware involved.
 *
 * Split out of the interrupt handler so that the translation can be driven
 * directly. It reads a port and there is no way to write one from a test, so
 * every rule below - which keys become sequences, which are swallowed, what
 * Ctrl does to a letter - had no coverage at all, in the one path every
 * keystroke in the system goes through.
 *
 * @param scancode Make or break code as the controller delivered it.
 */
void keyboard_handle_scancode(uint8_t scancode) {
    if (scancode == 0xE0) { e0_mode = 1; return; }

    if (e0_mode) {
      e0_mode = 0;
      if (scancode == 0x38) { altgr_pressed = 1; return; }
      else if (scancode == (0x38 | 0x80)) { altgr_pressed = 0; return; }

      /*
       * The controller's fake shift, and it has to be thrown away.
       *
       * Pressing an extended key while Shift is held does not send what one
       * would expect. The controller cancels the shift first and puts it back
       * afterwards, so Shift with Page Up arrives as E0 AA, E0 49, and the
       * release as E0 C9, E0 2A - the AA being a shift *release* that the user
       * never performed. Treating it as one leaves shift_pressed clear at
       * exactly the moment the key that needed it arrives, which is why
       * scrollback on Shift with the Page keys could never have worked: the
       * modifier was cancelled a byte before it was tested.
       *
       * A real shift press or release has no E0 in front of it, so the prefix is
       * the whole of the distinction.
       */
      if (scancode == 0x2A || scancode == 0xAA) return;
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
    
    // F-keys
    if (scancode == 0x3B) { terminal_switch(0); return; }
    if (scancode == 0x3C) { terminal_switch(1); return; }
    if (scancode == 0x3D) { terminal_switch(2); return; }

    /*
     * Scrollback moves off the arrow keys onto Shift with the Page keys, which
     * is where every terminal emulator puts it.
     *
     * It had the arrows because nothing else wanted them. Something does now:
     * the arrows are the only way a program can be told where to move, and a key
     * the driver consumes is a key no program will ever see. Shift is what
     * separates "scroll the window I am looking at" from "move within what I am
     * editing", and the driver already tracks it.
     */
    if (shift_pressed && scancode == 0x49) { terminal_scroll_up(); return; }
    if (shift_pressed && scancode == 0x51) { terminal_scroll_down(); return; }

    /*
     * Navigation keys become the sequences a terminal sends. Checked before the
     * layout tables, which map every one of these to zero - which is to say they
     * were being dropped.
     */
    const char *seq = kbd_sequence_for(scancode);
    if (seq != 0) { kbd_push_seq(seq); return; }

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

    /*
     * Ctrl-Z, and it is consumed the same way and for the same reasons.
     *
     * The difference is what the signal means rather than how it gets there: the
     * foreground group is parked instead of killed, keeping its memory, its
     * descriptors and the instruction it was on, and the shell it belongs to
     * gets the terminal back and prints a prompt. Which is only useful because
     * the shell can put it back - a stop with no fg to undo it would be a way to
     * lose work, not a way to set it aside.
     *
     * The 0x1A has been arriving in the input ring since v0.5.3 exactly as the
     * 0x03 was, and was handed to whatever happened to be reading as data.
     */
    if (c == 0x1A) {
        terminal_writestring("^Z\n");

        send_signal_to_group(foreground_pgid, SIG_TSTP);
        return;
    }

    if (c != 0) {
        kbd_push(c);
    }
}