/*
 * File: earlyfb.c
 * Purpose: A screen before there is anything to allocate one with.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Two facts make this possible, and both of them are about what is *not* needed.
 *
 * A 4 MB page needs no page table, so it needs no allocator, so it can be set up
 * before the physical memory manager exists. And boot.asm's page directory is
 * still the one in CR3 at the top of kernel_main, so there is a directory to
 * write into before init_paging() has built one. Between them, the framebuffer
 * can be mapped in the first line of C this kernel runs.
 *
 * The mapping is carried rather than remade. init_paging() builds a directory of
 * its own, and rather than laying a page table over the framebuffer's index it
 * copies the entry across - so the mapping exists in both directories and is
 * absent for no instruction at all. The first version did re-map it afterwards
 * and the window between the two was real: the console was live, the address was
 * dead, and the first printk() after paging was a page fault.
 *
 * And the address the screen lives at is a constant rather than whatever an
 * allocator returns, because that is what makes carrying it possible.
 *
 * Nothing here logs, and that is the point rather than an oversight: this runs
 * before the serial port and before the console, because it is what makes the
 * console possible. It answers with a status and the caller turns that into a
 * colour.
 */
#include "earlyfb.h"
#include "types.h"
#include "errno.h"
#include "paging.h"
#include "console.h"

/* boot.asm's page directory, at its higher-half address. The kernel is linked at
 * 0xC0000000 and the symbol resolves there; the hardware needs the physical one,
 * which is the same value minus the offset - but nothing here reloads CR3, so
 * only the virtual address is used. */
extern uint32_t boot_page_directory[1024];

#define PDE_PRESENT   0x001
#define PDE_WRITE     0x002
#define PDE_PWT       0x008   /* write-through */
#define PDE_PCD       0x010   /* cache disable */
#define PDE_PAGE_SIZE 0x080   /* PS: this entry maps 4 MB, not a page table */

#define PDE_SPAN      0x400000u   /* 4 MB */

/* Where the framebuffer ended up, once. Zero until early_fb_init() succeeds. */
static uint8_t *early_fb_base = 0;
static uint32_t early_fb_pitch = 0;
static uint32_t early_fb_width = 0;
static uint32_t early_fb_height = 0;

/**
 * @brief Whether this processor has the page attribute table.
 *
 * CPUID leaf 1, EDX bit 16. Everything since the Pentium III has it.
 */
static int early_fb_pat_supported(void) {
    uint32_t eax, ebx, ecx, edx;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));

    return (edx & (1u << 16)) != 0;
}

/**
 * @brief Makes PAT entry 1 write-combining, so a page can ask for it.
 *
 * A framebuffer mapped uncached is correct and unusably slow. Uncached is the
 * right answer for a controller's registers - a write that sits in a cache line
 * is a write the device never sees - and it is the wrong one for a screen: every
 * pixel becomes its own transaction across the bus, and the console writes
 * thousands of them per refresh. Under QEMU that costs nothing, because its
 * framebuffer is host memory. On the machine this project was carried to it made
 * the keyboard feel broken - keys arriving late and in bursts - and the keyboard
 * was fine. The screen was holding the kernel up.
 *
 * Write-combining is the type in between: writes are gathered in the processor
 * and sent as bursts, with no read caching and no ordering guarantee. Exactly
 * what a framebuffer wants and exactly what a register file must not have.
 *
 * Entry 1 is chosen because it is the one nothing here uses. A page's type is
 * looked up by (PAT << 2) | (PCD << 1) | PWT, so entry 1 is PWT alone -
 * PAGE_KERNEL_ONLY and PAGE_USER_ACCESS set neither bit, and PAGE_KERNEL_MMIO
 * sets both, which is entry 3 and stays uncached. Nothing in this kernel changes
 * meaning.
 *
 * Done here rather than through the elaborate sequence the manual gives for
 * changing PAT on a running system, because nothing has been mapped with the
 * affected type yet: this runs in the first line of kernel_main, before there is
 * anything cached at any address this entry describes.
 */
static void early_fb_pat_write_combine(void) {
    uint32_t lo, hi;

    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x277));   /* IA32_PAT */

    lo = (lo & ~0x0000FF00u) | (0x01u << 8);                   /* entry 1 = WC */

    asm volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(0x277));
}

uint32_t early_fb_pde_flags(int write_combining) {
    uint32_t flags = PDE_PRESENT | PDE_WRITE | PDE_PAGE_SIZE;

    /*
     * PWT alone selects PAT entry 1, which the call above made write-combining.
     * Without PAT the fallback is both bits, which is strong uncached - correct,
     * and the slow behaviour this exists to get away from.
     */
    return write_combining ? (flags | PDE_PWT) : (flags | PDE_PWT | PDE_PCD);
}

int early_fb_pse36(void) {
    uint32_t eax, ebx, ecx, edx;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));

    return (edx & (1u << 17)) != 0;   /* PSE-36 */
}

uint32_t early_fb_pde_entry(uint32_t phys_low, uint32_t phys_high, uint32_t flags) {
    /*
     * Bits 39:32 and no further. PSE-36 gives forty bits of physical address and
     * this kernel refuses the rest rather than dropping it - an address whose
     * high half does not fit is a different address, and the mapping would point
     * somewhere that reads back as perfectly plausible pixels.
     */
    if (phys_high > 0xFF) return 0;

    /* The 4 MB base has to be aligned; the low bits of a PDE are flags. */
    if (phys_low & (PDE_SPAN - 1)) return 0;

    /*
     * Bit 21 is reserved and stays clear, which is why the high half is masked
     * to eight bits above rather than shifted and hoped for. One bit too far and
     * the processor is being handed a reserved bit set in a page directory entry.
     */
    return (phys_low & 0xFFC00000u)
         | ((phys_high & 0xFFu) << 13)
         | (flags & 0x1FFFu);
}

uint32_t early_fb_pde_count(uint32_t phys, uint32_t bytes) {
    uint32_t first;
    uint32_t last;

    if (bytes == 0) return 0;
    if (phys + bytes < phys) return 0;   /* wraps past the top of memory */

    /*
     * From the entry the first byte falls in to the entry the last byte falls
     * in, inclusive. Counting by size alone would be wrong for every framebuffer
     * that does not begin on a 4 MB boundary - which is most of them, and none
     * of the ones this project can boot on.
     */
    first = phys / PDE_SPAN;
    last  = (phys + bytes - 1) / PDE_SPAN;

    return last - first + 1;
}

early_fb_status_t early_fb_check(const multiboot_info_t *mb) {
    uint32_t bytes;

    if (mb == 0) return EARLY_FB_NONE;
    if (!(mb->flags & MULTIBOOT_FLAG_FRAMEBUFFER)) return EARLY_FB_NONE;
    if (mb->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) return EARLY_FB_NOT_RGB;

    /*
     * Above 4 GB is reachable now, and it took a machine to find out that it had
     * to be. This was an unconditional refusal until the first laptop this
     * project was carried to put its framebuffer up there - and refusing it meant
     * no mapping, which meant no colour to report the refusal with, which meant
     * two releases of black screens that all looked the same.
     *
     * PSE-36 carries bits 39:32 inside the page directory entry, so a 4 MB page -
     * which is what this file already uses - reaches forty bits without PAE and
     * without touching anything else in the kernel. What is still refused is a
     * processor that does not have it, and an address above the forty bits it can
     * encode.
     */
    if (mb->framebuffer_addr_high != 0) {
        if (!early_fb_pse36()) return EARLY_FB_ABOVE_4G;
        if (mb->framebuffer_addr_high > 0xFF) return EARLY_FB_ABOVE_4G;

        /*
         * And a framebuffer that starts below 4 GB and runs past it. The count
         * below works on the low half alone, which stops meaning anything across
         * that boundary - a real limit, and a case no firmware produces.
         */
        if (mb->framebuffer_addr_low + (mb->framebuffer_pitch * mb->framebuffer_height)
            < mb->framebuffer_addr_low) {
            return EARLY_FB_ABOVE_4G;
        }
    }

    if (mb->framebuffer_bpp != 32) return EARLY_FB_BAD_FORMAT;
    if (mb->framebuffer_pitch == 0 || mb->framebuffer_height == 0) return EARLY_FB_BAD_FORMAT;
    if (mb->framebuffer_pitch < mb->framebuffer_width * 4) return EARLY_FB_BAD_FORMAT;

    bytes = mb->framebuffer_pitch * mb->framebuffer_height;

    /* The multiplication above is the one that can overflow, and a pitch and a
     * height both come from the bootloader. */
    if (mb->framebuffer_height != 0 && bytes / mb->framebuffer_height != mb->framebuffer_pitch) {
        return EARLY_FB_TOO_LARGE;
    }

    if (early_fb_pde_count(mb->framebuffer_addr_low, bytes) == 0) return EARLY_FB_TOO_LARGE;

    /*
     * The window is a fixed 64 MB and the mapping starts at its base, so what
     * has to fit is the framebuffer rounded out to the 4 MB entries that cover
     * it - not the framebuffer alone.
     */
    if (early_fb_pde_count(mb->framebuffer_addr_low, bytes) * PDE_SPAN >
        (FB_WINDOW_END - FB_WINDOW_BASE)) {
        return EARLY_FB_TOO_LARGE;
    }

    return EARLY_FB_OK;
}

early_fb_status_t early_fb_init(const multiboot_info_t *mb) {
    early_fb_status_t status = early_fb_check(mb);
    uint32_t bytes;
    uint32_t count;
    uint32_t phys_first;
    uint32_t offset;
    uint32_t cr4;

    if (status != EARLY_FB_OK) return status;

    bytes      = mb->framebuffer_pitch * mb->framebuffer_height;
    count      = early_fb_pde_count(mb->framebuffer_addr_low, bytes);
    phys_first = (mb->framebuffer_addr_low / PDE_SPAN) * PDE_SPAN;
    offset     = mb->framebuffer_addr_low - phys_first;

    /*
     * PSE, which every processor since the Pentium has and which nothing in this
     * kernel had needed until now. It changes the meaning of a page directory
     * entry only where the PS bit below is set, so the 4 KB entries boot.asm
     * already wrote and the ones init_paging() will write are untouched.
     */
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 4);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));

    /*
     * And the memory type, which is the difference between a console and a
     * console that holds the machine up. See early_fb_pat_write_combine().
     */
    int wc = early_fb_pat_supported();

    if (wc) early_fb_pat_write_combine();

    for (uint32_t i = 0; i < count; i++) {
        uint32_t vaddr = FB_WINDOW_BASE + i * PDE_SPAN;
        uint32_t entry = early_fb_pde_entry(phys_first + i * PDE_SPAN,
                                            mb->framebuffer_addr_high,
                                            early_fb_pde_flags(wc));

        if (entry == 0) return EARLY_FB_ABOVE_4G;

        boot_page_directory[vaddr / PDE_SPAN] = entry;
    }

    /*
     * Every entry written above has to be flushed, and a page directory is not
     * something invlpg can be pointed at usefully - reloading CR3 drops the whole
     * translation cache, which is what is wanted here and costs nothing on a
     * path that runs once.
     */
    {
        uint32_t cr3;

        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        asm volatile("mov %0, %%cr3" :: "r"(cr3));
    }

    early_fb_base   = (uint8_t *)(FB_WINDOW_BASE + offset);
    early_fb_pitch  = mb->framebuffer_pitch;
    early_fb_width  = mb->framebuffer_width;
    early_fb_height = mb->framebuffer_height;

    if (console_use_framebuffer(early_fb_base, early_fb_pitch, early_fb_width,
                                early_fb_height, 32) != E_OK) {
        /*
         * The mapping stands and the console refused it, which is a different
         * answer from "there is no screen": early_fb_fill() still works, so the
         * caller can still say something in colour.
         */
        return EARLY_FB_BAD_FORMAT;
    }

    return EARLY_FB_OK;
}

int early_fb_present(void) {
    return early_fb_base != 0;
}

uint32_t early_fb_pde(uint32_t pde_index) {
    uint32_t entry;

    if (early_fb_base == 0) return 0;
    if (pde_index >= 1024) return 0;

    if (pde_index < FB_WINDOW_BASE / PDE_SPAN ||
        pde_index >= FB_WINDOW_END / PDE_SPAN) {
        return 0;
    }

    entry = boot_page_directory[pde_index];

    /*
     * Present and a 4 MB page, both checked. The window is larger than any
     * framebuffer in it, so most of its indices were never written and answering
     * with an absent entry would install one that says "mapped" and points at
     * physical zero.
     */
    if (!(entry & PDE_PRESENT) || !(entry & PDE_PAGE_SIZE)) return 0;

    return entry;
}

void early_fb_fill(uint32_t colour) {
    if (early_fb_base == 0) return;

    for (uint32_t y = 0; y < early_fb_height; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(early_fb_base + y * early_fb_pitch);

        for (uint32_t x = 0; x < early_fb_width; x++) row[x] = colour;
    }
}
