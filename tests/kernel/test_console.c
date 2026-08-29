/*
 * File: test_console.c
 * Purpose: Where the terminal's cells land, in text mode and in pixels.
 *
 * This file is part of the esdumanOS test suite.
 *
 * The framebuffer backend is tested against an array in RAM. That is the same
 * arrangement test_blockdev.c has with its invented disk, and it works for the
 * same reason: console_use_framebuffer() takes an already-mapped buffer rather
 * than a physical address, so the thing that knows how to draw a glyph does not
 * also have to know how a framebuffer is found and mapped. Nothing here needs a
 * machine with a screen.
 *
 * Which matters, because there is no such machine in the suite. Both test
 * targets boot with -kernel and no bootloader, so no framebuffer is ever handed
 * over and the console is in text mode throughout - and every assertion below
 * reaches the same verdict on both.
 *
 * The module puts the console back into text mode before it returns and repaints
 * the screen. While the fake backend is installed, output goes into a four-by-two
 * cell buffer and is clipped away; printk() also writes to the serial port, so
 * the suite's own [PASS] lines are never lost.
 */
#include "ktest.h"
#include "console.h"
#include "multiboot.h"
#include "tty.h"
#include "errno.h"
#include "libft.h"

#define FAKE_COLS  4
#define FAKE_ROWS  2
#define FAKE_W     (FAKE_COLS * FONT_WIDTH)
#define FAKE_H     (FAKE_ROWS * FONT_HEIGHT)
#define FAKE_PITCH (FAKE_W * 4)
#define FAKE_BYTES (FAKE_PITCH * FAKE_H)

/** @brief Bytes past the end, to catch a backend that draws outside its buffer. */
#define FAKE_GUARD 64

static uint8_t fake_fb[FAKE_BYTES + FAKE_GUARD] __attribute__((aligned(4)));

/** @brief The pixel at a position, read back the way the display would. */
static uint32_t px(uint32_t x, uint32_t y) {
    return *(uint32_t *)(fake_fb + (y * FAKE_PITCH) + (x * 4));
}

/** @brief Fills the guard area with a value nothing should overwrite. */
static void guard_arm(void) {
    for (int i = 0; i < FAKE_GUARD; i++) fake_fb[FAKE_BYTES + i] = 0x5A;
}

static int guard_intact(void) {
    for (int i = 0; i < FAKE_GUARD; i++) {
        if (fake_fb[FAKE_BYTES + i] != 0x5A) return 0;
    }
    return 1;
}

/** @brief A cell entry: character in the low byte, colour in the high. */
static uint16_t cell(uint8_t ch, uint8_t attr) {
    return (uint16_t)ch | ((uint16_t)attr << 8);
}

/**
 * @brief What the kernel starts with, and the structure the bootloader fills.
 *
 * The offsets are the assertion. Every field in multiboot_info_t above
 * framebuffer_addr_low exists only to put it in the right place, so a field
 * added, removed or resized anywhere above shifts it - and a shifted address is
 * the kernel drawing the screen into whatever memory happened to be there. There
 * is no symptom short of looking at it.
 */
static void run_selection_assertions(void) {
    const console_backend_t *backend = console_active();

    KTEST_ASSERT(backend != 0 && backend->name != 0 && backend->put_cell != 0,
                 "[STRICT] [CONSOLE] a backend is selected and can draw");

    KTEST_ASSERT(__builtin_offsetof(multiboot_info_t, framebuffer_addr_low) == 88,
                 "[STRICT] [CONSOLE] the framebuffer address sits where the specification puts it");

    KTEST_ASSERT(sizeof(multiboot_info_t) == 116,
                 "[STRICT] [CONSOLE] and the structure is the whole of what a bootloader fills");
}

/**
 * @brief Glyphs, colours, and the bytes the font does not cover.
 */
static void run_drawing_assertions(void) {
    /* Light grey on black is what the terminal starts in; red on blue is
     * chosen here because neither half can be mistaken for the other. */
    const uint8_t attr = 0x14;           /* fg 4 (red), bg 1 (blue) */
    const uint32_t red  = 0xAA0000;
    const uint32_t blue = 0x0000AA;

    guard_arm();

    KTEST_ASSERT(console_use_framebuffer(fake_fb, FAKE_PITCH, FAKE_W, FAKE_H, 32) == E_OK &&
                 console_active() != 0 &&
                 ft_strcmp(console_active()->name, "framebuffer") == 0,
                 "[STRICT] [CONSOLE] the framebuffer backend takes a buffer and becomes current");

    console_put_cell(0, 0, cell('A', attr));

    /*
     * Row three of 'A' is 0b00011000: two pixels of ink in the middle of eight.
     * Reading the row back rather than one pixel of it is what tells a glyph
     * that is drawn from a corrupt table apart from one that is simply drawn.
     */
    const uint8_t *glyph = console_font['A' - FONT_FIRST_GLYPH];
    int row3_matches = 1;
    for (uint32_t col = 0; col < FONT_WIDTH; col++) {
        uint32_t want = (glyph[3] & (0x80 >> col)) ? red : blue;
        if (px(col, 3) != want) row3_matches = 0;
    }

    KTEST_ASSERT(row3_matches,
                 "[STRICT] [CONSOLE] a glyph is drawn pixel for pixel from the font");

    KTEST_ASSERT(px(3, 3) == red && px(0, 3) == blue,
                 "[STRICT] [CONSOLE] foreground and background come out of the attribute byte");

    /*
     * A byte the font does not cover. Row three of the fallback box is
     * 0b01111110, so its edges are background and its middle is not - which also
     * says the blitter did not read past the end of the glyph table looking for
     * a character it does not have.
     */
    console_put_cell(1, 0, cell(200, attr));

    KTEST_ASSERT(px(FONT_WIDTH + 0, 3) == blue && px(FONT_WIDTH + 1, 3) == red &&
                 px(FONT_WIDTH + 7, 3) == blue,
                 "[STRICT] [CONSOLE] a byte outside the font is drawn as the fallback box");

    /* The last cell this backend has room for. */
    console_put_cell(FAKE_COLS - 1, FAKE_ROWS - 1, cell('W', attr));

    KTEST_ASSERT(guard_intact(),
                 "[STRICT] [CONSOLE] drawing the last cell stays inside the buffer");

    /*
     * And a cell the console has but this buffer does not. tty.c hands over all
     * eighty columns whatever the screen is; a backend that trusted them would
     * write a kilobyte past the end of a small framebuffer.
     */
    console_put_cell(CONSOLE_COLS - 1, CONSOLE_ROWS - 1, cell('W', attr));

    KTEST_ASSERT(guard_intact(),
                 "[STRICT] [CONSOLE] and a cell beyond the buffer is not drawn at all");
}

/**
 * @brief The shadow, which is the difference between one glyph and nineteen hundred.
 */
static void run_shadow_assertions(void) {
    const uint8_t attr = 0x14;
    const uint32_t sentinel = 0x12345678;

    console_put_cell(2, 0, cell('B', attr));

    /* Poked in behind the backend's back. If it survives, nothing was drawn. */
    *(uint32_t *)(fake_fb + (2 * FONT_WIDTH * 4)) = sentinel;

    console_put_cell(2, 0, cell('B', attr));

    KTEST_ASSERT(px(2 * FONT_WIDTH, 0) == sentinel,
                 "[STRICT] [CONSOLE] a cell redrawn with what it already holds is not drawn again");

    console_put_cell(2, 0, cell('C', attr));

    KTEST_ASSERT(px(2 * FONT_WIDTH, 0) != sentinel,
                 "[STRICT] [CONSOLE] and a cell whose contents changed is");
}

/**
 * @brief A cursor that has to be drawn, and taken away again.
 */
static void run_cursor_assertions(void) {
    const uint8_t attr = 0x14;
    const uint32_t red = 0xAA0000;

    console_put_cell(0, 1, cell(' ', attr));
    console_set_cursor(0, 1);

    /* The bottom two rows of the cell, painted in the foreground colour. */
    uint32_t cursor_row = FONT_HEIGHT + (FONT_HEIGHT - 1);

    KTEST_ASSERT(px(0, cursor_row) == red && px(7, cursor_row) == red,
                 "[STRICT] [CONSOLE] the cursor is painted over the bottom of its cell");

    /*
     * Hidden by asking for a column the screen does not have, which is how
     * update_screen() hides it when the cursor has scrolled out of view. The
     * cell underneath has to come back, and the only record of what was there is
     * the shadow.
     */
    console_set_cursor(CONSOLE_COLS, 1);

    KTEST_ASSERT(px(0, cursor_row) != red,
                 "[STRICT] [CONSOLE] and asking for a column off the screen takes it away again");

    KTEST_ASSERT(guard_intact(),
                 "[STRICT] [CONSOLE] the cursor never leaves the buffer either");
}

/**
 * @brief What the backend refuses, and putting the screen back.
 */
static void run_refusal_assertions(void) {
    const console_backend_t *before = console_active();

    KTEST_ASSERT(console_use_framebuffer(fake_fb, FAKE_PITCH, FAKE_W, FAKE_H, 24) == E_INVAL &&
                 console_active() == before,
                 "[STRICT] [CONSOLE] a pixel format this cannot draw is refused, and changes nothing");

    KTEST_ASSERT(console_use_framebuffer(0, FAKE_PITCH, FAKE_W, FAKE_H, 32) == E_INVAL &&
                 console_active() == before,
                 "[STRICT] [CONSOLE] and so is a buffer that is not there");

    /*
     * Back to text mode, which is what both test machines boot in, followed by a
     * repaint so the screen shows the suite again rather than whatever was on it
     * when this module started.
     */
    console_use_vga();
    update_screen();

    KTEST_ASSERT(console_active() != 0 &&
                 ft_strcmp(console_active()->name, "vga") == 0,
                 "[STRICT] [CONSOLE] and the console is left where the rest of the suite found it");
}

void run_console_tests(void) {
    printk("\n--- Console Backend Tests ---\n");

    run_selection_assertions();
    run_drawing_assertions();
    run_shadow_assertions();
    run_cursor_assertions();
    run_refusal_assertions();
}
