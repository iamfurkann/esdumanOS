#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "multiboot.h"

/**
 * @brief Default size of a physical memory page.
 * Defines the standard 4KB page size used by the physical memory manager.
 */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/**
 * @brief Maximum number of frames that can be managed by the PMM.
 */
#define PMM_FRAMES_COUNT   32768

/**
 * @brief Size of the bitmap array in 32-bit words.
 * 32768 frames / 32 bits per word = 1024 words.
 */
#define BITMAP_SIZE        1024

/**
 * @brief Total physical memory assumption (128 MB).
 */
#define PMM_TOTAL_MEMORY   134217728

/**
 * @brief Fallback memory size (16 MB) used if Multiboot info is unavailable.
 */
#define PMM_FALLBACK_MEMORY (16 * 1024 * 1024)

/**
 * @brief Initializes the Physical Memory Manager (PMM).
 * Uses the provided Multiboot information to determine available memory regions.
 * 
 * @param mboot_info Pointer to the Multiboot information structure.
 */
void init_pmm(multiboot_info_t *mboot_info);

/**
 * @brief Allocates a single physical frame.
 * Scans the bitmap for a free frame, marks it as used, and returns its physical address.
 * 
 * @return The physical address of the allocated 4KB frame, or 0 if no memory is available.
 */
uint32_t pmm_alloc_frame(void);

/**
 * @brief Frees a previously allocated physical frame.
 * Clears the corresponding bit in the bitmap, making the frame available for future allocations.
 * 
 * @param addr The physical address of the frame to free.
 */
void pmm_free_frame(uint32_t addr);

/**
 * @brief Retrieves the total amount of physical memory managed by the system.
 * 
 * @return Total memory size in bytes.
 */
uint32_t pmm_get_total_memory(void);

/**
 * @brief Retrieves the total amount of free physical memory currently available.
 * 
 * @return Free memory size in bytes.
 */
uint32_t pmm_get_free_memory(void);

#endif