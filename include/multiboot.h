#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

/**
 * @brief Multiboot information structure provided by the bootloader
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
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t mmap_length; 
    uint32_t mmap_addr;   
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
