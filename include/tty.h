#ifndef TTY_H
#define TTY_H

#include "types.h"

/**
 * @brief Enumerates the standard VGA text mode colors.
 * 
 * Defines the 16 hardware-supported color codes utilized by the VGA controller
 * to display text with varying foreground and background properties.
 */
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

/**
 * @brief Initializes the terminal interface.
 * 
 * Clears the VGA text buffer and resets the cursor coordinates,
 * preparing the screen for text output.
 */
void terminal_initialize(void);

/**
 * @brief Outputs a single character to the terminal.
 * 
 * Handles printing the character to the current cursor location and 
 * appropriately updating the cursor coordinates, including handling newlines.
 * 
 * @param c The character to display.
 */
void terminal_putchar(char c);

/**
 * @brief Prints a null-terminated string to the terminal.
 * 
 * @param data The string to display.
 */
void terminal_writestring(const char* data);

/**
 * @brief Updates the hardware cursor position.
 * 
 * Communicates with the VGA controller registers to visually move the blinking 
 * cursor to the specified column and row.
 * 
 * @param x The column index (0-based).
 * @param y The row index (0-based).
 */
void update_cursor(size_t x, size_t y);

/**
 * @brief Switches the active virtual terminal.
 * 
 * Swaps the current VGA text buffer with the buffer of the requested 
 * virtual terminal, providing multi-tty support.
 * 
 * @param term_no The target terminal number to switch to.
 */
void terminal_switch(size_t term_no);

/**
 * @brief Sets the terminal foreground and background text colors.
 * 
 * @param fg The foreground VGA color.
 * @param bg The background VGA color.
 */
void terminal_setcolor(uint8_t fg, uint8_t bg);

/**
 * @brief Draws a status bar at the bottom or top of the screen.
 * 
 * Used for kernel interface visualization, separating UI from logging areas.
 * 
 * @param left Text aligned to the left of the status bar.
 * @param right Text aligned to the right of the status bar.
 */
void draw_status_bar(const char* left, const char* right);

/**
 * @brief Scrolls the terminal contents up by one line.
 * 
 * Shifts the entire VGA text buffer contents up, discarding the top line 
 * and freeing the bottom line for new output.
 */
void terminal_scroll_up(void);

/**
 * @brief Scrolls the terminal contents down by one line.
 * 
 * Shifts the entire VGA text buffer contents down, discarding the bottom line 
 * and freeing the top line for output.
 */
void terminal_scroll_down(void);
#endif