#ifndef EARLYFB_H
#define EARLYFB_H

#include "types.h"
#include "multiboot.h"

/**
 * @file earlyfb.h
 * @brief A screen before there is anything to allocate one with.
 *
 * Until v1.13.0 the framebuffer was mapped in the middle of the boot, after the
 * physical allocator and after the page tables, because mapping needed both.
 * Everything before that point drew into text-mode video memory - and on a
 * machine the bootloader left in a graphics mode, which is every machine that
 * boots UEFI, nobody saw any of it.
 *
 * That was harmless under QEMU, where VGA text exists and the early lines appear
 * on it. On the first physical machine this project was tried on it meant the
 * first half of the boot was silent: five stages with no way to report, and a
 * failure in any of them producing exactly the same black screen as a failure in
 * any other. Diagnosing it cost one bit per boot.
 *
 * So the framebuffer is mapped here instead, in the first line of kernel_main,
 * using two things that exist before anything else does: the page directory
 * boot.asm built, and 4 MB pages, which need no page table and therefore no
 * allocator. From that point the console is real and every stage can speak.
 */

/**
 * @brief Why an early framebuffer could not be set up.
 *
 * Returned rather than logged, because at the moment this runs there is nowhere
 * to log to - that is the problem it exists to solve. The caller turns it into a
 * colour, which is the only thing a machine with no console can still say.
 */
typedef enum {
    EARLY_FB_OK = 0,
    EARLY_FB_NONE,        /**< The bootloader reported no framebuffer.        */
    EARLY_FB_NOT_RGB,     /**< It is a text mode, or a paletted one.          */
    EARLY_FB_ABOVE_4G,    /**< Above 4 GB and PSE-36 cannot reach it.         */
    EARLY_FB_TOO_LARGE,   /**< It does not fit the window reserved for it.    */
    EARLY_FB_BAD_FORMAT   /**< A pixel format the console cannot draw into.   */
} early_fb_status_t;

/**
 * @brief What the screen is painted when the console cannot be had.
 *
 * The only sentence a kernel with no console can still speak, so the vocabulary
 * is small and the words are far apart - a reader photographing a laptop screen
 * has to tell them apart from across a room.
 *
 * Two of the five refusals can be painted and three cannot, and that is a limit
 * rather than an omission: EARLY_FB_NONE, EARLY_FB_NOT_RGB and
 * EARLY_FB_ABOVE_4G are all cases where there is no mapping to paint through.
 * The first two are also the cases where the text backend is the right answer
 * and probably works. The third is the one that stays dark, and `bootstop=` is
 * what is left for it.
 */
#define EARLY_FB_COLOUR_BAD_FORMAT  0x00FF00FFu   /* magenta */
#define EARLY_FB_COLOUR_MAP_FAILED  0x0000FFFFu   /* cyan    */

/**
 * @brief Maps the bootloader's framebuffer and gives the console to it.
 *
 * Call once, as early in kernel_main as the multiboot information allows, and
 * before anything that might fail. Sets CR4.PSE and writes 4 MB entries into
 * boot.asm's page directory; both are undone by nothing, because init_paging()
 * replaces that directory outright and maps the framebuffer again at the same
 * address through vmm_map_fixed().
 *
 * @param mb The multiboot information, already translated where the bootloader
 *           spoke Multiboot 2.
 * @return EARLY_FB_OK, or the reason it could not.
 */
early_fb_status_t early_fb_init(const multiboot_info_t *mb);

/**
 * @brief Fills the whole screen with one colour, or does nothing.
 *
 * The last thing a kernel with no console can say. Safe to call whether or not
 * early_fb_init() succeeded: without a mapping it returns having done nothing,
 * which is the one case that stays dark and is stated as a limit rather than
 * hidden.
 *
 * @param colour 0x00RRGGBB.
 */
void early_fb_fill(uint32_t colour);

/**
 * @brief Whether early_fb_init() mapped a framebuffer this boot.
 */
int early_fb_present(void);

/**
 * @brief The 4 MB entry covering one page directory index, or 0.
 *
 * init_paging() builds a directory of its own and loads it, and the instant it
 * does the early mapping stops existing - the framebuffer's index gets an empty
 * page table like every other kernel index. That left the console pointing at an
 * address with nothing behind it, and the first line printed after paging was a
 * page fault at 0xE1xxxxxx.
 *
 * So the entry is carried across rather than remade. The mapping exists in both
 * directories and is never absent for an instruction, which is the only version
 * of this that has no window to get wrong: a re-map after the fact is a handover,
 * and a handover has a moment in it.
 *
 * @param pde_index Index into the page directory, 0 to 1023.
 * @return The entry to install, or 0 when that index is not the framebuffer's.
 */
uint32_t early_fb_pde(uint32_t pde_index);

/* ── The part that can be asserted without a machine ─────────────────── */

/**
 * @brief How many 4 MB entries a framebuffer of this size and address needs.
 *
 * Split out and exported for the reason every arithmetic decision in this kernel
 * has been since v1.9.0: the interesting cases are the ones no machine here
 * produces. A framebuffer starting three megabytes into a 4 MB region needs one
 * more entry than its size alone suggests, and no machine this project can boot
 * will ever demonstrate that.
 *
 * @param phys Physical base of the framebuffer.
 * @param bytes Its length.
 * @return The number of 4 MB page directory entries the mapping covers, or 0
 *         when the request is not one that can be mapped.
 */
uint32_t early_fb_pde_count(uint32_t phys, uint32_t bytes);

/**
 * @brief Checks a bootloader's framebuffer report without mapping anything.
 *
 * The same order of checks early_fb_init() makes, so that a test can walk every
 * refusal without a page directory to write into.
 */
early_fb_status_t early_fb_check(const multiboot_info_t *mb);

/**
 * @brief Whether this processor can address above 4 GB with 4 MB pages.
 *
 * PSE-36, CPUID leaf 1, EDX bit 17. Everything since the Pentium II has it, and
 * the check is here rather than assumed because this kernel has been caught once
 * already assuming a capability that held on the machine it was written against.
 */
int early_fb_pse36(void);

/**
 * @brief Builds the page directory entry for one 4 MB page of the framebuffer.
 *
 * The reason this release exists in the shape it does. The first machine this
 * project was carried to put its framebuffer above 4 GB, and a 32-bit kernel with
 * no PAE was assumed to have no way to reach it - so the screen was refused, the
 * console fell back to text mode that machine does not have, and every failure
 * for two releases looked like the same black rectangle.
 *
 * PSE-36 is the way. A 4 MB page directory entry carries physical address bits
 * 31:22 where they look like they are, and bits 39:32 in bits 20:13 - so forty
 * bits of physical address through a mechanism this file was already using for a
 * different reason. Bit 21 is reserved and must be left clear; writing the high
 * half one bit too far is a mapping that points somewhere else entirely and reads
 * back as plausible pixels.
 *
 * Exported because it is arithmetic, and arithmetic can be asserted without the
 * machine that needs it - which is the only way this particular case will ever be
 * covered, since no machine available to this project puts a framebuffer up
 * there.
 *
 * @param phys_low Low 32 bits of the 4 MB-aligned physical base.
 * @param phys_high Bits 39:32 of it; anything above bit 39 cannot be encoded.
 * @param flags Page flags to OR in, below bit 13.
 * @return The entry, or 0 when the address cannot be encoded.
 */
uint32_t early_fb_pde_entry(uint32_t phys_low, uint32_t phys_high, uint32_t flags);

/**
 * @brief The page flags a framebuffer entry should carry.
 *
 * A screen mapped uncached is correct and unusably slow: every pixel becomes its
 * own transaction across the bus, and a console refresh writes thousands. That
 * cost nothing under an emulator whose framebuffer is host memory, and on the
 * first real machine it made the keyboard feel broken - keys arriving late and
 * in bursts - while the keyboard was fine and the screen was holding the kernel
 * up.
 *
 * Write-combining is the type in between uncached and cached: writes are
 * gathered in the processor and sent as bursts, with no read caching. Exactly
 * what a framebuffer wants, and exactly what a register file must not have -
 * which is why this is a separate decision from PAGE_KERNEL_MMIO rather than a
 * change to it.
 *
 * @param write_combining Non-zero when PAT is available and entry 1 has been
 *                        made write-combining.
 * @return Flags for the low bits of a 4 MB page directory entry.
 */
uint32_t early_fb_pde_flags(int write_combining);

#endif // EARLYFB_H
