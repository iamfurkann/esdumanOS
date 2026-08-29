#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

/**
 * @file console.h
 * @brief Where the terminal's characters actually land.
 *
 * drivers/tty.c holds three terminals, each a hundred rows of eighty cells, and
 * knows everything about escape sequences, scrollback and cursors. Until v1.6.0
 * it also knew that a cell is written by storing sixteen bits at 0xC00B8000 and
 * that a cursor is moved by writing two CRT controller registers. Six lines of
 * eight hundred, and those six were the entire reason the terminal could not
 * work on a machine built after about 2010, where there is no text mode to write
 * into.
 *
 * This is the seam those six lines go through. A backend is asked to put a cell
 * somewhere and to move the cursor; it is never asked what a cell means, because
 * the entry format below is the one tty.c already stores and changing it would
 * be a change to all eight hundred lines rather than to six.
 *
 * Two backends exist. VGA writes text-mode cells the way the kernel always has.
 * The framebuffer backend draws each cell as an 8x16 glyph into a linear pixel
 * buffer the bootloader handed over, which is the only kind of screen UEFI
 * offers.
 */

/** @brief Columns the console has. tty.c's buffers are this wide. */
#define CONSOLE_COLS 80

/** @brief Rows the console has, status bar included. */
#define CONSOLE_ROWS 25

/** @brief Pixel width of one character cell. */
#define FONT_WIDTH 8

/** @brief Pixel height of one character cell. */
#define FONT_HEIGHT 16

/** @brief First byte the font covers. */
#define FONT_FIRST_GLYPH 32

/** @brief Bytes the font covers: 32 through 126 inclusive. */
#define FONT_GLYPH_COUNT 95

/** @brief The glyph bitmaps, in drivers/console_font.c. */
extern const uint8_t console_font[FONT_GLYPH_COUNT][FONT_HEIGHT];

/** @brief Drawn for any byte outside the covered range. */
extern const uint8_t console_font_fallback[FONT_HEIGHT];

/**
 * @brief One way of getting characters onto a screen.
 *
 * Neither handler validates its coordinates: console_put_cell() and
 * console_set_cursor() do that once, above, so that a second backend cannot
 * forget to. That is the same arrangement blockdev_read() has with its drivers,
 * and for the same reason.
 */
typedef struct {
    const char *name;

    /** Draws one cell. @p entry is character in the low byte, colour in the high. */
    void (*put_cell)(size_t x, size_t y, uint16_t entry);

    /** Moves the cursor. An @p x of CONSOLE_COLS or above hides it. */
    void (*set_cursor)(size_t x, size_t y);
} console_backend_t;

/**
 * @brief Selects the VGA text-mode backend.
 *
 * Always available: the mapping it writes through is set up by the bootstrap
 * code before any C runs, so this works before paging is up and before anything
 * has been allocated.
 */
void console_use_vga(void);

/**
 * @brief Selects the framebuffer backend, drawing into an already-mapped buffer.
 *
 * The mapping is the caller's job rather than this file's. A framebuffer is
 * device memory that has to be found in the Multiboot information and mapped
 * uncached, and none of that is knowledge a glyph blitter needs - keeping it out
 * is also what lets the test module point this at an ordinary array in RAM.
 *
 * @param base Mapped base of the buffer.
 * @param pitch Bytes between the start of one pixel row and the next. Not
 *              width * 4: a framebuffer may be padded, and assuming otherwise
 *              draws a picture that shears.
 * @param width Pixels across.
 * @param height Pixels down.
 * @param bpp Bits per pixel. Only 32 is accepted.
 * @return E_OK, or E_INVAL for a buffer this backend cannot draw into - in which
 *         case the active backend is left exactly as it was.
 */
int console_use_framebuffer(void *base, uint32_t pitch, uint32_t width,
                            uint32_t height, uint8_t bpp);

/**
 * @brief The backend in use. Never 0: text mode is the starting state.
 *
 * On a framebuffer boot the starting state is also invisible, and briefly. The
 * bootloader has put the machine into a graphics mode before the kernel's first
 * instruction, so the text-mode cells written until paging is up and the
 * framebuffer is mapped go to memory the display does not read. Nothing is lost
 * by that: tty.c fills its cell buffers the whole time, and the repaint that
 * follows the mapping puts every one of those lines on the screen at once.
 */
const console_backend_t *console_active(void);

/**
 * @brief Draws one cell, if there is a backend and the coordinates are on screen.
 */
void console_put_cell(size_t x, size_t y, uint16_t entry);

/**
 * @brief Moves the cursor, or hides it when @p x is off the right edge.
 */
void console_set_cursor(size_t x, size_t y);

#endif // CONSOLE_H
