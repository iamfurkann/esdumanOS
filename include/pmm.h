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
 * @brief Drops one reference to a physical frame, releasing it at the last one.
 *
 * Every allocation starts at one reference, so a caller that allocated a frame
 * and frees it once sees exactly the behaviour this had before frames could be
 * shared: the bit is cleared and the memory goes back to the allocator. A frame
 * that pmm_ref_frame() has handed to a second address space survives this call
 * and is released when its other owner lets go.
 *
 * @param addr The physical address of the frame to release.
 */
void pmm_free_frame(uint32_t addr);

/**
 * @brief Takes an additional reference to an already allocated frame.
 *
 * The other half of copy-on-write: fork() installs the parent's frames in the
 * child rather than duplicating them, and each side must be able to tear its own
 * address space down without pulling the page out from under the other.
 *
 * Refuses a frame that is not currently allocated - that would be a reference to
 * something the allocator is free to hand out - and refuses to wrap at 255. With
 * MAX_TASKS at 16 the ceiling is unreachable; it is a guard, not a policy.
 *
 * @param addr The physical address of the frame to share.
 */
void pmm_ref_frame(uint32_t addr);

/**
 * @brief Reads how many owners a frame currently has.
 *
 * @param addr The physical address of the frame.
 * @return Reference count; 0 for a frame that is not allocated or out of range.
 */
uint32_t pmm_frame_refcount(uint32_t addr);

/**
 * @brief Counts the frames that more than one address space is using.
 *
 * Copy-on-write makes "free memory" an incomplete answer on its own: after a
 * fork almost nothing has been spent, and how much of what is in use is shared
 * rather than owned is not visible anywhere else.
 *
 * @return Number of frames with a reference count above one.
 */
uint32_t pmm_get_shared_frames(void);

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