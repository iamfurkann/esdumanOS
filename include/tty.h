#ifndef TTY_H
#define TTY_H

#include "types.h"

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void update_cursor(size_t x, size_t y);
void terminal_switch(size_t term_no);
void terminal_setcolor(uint8_t fg, uint8_t bg);
void draw_status_bar(const char* left, const char* right);
void terminal_scroll_up(void);
void terminal_scroll_down(void);
#endif