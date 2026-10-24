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
 * Which matters, because for most of this module's life there was no such
 * machine in the suite: the test targets booted with -kernel and no bootloader,
 * so no framebuffer was ever handed over and the console was in text mode
 * throughout. That stopped being true when test_kernel_uefi started booting
 * through GRUB under OVMF, and stopped mattering less than it looks - the
 * assertions here install a backend of their own either way, so what changed was
 * not the verdict but the timing around it. See irq_off().
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
 * @brief Closes the window between drawing a cell and reading it back.
 *
 * This module is the one place in the suite where an assertion looks at the
 * screen, and the screen has two other authors. draw_status_bar() writes row
 * zero, and rtc_timer_callback() calls it from interrupt context whenever the
 * clock's second changes; update_screen() repaints rows one and below, and every
 * printk this module makes can ask for one. In an eighty-by-twenty-five console
 * those land far from anything a test would inspect. In the four-by-two buffer
 * here, row zero column zero is both the first cell this module draws and the
 * first cell the status bar overwrites.
 *
 * So the drawing and the reading happen together, with interrupts off, and the
 * assertions are made afterwards from what was read. The failure this prevents
 * is not hypothetical: the pixel checks below passed for three releases and then
 * failed on one target of four, because the boot log grew by two lines and moved
 * where the clock's second fell. Nothing about the console had changed.
 *
 * The idiom is test_cow.c's - the flag is saved and only restored if it was set,
 * because a module that unconditionally re-enables interrupts is a module that
 * decides for whatever called it.
 */
static uint32_t irq_off(void) {
    uint32_t eflags;

    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");
    return eflags;
}

/** @brief Restores what irq_off() saved, and nothing more. */
static void irq_restore(uint32_t eflags) {
    if (eflags & 0x200) asm volatile("sti");
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

    const uint8_t *glyph = console_font['A' - FONT_FIRST_GLYPH];
    int row3_matches = 1;
    uint32_t ink, paper, box_left, box_ink, box_right;
    int inside_last, inside_beyond;
    uint32_t eflags;

    guard_arm();

    KTEST_ASSERT(console_use_framebuffer(fake_fb, FAKE_PITCH, FAKE_W, FAKE_H, 32) == E_OK &&
                 console_active() != 0 &&
                 ft_strcmp(console_active()->name, "framebuffer") == 0,
                 "[STRICT] [CONSOLE] the framebuffer backend takes a buffer and becomes current");

    /* Every cell drawn and every pixel read in one closed window; see irq_off(). */
    eflags = irq_off();

    console_put_cell(0, 0, cell('A', attr));

    /*
     * Row three of 'A' is 0b00011000: two pixels of ink in the middle of eight.
     * Reading the row back rather than one pixel of it is what tells a glyph
     * that is drawn from a corrupt table apart from one that is simply drawn.
     */
    for (uint32_t col = 0; col < FONT_WIDTH; col++) {
        uint32_t want = (glyph[3] & (0x80 >> col)) ? red : blue;
        if (px(col, 3) != want) row3_matches = 0;
    }

    ink   = px(3, 3);
    paper = px(0, 3);

    /*
     * A byte the font does not cover. Row three of the fallback box is
     * 0b01111110, so its edges are background and its middle is not - which also
     * says the blitter did not read past the end of the glyph table looking for
     * a character it does not have.
     */
    console_put_cell(1, 0, cell(200, attr));

    box_left  = px(FONT_WIDTH + 0, 3);
    box_ink   = px(FONT_WIDTH + 1, 3);
    box_right = px(FONT_WIDTH + 7, 3);

    /* The last cell this backend has room for. */
    console_put_cell(FAKE_COLS - 1, FAKE_ROWS - 1, cell('W', attr));
    inside_last = guard_intact();

    /*
     * And a cell the console has but this buffer does not. tty.c hands over all
     * eighty columns whatever the screen is; a backend that trusted them would
     * write a kilobyte past the end of a small framebuffer.
     */
    console_put_cell(CONSOLE_COLS - 1, CONSOLE_ROWS - 1, cell('W', attr));
    inside_beyond = guard_intact();

    irq_restore(eflags);

    KTEST_ASSERT(row3_matches,
                 "[STRICT] [CONSOLE] a glyph is drawn pixel for pixel from the font");

    KTEST_ASSERT(ink == red && paper == blue,
                 "[STRICT] [CONSOLE] foreground and background come out of the attribute byte");

    KTEST_ASSERT(box_left == blue && box_ink == red && box_right == blue,
                 "[STRICT] [CONSOLE] a byte outside the font is drawn as the fallback box");

    KTEST_ASSERT(inside_last,
                 "[STRICT] [CONSOLE] drawing the last cell stays inside the buffer");

    KTEST_ASSERT(inside_beyond,
                 "[STRICT] [CONSOLE] and a cell beyond the buffer is not drawn at all");
}

/**
 * @brief The shadow, which is the difference between one glyph and nineteen hundred.
 */
static void run_shadow_assertions(void) {
    const uint8_t attr = 0x14;
    const uint32_t sentinel = 0x12345678;
    uint32_t after_redraw, after_change;
    uint32_t eflags = irq_off();

    console_put_cell(2, 0, cell('B', attr));

    /* Poked in behind the backend's back. If it survives, nothing was drawn. */
    *(uint32_t *)(fake_fb + (2 * FONT_WIDTH * 4)) = sentinel;

    console_put_cell(2, 0, cell('B', attr));
    after_redraw = px(2 * FONT_WIDTH, 0);

    console_put_cell(2, 0, cell('C', attr));
    after_change = px(2 * FONT_WIDTH, 0);

    irq_restore(eflags);

    KTEST_ASSERT(after_redraw == sentinel,
                 "[STRICT] [CONSOLE] a cell redrawn with what it already holds is not drawn again");

    KTEST_ASSERT(after_change != sentinel,
                 "[STRICT] [CONSOLE] and a cell whose contents changed is");
}

/**
 * @brief A cursor that has to be drawn, and taken away again.
 */
static void run_cursor_assertions(void) {
    const uint8_t attr = 0x14;
    const uint32_t red = 0xAA0000;

    /* The bottom two rows of the cell, painted in the foreground colour. */
    uint32_t cursor_row = FONT_HEIGHT + (FONT_HEIGHT - 1);
    uint32_t shown_left, shown_right, hidden;
    int inside;
    uint32_t eflags = irq_off();

    console_put_cell(0, 1, cell(' ', attr));
    console_set_cursor(0, 1);

    shown_left  = px(0, cursor_row);
    shown_right = px(7, cursor_row);

    /*
     * Hidden by asking for a column the screen does not have, which is how
     * update_screen() hides it when the cursor has scrolled out of view. The
     * cell underneath has to come back, and the only record of what was there is
     * the shadow.
     */
    console_set_cursor(CONSOLE_COLS, 1);

    hidden = px(0, cursor_row);
    inside = guard_intact();

    irq_restore(eflags);

    KTEST_ASSERT(shown_left == red && shown_right == red,
                 "[STRICT] [CONSOLE] the cursor is painted over the bottom of its cell");

    KTEST_ASSERT(hidden != red,
                 "[STRICT] [CONSOLE] and asking for a column off the screen takes it away again");

    KTEST_ASSERT(inside,
                 "[STRICT] [CONSOLE] the cursor never leaves the buffer either");
}

/**
 * @brief What the backend refuses, and putting the screen back.
 */
static void run_refusal_assertions(const console_state_t *saved) {
    const console_backend_t *before = console_active();

    KTEST_ASSERT(console_use_framebuffer(fake_fb, FAKE_PITCH, FAKE_W, FAKE_H, 24) == E_INVAL &&
                 console_active() == before,
                 "[STRICT] [CONSOLE] a pixel format this cannot draw is refused, and changes nothing");

    KTEST_ASSERT(console_use_framebuffer(0, FAKE_PITCH, FAKE_W, FAKE_H, 32) == E_INVAL &&
                 console_active() == before,
                 "[STRICT] [CONSOLE] and so is a buffer that is not there");

    /*
     * Back to whatever was there, followed by a repaint so the screen shows the
     * suite again rather than the four-by-two corner this module has been
     * drawing into.
     *
     * This used to call console_use_vga() and assert the name was "vga", with a
     * comment saying that is what both test machines boot in. That was true of
     * both machines at the time and is not a fact about the console: a run under
     * a bootloader that hands over a framebuffer starts on the other backend, and
     * forcing text mode there would take the screen away from every module after
     * this one while asserting, wrongly, that it had put things back.
     *
     * What is restored is what was found - the same discipline test_blockdev.c
     * has with the root block device, and for the same reason.
     */
    console_restore(saved);
    update_screen();

    KTEST_ASSERT(console_active() == saved->backend,
                 "[STRICT] [CONSOLE] and the console is left on the backend the suite arrived with");
}


/**
 * @brief Where the console lands on screens this project has never seen.
 *
 * console_use_framebuffer() clears the whole buffer before it returns, so
 * asserting the layout for a 4K panel through it would mean finding 31 MB to
 * clear. console_layout() is the same arithmetic with the screen taken out of
 * it, which is the only way these sizes can be checked at all - and they are
 * exactly the sizes a real machine reports and QEMU never does.
 */
static void run_layout_assertions(void) {
    console_layout_t l;

    console_layout(1024, 768, &l);
    KTEST_ASSERT(l.scale == 1 && l.cols == CONSOLE_COLS && l.rows == CONSOLE_ROWS,
                 "[STRICT] [CONSOLE] 1024x768 holds the whole console at scale one");

    /*
     * The size a real panel reports, and the first one where the glyphs are
     * drawn twice as large. 80 columns of 8 pixels doubled is 1280, which fits
     * 1920 with room for a border; 25 rows of 16 doubled is 800 within 1080.
     */
    console_layout(1920, 1080, &l);
    KTEST_ASSERT(l.scale == 2 && l.cols == CONSOLE_COLS && l.rows == CONSOLE_ROWS &&
                 l.origin_x == (1920 - CONSOLE_COLS * FONT_WIDTH * 2) / 2,
                 "[STRICT] [CONSOLE] 1920x1080 doubles the glyphs and centres what is left");

    /*
     * Five, and the number is worth writing out because the first version of
     * this assertion guessed four and was wrong. A scale unit is 80 columns of
     * 8 pixels across and 25 rows of 16 down - 640 by 400 - so five is 3200 by
     * 2000 and fits, and six would be 3840 by 2400 on a panel 2160 tall. The
     * height binds, as it does on every 16:9 screen.
     */
    console_layout(3840, 2160, &l);
    KTEST_ASSERT(l.scale == 5 && l.cols == CONSOLE_COLS && l.rows == CONSOLE_ROWS,
                 "[STRICT] [CONSOLE] a 4K panel scales to five rather than leaving the text unreadable");

    /*
     * A laptop panel that is wide enough for the columns and short of the rows.
     * 25 rows of 16 pixels doubled is 800 and the screen is 768, so the scale
     * has to stay at one - the height is what binds, and a scale chosen from the
     * width alone would draw the bottom of the console off the screen.
     */
    console_layout(1366, 768, &l);
    KTEST_ASSERT(l.scale == 1 && l.rows == CONSOLE_ROWS &&
                 l.origin_y == (768 - CONSOLE_ROWS * FONT_HEIGHT) / 2,
                 "[STRICT] [CONSOLE] a screen bound by its height scales for the height, not the width");

    /*
     * An ultrawide, where the two axes disagree by a lot. The console stays
     * eighty by twenty-five and the leftover width becomes border on both sides
     * rather than a console pushed into a corner.
     */
    console_layout(3440, 1440, &l);
    KTEST_ASSERT(l.cols == CONSOLE_COLS && l.rows == CONSOLE_ROWS &&
                 l.origin_x > l.origin_y,
                 "[STRICT] [CONSOLE] an ultrawide keeps 80x25 and turns the extra width into border");
}

void run_console_tests(void) {
    console_state_t saved;

    printk("\n--- Console Backend Tests ---\n");

    /*
     * Taken before anything is drawn, because everything below installs a
     * framebuffer of its own and that overwrites where the real one is.
     */
    console_save(&saved);

    run_layout_assertions();
    run_selection_assertions();
    run_drawing_assertions();
    run_shadow_assertions();
    run_cursor_assertions();
    run_refusal_assertions(&saved);
}
