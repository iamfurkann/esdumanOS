#include "keyboard.h"
#include "io.h"
#include "tty.h"
#include "process.h"

// 0: US, 1: TR
int current_layout = 0; 

/*RING BUFFER IMPLEMENTATION*/
#define KBD_BUFFER_SIZE 256
volatile char kbd_buffer[KBD_BUFFER_SIZE];
volatile int kbd_head = 0; 
volatile int kbd_tail = 0; 

char get_keyboard_char(void) {
    char temp = 0;
    if (kbd_head != kbd_tail) {
        temp = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    }
    return temp;
}

/*LAYOUTS)*/
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

/*(IRQ1 HANDLER)*/
static int shift_pressed = 0;
static int caps_lock = 0;
static int altgr_pressed = 0;
static int e0_mode = 0;

void keyboard_interrupt_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) { e0_mode = 1; return; }

    if (e0_mode) {
      e0_mode = 0;
      if (scancode == 0x38) { altgr_pressed = 1; return; }
      else if (scancode == (0x38 | 0x80)) { altgr_pressed = 0; return; }
    }

    if (scancode & 0x80) {
        uint8_t released_key = scancode & 0x7F;
        if (released_key == 0x2A || released_key == 0x36) shift_pressed = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = 1; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }
    
    // F tuşları ve Scrollback
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

    if (c != 0) {
        int next_head = (kbd_head + 1) % KBD_BUFFER_SIZE;
        if (next_head != kbd_tail) {
            kbd_buffer[kbd_head] = c;
            kbd_head = next_head;
            extern void wakeup_tasks(int reason);
            wakeup_tasks(1); // WAIT_KBD
        }
    }
}