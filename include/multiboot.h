#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

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

#endif
