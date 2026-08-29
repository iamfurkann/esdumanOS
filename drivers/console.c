/*
 * File: console.c
 * Purpose: The two ways this kernel can put a character on a screen.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Everything here was six lines of drivers/tty.c until v1.6.0. The terminal held
 * three scrollback buffers, parsed escape sequences and tracked cursors across
 * eight hundred lines, and in six of them it stored sixteen bits at 0xC00B8000
 * and wrote two CRT controller registers. Those six were the whole of what tied
 * it to a machine with a text mode, which is a machine nobody has sold for
 * fifteen years.
 *
 * The seam is deliberately narrow: put a cell here, move the cursor there.
 * Neither backend is told what a cell means. The entry is the VGA word tty.c
 * already stores - character low, colour high - and it stays that shape because
 * inventing a nicer one would have been a change to eight hundred lines instead
 * of to six.
 *
 * Bounds are checked once, here, rather than in each backend. That is the same
 * arrangement blockdev_read() has with its drivers, and it exists for the same
 * reason: the check that only lives in the first implementation is the one the
 * second implementation forgets.
 */
#include "console.h"
#include "io.h"
#include "errno.h"

/* ── VGA text mode ──────────────────────────────────────────────────── */

/*
 * The higher-half mapping of 0xB8000, set up by the bootstrap code before any C
 * runs. That is why this backend works before paging is initialised and before
 * anything has been allocated, and why it is the one the kernel starts with.
 */
static volatile uint16_t *const vga_memory = (volatile uint16_t *)0xC00B8000;

static void vga_put_cell(size_t x, size_t y, uint16_t entry) {
    vga_memory[y * CONSOLE_COLS + x] = entry;
}

static void vga_set_cursor(size_t x, size_t y) {
    uint16_t pos = (uint16_t)(y * CONSOLE_COLS + x);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static const console_backend_t vga_backend = {
    .name       = "vga",
    .put_cell   = vga_put_cell,
    .set_cursor = vga_set_cursor
};

/* ── Framebuffer ────────────────────────────────────────────────────── */

/**
 * @brief The sixteen colours a VGA attribute byte can name, as pixels.
 *
 * The text-mode palette, because that is what the attribute byte in every entry
 * means. A framebuffer has no palette of its own, so the meaning has to be
 * carried across rather than assumed.
 */
static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static struct {
    uint8_t *base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    size_t   cols;        /**< Cells that fit across; never above CONSOLE_COLS. */
    size_t   rows;

    /*
     * How large a glyph is drawn, and where the console sits.
     *
     * The terminal is eighty by twenty-five whatever the screen is, which at one
     * pixel per pixel is 640 by 400 - a postage stamp in the corner of anything a
     * machine built this century displays, and a UEFI framebuffer is usually the
     * panel's own resolution. So each glyph pixel is drawn as a square of
     * `scale` by `scale`, at the largest whole number that still leaves the
     * whole console on the screen, and the result is centred.
     *
     * Whole numbers only. A bitmap font scaled by anything else has to decide
     * what to do with half a pixel, and every answer to that looks worse than
     * the small version.
     */
    uint32_t scale;
    uint32_t origin_x;
    uint32_t origin_y;

    /*
     * What was drawn last, so a cell whose contents have not changed is not
     * blitted again.
     *
     * update_screen() in tty.c hands over all nineteen hundred cells on every
     * repaint. In text mode that is four kilobytes of stores and costs nothing;
     * here each cell is a hundred and twenty-eight bytes of pixels, so the same
     * repaint is a quarter of a megabyte - and typing one character causes one.
     * Four thousand bytes of shadow turns that back into one glyph.
     */
    uint16_t shadow[CONSOLE_ROWS][CONSOLE_COLS];

    size_t cursor_x;
    size_t cursor_y;
    int    cursor_shown;
} fb;

/**
 * @brief Draws one cell's pixels, with no reference to what was there before.
 */
static void fb_blit_cell(size_t x, size_t y, uint16_t entry) {
    uint8_t  ch    = (uint8_t)(entry & 0xFF);
    uint8_t  attr  = (uint8_t)(entry >> 8);
    uint32_t fg    = vga_palette[attr & 0x0F];
    uint32_t bg    = vga_palette[(attr >> 4) & 0x0F];

    const uint8_t *glyph =
        (ch >= FONT_FIRST_GLYPH && ch < FONT_FIRST_GLYPH + FONT_GLYPH_COUNT)
            ? console_font[ch - FONT_FIRST_GLYPH]
            : console_font_fallback;

    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];

        for (uint32_t sy = 0; sy < fb.scale; sy++) {
            uint32_t py = fb.origin_y + (y * FONT_HEIGHT + row) * fb.scale + sy;
            uint8_t *line = fb.base + (py * fb.pitch)
                                    + (fb.origin_x + x * FONT_WIDTH * fb.scale) * 4;

            for (uint32_t col = 0; col < FONT_WIDTH; col++) {
                uint32_t pixel = (bits & (0x80 >> col)) ? fg : bg;

                for (uint32_t sx = 0; sx < fb.scale; sx++) {
                    *(uint32_t *)(line + (col * fb.scale + sx) * 4) = pixel;
                }
            }
        }
    }
}

/**
 * @brief Paints the cursor over the bottom of a cell, or takes it away again.
 *
 * A software cursor, because there is no hardware one to program: the CRT
 * controller registers the text-mode backend writes describe a text-mode cursor
 * and there is no text mode here. Removing it means redrawing the cell it was
 * sitting on, which is what the shadow is for.
 */
static void fb_paint_cursor(size_t x, size_t y, int on) {
    if (x >= fb.cols || y >= fb.rows) return;

    if (!on) {
        fb_blit_cell(x, y, fb.shadow[y][x]);
        return;
    }

    uint8_t  attr = (uint8_t)(fb.shadow[y][x] >> 8);
    uint32_t fg   = vga_palette[attr & 0x0F];

    /* The bottom two rows of the glyph, which grow with it. */
    for (uint32_t row = FONT_HEIGHT - 2; row < FONT_HEIGHT; row++) {
        for (uint32_t sy = 0; sy < fb.scale; sy++) {
            uint32_t py = fb.origin_y + (y * FONT_HEIGHT + row) * fb.scale + sy;
            uint8_t *line = fb.base + (py * fb.pitch)
                                    + (fb.origin_x + x * FONT_WIDTH * fb.scale) * 4;

            for (uint32_t col = 0; col < FONT_WIDTH * fb.scale; col++) {
                *(uint32_t *)(line + col * 4) = fg;
            }
        }
    }
}

static void fb_put_cell(size_t x, size_t y, uint16_t entry) {
    if (x >= fb.cols || y >= fb.rows) return;
    if (fb.shadow[y][x] == entry) return;

    fb.shadow[y][x] = entry;
    fb_blit_cell(x, y, entry);

    /* The cursor sits on top of a cell, so redrawing that cell removes it. */
    if (fb.cursor_shown && x == fb.cursor_x && y == fb.cursor_y) {
        fb_paint_cursor(x, y, 1);
    }
}

static void fb_set_cursor(size_t x, size_t y) {
    if (fb.cursor_shown) {
        fb_paint_cursor(fb.cursor_x, fb.cursor_y, 0);
        fb.cursor_shown = 0;
    }

    if (x >= fb.cols || y >= fb.rows) return;

    fb.cursor_x = x;
    fb.cursor_y = y;
    fb.cursor_shown = 1;
    fb_paint_cursor(x, y, 1);
}

static const console_backend_t fb_backend = {
    .name       = "framebuffer",
    .put_cell   = fb_put_cell,
    .set_cursor = fb_set_cursor
};

/* ── Selection ──────────────────────────────────────────────────────── */

/*
 * Text mode until something knows better, and set here rather than by a call.
 *
 * The mapping the VGA backend writes through exists before the first C
 * instruction, so this is the one backend that is always safe to have selected -
 * including before paging, before the heap, and before anything has read the
 * Multiboot information. A boot that was given a framebuffer replaces it once
 * paging is up.
 *
 * Nobody selects it at startup, and that is deliberate. terminal_initialize()
 * is the obvious place and it is the wrong one: SYSCALL_CLEAR_SCREEN calls it,
 * so a backend chosen there would be chosen again every time a program ran
 * `clear` - and on a framebuffer boot that would put the console back into text
 * mode at the first clear screen, with every write after it going somewhere the
 * display does not read. Which backend is right is a fact about how the machine
 * booted, so the boot path is the only thing that gets to say.
 */
static const console_backend_t *active = &vga_backend;

void console_use_vga(void) {
    active = &vga_backend;
}

int console_use_framebuffer(void *base, uint32_t pitch, uint32_t width,
                            uint32_t height, uint8_t bpp) {
    /*
     * Only 32 bits per pixel, and the others are refused rather than
     * approximated. Every UEFI framebuffer and every mode QEMU offers is 32-bit;
     * a 24-bit path would be code no machine available to this project can
     * execute, which is the kind of code that is wrong and never found out.
     */
    if (base == 0 || bpp != 32) return E_INVAL;
    if (width < FONT_WIDTH || height < FONT_HEIGHT) return E_INVAL;
    if (pitch < width * 4) return E_INVAL;

    fb.base   = (uint8_t *)base;
    fb.pitch  = pitch;
    fb.width  = width;
    fb.height = height;

    /*
     * The console stays eighty by twenty-five - growing it means changing the
     * size of tty.c's three scrollback buffers, which is a decision about the
     * terminal rather than about the screen. What is decided here is how large
     * those cells are drawn and where they sit.
     *
     * The largest whole-number scale that still leaves the whole console on the
     * display. At 1024x768 that is one and the console is 640x400; at the
     * 1920x1080 a real panel is likely to report it is two, and the text is
     * twice the size. Scaling by anything other than a whole number has to
     * decide what to do with half a pixel of a bitmap glyph, and every answer
     * looks worse than the small version.
     */
    fb.scale = 1;
    while ((CONSOLE_COLS * FONT_WIDTH * (fb.scale + 1)) <= width &&
           (CONSOLE_ROWS * FONT_HEIGHT * (fb.scale + 1)) <= height) {
        fb.scale++;
    }

    fb.cols = width / (FONT_WIDTH * fb.scale);
    fb.rows = height / (FONT_HEIGHT * fb.scale);
    if (fb.cols > CONSOLE_COLS) fb.cols = CONSOLE_COLS;
    if (fb.rows > CONSOLE_ROWS) fb.rows = CONSOLE_ROWS;

    /* Centred, so that what is left over is a border rather than a corner. */
    fb.origin_x = (width  - (uint32_t)fb.cols * FONT_WIDTH  * fb.scale) / 2;
    fb.origin_y = (height - (uint32_t)fb.rows * FONT_HEIGHT * fb.scale) / 2;

    /*
     * The whole buffer to black first, and not only the part the console
     * occupies. Everything outside it is border now, and a border showing what
     * the bootloader happened to leave behind is worse than no border at all.
     */
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *line = fb.base + (y * pitch);
        for (uint32_t x = 0; x < width; x++) {
            *(uint32_t *)(line + x * 4) = 0;
        }
    }

    /*
     * The shadow says every cell holds a black space, which the buffer now
     * matches. Without clearing first, a cell that really is a black space would
     * be skipped as unchanged and whatever was on the screen would show through.
     */
    for (size_t y = 0; y < CONSOLE_ROWS; y++) {
        for (size_t x = 0; x < CONSOLE_COLS; x++) {
            fb.shadow[y][x] = 0x0720;   /* light grey on black, space */
        }
    }

    fb.cursor_shown = 0;
    fb.cursor_x = 0;
    fb.cursor_y = 0;

    active = &fb_backend;
    return E_OK;
}

const console_backend_t *console_active(void) {
    return active;
}

void console_save(console_state_t *out) {
    if (out == 0) return;

    out->backend = active;
    out->base    = fb.base;
    out->pitch   = fb.pitch;
    out->width   = fb.width;
    out->height  = fb.height;
    out->bpp     = 32;      /* the only one console_use_framebuffer() accepts */
}

int console_restore(const console_state_t *in) {
    if (in == 0 || in->backend == 0) return E_INVAL;

    if (in->backend == &vga_backend) {
        console_use_vga();
        return E_OK;
    }

    /*
     * Back through the front door rather than by restoring the fields.
     *
     * What ran in between drew over the screen and over the shadow, and those
     * two have to agree or the next repaint skips exactly the cells that need
     * redrawing - a cell whose shadow says "already correct" is not drawn, and
     * on a screen somebody else has scribbled on that is the wrong answer.
     * console_use_framebuffer() clears both and puts them back in step.
     */
    return console_use_framebuffer(in->base, in->pitch, in->width, in->height, in->bpp);
}

void console_put_cell(size_t x, size_t y, uint16_t entry) {
    if (active == 0) return;
    if (x >= CONSOLE_COLS || y >= CONSOLE_ROWS) return;

    active->put_cell(x, y, entry);
}

void console_set_cursor(size_t x, size_t y) {
    if (active == 0) return;

    /*
     * Not clamped, and not refused either. tty.c hides the cursor by asking for
     * column eighty, which is off the right edge of an eighty-column screen -
     * the text-mode backend parks the hardware cursor outside the visible area
     * and the framebuffer backend stops drawing one. Rejecting the call here
     * would leave a cursor on the screen with nothing under it.
     */
    active->set_cursor(x, y);
}
