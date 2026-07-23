#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/**
 * @brief Virtual address for fractal (recursive) mapping of the Page Directory.
 * Allows the Page Directory to map itself to easily modify page tables.
 */
#define RECURSIVE_PD_VADDR 0xFFFFF000

/**
 * @brief Starting virtual address for the Page Tables when using recursive mapping.
 */
#define RECURSIVE_PT_VADDR 0xFFC00000

/**
 * @brief Top of the user-mode (Ring 3) stack space.
 */
#define USER_STACK_TOP     0xBFFFF000

/**
 * @brief Temporary mapping virtual address used during address space cloning.
 */
#define TEMP_MAP_VADDR     0xE0000000

/**
 * @brief Standard page size (4 KB).
 */
#ifndef PAGE_SIZE
#define PAGE_SIZE          4096
#endif

/**
 * @brief Page table/directory flags.
 * PAGE_KERNEL_ONLY: Present=1, Read/Write=1, User=0. Accessible only by kernel.
 */
#define PAGE_KERNEL_ONLY   3

/**
 * @brief Page table/directory flags.
 * PAGE_USER_ACCESS: Present=1, Read/Write=1, User=1. Accessible by user-mode processes.
 */
#define PAGE_USER_ACCESS   7

/**
 * @brief Page table/directory flags.
 * PAGE_NOT_PRESENT: Present=0, Read/Write=1. Page is swapped out or unmapped.
 */
#define PAGE_NOT_PRESENT   2

/**
 * @brief Initializes the paging subsystem.
 * Sets up the kernel page directory and enables hardware paging.
 */
void init_paging(void);

/**
 * @brief Maps a physical address to a virtual address with specified flags.
 * 
 * @param virtual_addr The virtual address to map.
 * @param physical_addr The physical address to back the virtual address.
 * @param flags The access flags for the page (e.g., PAGE_KERNEL_ONLY, PAGE_USER_ACCESS).
 * @return 0 on success, or a negative error code on failure.
 */
int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);

/**
 * @brief Unmaps a previously mapped virtual address.
 * Invalidates the TLB entry to ensure memory consistency.
 * 
 * @param virtual_addr The virtual address to unmap.
 */
void unmap_page(uint32_t virtual_addr);

/**
 * @brief Loads the page directory base address into the CR3 register.
 * 
 * @param dir Physical address of the page directory.
 */
extern void load_page_directory(uint32_t* dir);

/**
 * @brief Enables the paging mechanism by setting the PG bit in the CR0 register.
 */
extern void enable_paging(void);

#endif