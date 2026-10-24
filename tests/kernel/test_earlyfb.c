/*
 * File: test_earlyfb.c
 * Purpose: The screen that has to exist before anything can complain about it.
 *
 * This file is part of the esdumanOS test suite.
 *
 * None of this needs a framebuffer, and that is the point rather than a
 * compromise. The cases worth asserting are the ones no machine this project can
 * boot on produces: a framebuffer that does not begin on a 4 MB boundary, one
 * larger than the window reserved for it, one the firmware placed above 4 GB.
 * QEMU produces none of them and a laptop produces one of them once, in the
 * dark, with no console to say which.
 *
 * The same argument the xHCI context stride made in v1.9.0, the endpoint zero
 * packet size in v1.11.0 and the ACPI table walk in v1.12.0. It has now paid
 * four times, and this release is the one where it pays for the console itself.
 */
#include "ktest.h"
#include "earlyfb.h"
#include "paging.h"
#include "multiboot.h"
#include "types.h"
#include "libft.h"

#define MB4 0x400000u

/**
 * @brief Builds a framebuffer report the way a bootloader would.
 */
static void build_fb(multiboot_info_t *mb, uint32_t addr, uint32_t width,
                     uint32_t height, uint32_t pitch, uint8_t bpp, uint8_t type) {
    ft_memset(mb, 0, sizeof(*mb));
    mb->flags                 = MULTIBOOT_FLAG_FRAMEBUFFER;
    mb->framebuffer_addr_low  = addr;
    mb->framebuffer_addr_high = 0;
    mb->framebuffer_width     = width;
    mb->framebuffer_height    = height;
    mb->framebuffer_pitch     = pitch;
    mb->framebuffer_bpp       = bpp;
    mb->framebuffer_type      = type;
}

/**
 * @brief How many 4 MB entries cover a region, which is not a division.
 */
static void run_pde_assertions(void) {
    KTEST_ASSERT(early_fb_pde_count(0, MB4) == 1 &&
                 early_fb_pde_count(MB4, MB4) == 1,
                 "[STRICT] [EARLYFB] a region of exactly four megabytes on its own boundary is one entry");

    /*
     * The case a division would get wrong, and the one no machine here will
     * ever show: two megabytes of framebuffer starting three megabytes into a
     * region crosses into the next one. Sized alone it is "less than 4 MB, so
     * one entry", and mapping one entry would leave the bottom of the screen
     * pointing at nothing.
     */
    KTEST_ASSERT(early_fb_pde_count(3 * 1024 * 1024, 2 * 1024 * 1024) == 2,
                 "[STRICT] [EARLYFB] a region smaller than an entry still spans two when it straddles a boundary");

    /*
     * A real panel, aligned and not. 1920x1080 at 32 bits is 7.9 MB: two entries
     * when it starts on a boundary and three when it does not, which is what a
     * firmware that places it at an arbitrary address produces.
     */
    KTEST_ASSERT(early_fb_pde_count(0xD0000000u, 1920 * 1080 * 4) == 2 &&
                 early_fb_pde_count(0xD0100000u, 1920 * 1080 * 4) == 3,
                 "[STRICT] [EARLYFB] a 1920x1080 panel is two entries aligned and three when it is not");

    KTEST_ASSERT(early_fb_pde_count(0, 0) == 0 &&
                 early_fb_pde_count(0xFFFF0000u, 0x20000u) == 0,
                 "[STRICT] [EARLYFB] nothing to map is refused, and so is a region that runs off the top of memory");
}

/**
 * @brief Every refusal, in the order the boot meets them.
 */
static void run_check_assertions(void) {
    static multiboot_info_t mb;

    ft_memset(&mb, 0, sizeof(mb));
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_NONE &&
                 early_fb_check(0) == EARLY_FB_NONE,
                 "[STRICT] [EARLYFB] a bootloader that reported no framebuffer is told apart from a broken one");

    /*
     * A text mode is not a broken framebuffer, and the difference decides
     * whether the text backend is the right answer or a black screen. On a
     * machine with VGA text it is right; the console keeps it.
     */
    build_fb(&mb, 0xD0000000u, 1920, 1080, 7680, 32, 2);
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_NOT_RGB,
                 "[STRICT] [EARLYFB] a text mode is refused by name rather than drawn into");

    /*
     * Above 4 GB is reachable, and finding out that it had to be cost this
     * project two releases of identical black screens. The first machine it was
     * ever carried to put its framebuffer up there; a 32-bit kernel with no PAE
     * was assumed to have no way to reach it, so the screen was refused - and
     * refusing meant no mapping, which meant no colour to report the refusal
     * with.
     */
    build_fb(&mb, 0xD0000000u, 1920, 1080, 7680, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    mb.framebuffer_addr_high = 1;
    KTEST_ASSERT(!early_fb_pse36() || early_fb_check(&mb) == EARLY_FB_OK,
                 "[STRICT] [EARLYFB] an address above 4 GB is reached through PSE-36 rather than refused");

    /* Past the forty bits PSE-36 encodes, which is refused rather than dropped. */
    build_fb(&mb, 0xD0000000u, 1920, 1080, 7680, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    mb.framebuffer_addr_high = 0x100;
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_ABOVE_4G,
                 "[STRICT] [EARLYFB] and one past the forty bits it encodes is refused, not truncated");

    /*
     * Three ways to be unusable that all mean the same thing to the drawing
     * code: a depth it cannot write, a pitch that does not reach across the
     * width, and a height of nothing.
     */
    build_fb(&mb, 0xD0000000u, 1920, 1080, 7680, 24, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    {
        int all_bad = (early_fb_check(&mb) == EARLY_FB_BAD_FORMAT);

        build_fb(&mb, 0xD0000000u, 1920, 1080, 1920, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
        all_bad = all_bad && (early_fb_check(&mb) == EARLY_FB_BAD_FORMAT);

        build_fb(&mb, 0xD0000000u, 1920, 0, 7680, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
        all_bad = all_bad && (early_fb_check(&mb) == EARLY_FB_BAD_FORMAT);

        KTEST_ASSERT(all_bad,
                     "[STRICT] [EARLYFB] a depth, a pitch and a height this kernel cannot draw into are each refused");
    }

    /*
     * A 4K panel, which is the largest thing the window was sized for and larger
     * than anything this project has met. It has to fit, because the release
     * before this one had a 16 MB window that it did not.
     */
    build_fb(&mb, 0xD0000000u, 3840, 2160, 3840 * 4, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_OK,
                 "[STRICT] [EARLYFB] a 4K panel fits the window, which it did not before this release");

    build_fb(&mb, 0xD0000000u, 7680, 4320, 7680 * 4, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_TOO_LARGE,
                 "[STRICT] [EARLYFB] and one larger than the window is refused rather than mapped short");

    build_fb(&mb, 0xD0000000u, 1920, 1080, 7680, 32, MULTIBOOT_FRAMEBUFFER_TYPE_RGB);
    KTEST_ASSERT(early_fb_check(&mb) == EARLY_FB_OK,
                 "[STRICT] [EARLYFB] and an ordinary panel is accepted");
}

/**
 * @brief The two windows, which must not overlap and must be big enough.
 */
static void run_window_assertions(void) {
    KTEST_ASSERT(FB_WINDOW_END == DEVICE_WINDOW_BASE &&
                 FB_WINDOW_BASE < FB_WINDOW_END &&
                 DEVICE_WINDOW_BASE < DEVICE_WINDOW_END,
                 "[STRICT] [EARLYFB] the framebuffer's window ends exactly where the device window begins");

    /*
     * Sized against the largest panel rather than against a round number. The
     * window this replaced was 16 MB and a 4K framebuffer is 31.6, which is the
     * whole reason it moved.
     */
    KTEST_ASSERT((FB_WINDOW_END - FB_WINDOW_BASE) >= 3840u * 2160u * 4u &&
                 (DEVICE_WINDOW_END - DEVICE_WINDOW_BASE) >= 64u * 1024 * 1024,
                 "[STRICT] [EARLYFB] the framebuffer window holds a 4K panel and the device window still has room behind it");
}

/**
 * @brief The colours, which are the whole vocabulary of a machine with no console.
 */
static void run_colour_assertions(void) {
    KTEST_ASSERT(EARLY_FB_COLOUR_BAD_FORMAT != EARLY_FB_COLOUR_MAP_FAILED &&
                 EARLY_FB_COLOUR_BAD_FORMAT != 0 &&
                 EARLY_FB_COLOUR_MAP_FAILED != 0,
                 "[STRICT] [EARLYFB] the two colours a failed console can show are different, and neither is black");
}


/**
 * @brief The entry that reaches above 4 GB, which no machine here will produce.
 *
 * PSE-36 puts physical address bits 39:32 into page directory entry bits 20:13,
 * with bit 21 reserved between them. Writing the high half one bit too far sets
 * a reserved bit and maps somewhere else entirely - and the pixels that come
 * back from somewhere else look exactly like pixels.
 */
static void run_pde_entry_assertions(void) {
    uint32_t flags = 0x001 | 0x002 | 0x080;   /* present, write, 4 MB page */

    KTEST_ASSERT(early_fb_pde_entry(0xD0000000u, 0, flags) == (0xD0000000u | flags),
                 "[STRICT] [EARLYFB] an address below 4 GB is the entry it looks like");

    /*
     * One above 4 GB: the high half is 1, which belongs in bit 13 and nowhere
     * else. The low base survives untouched and bit 21 stays clear.
     */
    {
        uint32_t e = early_fb_pde_entry(0xD0000000u, 1, flags);

        KTEST_ASSERT(e != 0 && (e & 0xFFC00000u) == 0xD0000000u &&
                     ((e >> 13) & 0xFF) == 1 && (e & (1u << 21)) == 0,
                     "[STRICT] [EARLYFB] the high half of the address goes to bits 20:13 and leaves bit 21 clear");
    }

    KTEST_ASSERT(early_fb_pde_entry(0xD0000000u, 0x100, flags) == 0 &&
                 early_fb_pde_entry(0xD0100000u, 0, flags) == 0,
                 "[STRICT] [EARLYFB] an address past forty bits, or not on a 4 MB boundary, yields no entry");

    /*
     * The memory type, which is the difference between a console and a console
     * that holds the machine up. PWT alone is PAT entry 1, made write-combining
     * at boot; both bits is entry 3, which is strong uncached and is what the
     * screen had before - correct, and slow enough on real hardware to look like
     * a broken keyboard.
     */
    {
        uint32_t wc = early_fb_pde_flags(1);
        uint32_t uc = early_fb_pde_flags(0);

        KTEST_ASSERT((wc & 0x008) && !(wc & 0x010) &&
                     (uc & 0x008) && (uc & 0x010) &&
                     (wc & 0x080) && (uc & 0x080),
                     "[STRICT] [EARLYFB] write-combining is PWT alone and the fallback is both bits, and both map 4 MB");
    }
}

void run_earlyfb_tests(void) {
    printk("\n--- Early Framebuffer Tests ---\n");

    run_pde_assertions();
    run_check_assertions();
    run_pde_entry_assertions();
    run_window_assertions();
    run_colour_assertions();
}
