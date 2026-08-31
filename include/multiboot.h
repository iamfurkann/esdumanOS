#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

/**
 * @brief The magic a bootloader leaves in eax, one per specification.
 *
 * Both specifications enter the same way - magic in eax, information structure
 * in ebx - which is why boot.asm has two headers and one entry point.
 */
#define MULTIBOOT1_BOOTLOADER_MAGIC 0x2BADB002
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

/*
 * The flags this kernel actually tests, named. They were written as bare
 * constants at each site - `flags & 0x00000004`, `flags & (1 << 6)` - which was
 * readable while one bootloader filled them in. With a translator that has to
 * *set* them, the same bit written from two places under two spellings is the
 * kind of link nothing watches.
 */
#define MULTIBOOT_FLAG_MEMORY      (1 << 0)   /**< mem_lower and mem_upper. */
#define MULTIBOOT_FLAG_CMDLINE     (1 << 2)   /**< cmdline points at a string. */
#define MULTIBOOT_FLAG_MMAP        (1 << 6)   /**< mmap_addr and mmap_length.  */

/**
 * @brief Set in flags when the framebuffer fields below are filled in.
 */
#define MULTIBOOT_FLAG_FRAMEBUFFER (1 << 12)

/**
 * @brief framebuffer_type for a linear buffer of RGB pixels.
 *
 * The other two values are 0 for a paletted buffer and 2 for EGA text. A
 * bootloader that could not set the graphics mode reports 2 and leaves the
 * machine in text mode, which is a case that has to be recognised rather than
 * assumed away: it is the difference between drawing pixels and scribbling over
 * the text-mode screen.
 */
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1

/**
 * @brief Multiboot information structure provided by the bootloader
 *
 * This stopped at mmap_addr until v1.6.0, which was every field anything read.
 * The framebuffer fields are at the end of a structure whose layout is fixed by
 * the specification, so reaching them means spelling out everything in between -
 * the drive list, the APM table, the VBE block - whether or not anything looks
 * at them. They are named rather than skipped with padding because a reader who
 * needs to know why there is a gap here should find the answer in the gap.
 *
 * The offsets in the comments are not decoration. tests/kernel/test_console.c
 * asserts the important ones with __builtin_offsetof, because a field inserted
 * or resized anywhere above shifts framebuffer_addr, and a shifted
 * framebuffer_addr is the kernel drawing the screen into whatever memory happens
 * to be at the address it read.
 */
typedef struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;       // Offset 40
    uint32_t mmap_length; // Offset 44
    uint32_t mmap_addr;   // Offset 48

    uint32_t drives_length;     // Offset 52
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;         // Offset 68

    uint32_t vbe_control_info;  // Offset 72
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;          // Offset 80
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    /*
     * Sixty-four bits in the specification, and split into halves here rather
     * than declared as one. This kernel is linked without libgcc, so a 64-bit
     * quantity is a value the compiler may decide to handle with a helper call
     * that does not exist - and the address is a 32-bit one on every machine
     * this can run on anyway. The high half is read and refused rather than
     * ignored.
     */
    uint32_t framebuffer_addr_low;   // Offset 88
    uint32_t framebuffer_addr_high;  // Offset 92
    uint32_t framebuffer_pitch;      // Offset 96
    uint32_t framebuffer_width;      // Offset 100
    uint32_t framebuffer_height;     // Offset 104
    uint8_t  framebuffer_bpp;        // Offset 108
    uint8_t  framebuffer_type;       // Offset 109
    uint8_t  color_info[6];          // Offset 110
} __attribute__((packed)) multiboot_info_t;

/**
 * @brief Multiboot memory map entry structure representing a memory region
 */
typedef struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

/* ── Multiboot 2 ────────────────────────────────────────────────────── */

/**
 * @brief Memory map entries the translation below will carry over.
 *
 * A bounded static table, for the reason every other table in this kernel is
 * bounded: the limit is a documented number rather than a memory condition
 * discovered at boot. A real machine reports on the order of ten regions and
 * QEMU fewer; sixty-four is above anything either has produced, and a map
 * longer than this is truncated with a line rather than silently.
 */
#define MB2_MMAP_MAX 64

/**
 * @brief Fills a Multiboot 1 information structure from a Multiboot 2 one.
 *
 * The whole point is that nothing downstream changes. init_pmm() walks a
 * Multiboot 1 memory map, install_framebuffer_console() reads the Multiboot 1
 * framebuffer fields, and selftest.c reads the Multiboot 1 command line - so
 * this translates rather than introducing a second shape for each of them to
 * learn.
 *
 * The memory map is copied into this module's own table because the two layouts
 * differ: Multiboot 2 carries the entry size once in the tag, Multiboot 1
 * carries it in every entry.
 *
 * @param info_phys Physical address of the Multiboot 2 information structure,
 *                  as the bootloader left it in ebx.
 * @param out Receives the translated structure.
 * @return E_OK, or a negative errno when the structure could not be walked.
 */
int mb2_translate(uint32_t info_phys, multiboot_info_t *out);

/**
 * @brief The ACPI RSDP the bootloader handed over, or 0.
 *
 * This is the reason Multiboot 2 is here at all. The tag carries a *copy* of the
 * RSDP rather than a pointer to it, so what this returns is an address inside
 * the information structure - which stays where it is for the life of the
 * system, the same assumption the command line has always been read under.
 *
 * 0 means no tag was present, which is the ordinary case on a BIOS machine and
 * the fatal one on a UEFI machine. acpi_init() falls back to scanning the
 * legacy areas, which works on the first and cannot on the second.
 */
const void *mb2_acpi_rsdp(void);

/**
 * @brief Logs which tags the bootloader provided. Call once klog is up.
 *
 * Separate from mb2_translate() because the translation runs before there is
 * anywhere for a line to go: the memory map is needed by init_pmm(), and the
 * serial port and console are brought up after that. What it saw is kept and
 * reported here.
 *
 * Everything it reports is at INFO, and that is a correction rather than a
 * preference: klog's default level is INFO and drops anything below it before
 * the ring, so a per-tag line at DEBUG would have been a dump that never
 * appeared on any machine.
 */
void mb2_report(void);

#endif
