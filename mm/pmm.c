/**
 * @file pmm.c
 * @brief Physical Memory Manager implementation with dynamic bitmap.
 */
/*
 * File: pmm.c
 * Purpose: Physical Memory Manager implementation with dynamic bitmap.
 */
#include "pmm.h"
#include "kernel.h"
#include "klog.h"
#include "errno.h"

/**
 * @brief Frames below 1 MB, which this allocator does not hand out or take back.
 *
 * Written as the arithmetic rather than as 256, which is what it was in both
 * places that used it. The number is a relationship - one megabyte divided by
 * the page size - and a relationship spelled as a literal is one that stops
 * being true the moment either side of it moves.
 *
 * Nothing below this is ever allocated either: init_pmm() marks the whole
 * low-memory region used in the bitmap, so pmm_find_first_free() cannot return
 * one. The guards here are the second half of that, for a frame arriving from
 * somewhere other than this allocator.
 */
#define PMM_LOW_MEMORY_FRAMES (0x100000u / PAGE_SIZE)

extern uint32_t _kernel_end;

uint32_t *pmm_bitmap = 0;
uint32_t pmm_bitmap_size_words = 0;
uint32_t pmm_frames_count = 0;
uint32_t used_memory = 0;
uint32_t actual_total_memory = 0;

/*
 * One reference count per frame, laid out immediately after the bitmap.
 *
 * A bit answers "is this frame in use"; copy-on-write needs "by how many", so
 * that the second address space to let go of a shared page is the one that
 * actually returns it. One byte per frame is 32 KB at 128 MB of RAM, and the
 * ceiling it implies - 255 owners - sits far above MAX_TASKS.
 *
 * The table has to be inside the region init_pmm() marks as used below, for the
 * same reason the bitmap does: it lives in physical memory that the allocator
 * would otherwise hand out, and would then be overwritten by whatever received
 * it. See the kernel_end_phys calculation.
 */
static uint8_t *pmm_refcount = 0;

spinlock_t pmm_lock;
static uint32_t lowest_free_idx = 0;

/**
 * @brief Finds the first free frame in the physical memory bitmap.
 * 
 * @return The index of the first free frame, or 0xFFFFFFFF if no free frame is found.
 */
static uint32_t pmm_find_first_free(void) {
    for (uint32_t i = lowest_free_idx; i < pmm_bitmap_size_words; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1U << j))) {
                    lowest_free_idx = i;
                    return (i * 32) + j;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

/**
 * @brief Initializes the Physical Memory Manager.
 * 
 * @param mboot_info Pointer to the multiboot information structure.
 */
void init_pmm(multiboot_info_t *mboot_info) {
    spinlock_init(&pmm_lock);
    lowest_free_idx = 0; 
    actual_total_memory = 0;
    int memory_map_found = 0;

    if (mboot_info != 0) {
        if (mboot_info->flags & MULTIBOOT_FLAG_MMAP) {
            multiboot_memory_map_t *mmap = (multiboot_memory_map_t *)mboot_info->mmap_addr;
            uint32_t highest_addr = 0;
            while ((uint32_t)mmap < mboot_info->mmap_addr + mboot_info->mmap_length) {
                if (mmap->type == 1) {
                    uint32_t end_addr = mmap->addr_low + mmap->len_low;
                    if (end_addr > highest_addr) highest_addr = end_addr;
                }
                mmap = (multiboot_memory_map_t *)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
            }
            if (highest_addr > 0) {
                actual_total_memory = highest_addr;
                memory_map_found = 1;
            }
        }
        if (!memory_map_found && (mboot_info->flags & (1 << 0))) {
            actual_total_memory = (mboot_info->mem_upper * 1024) + (1024 * 1024);
            memory_map_found = 1;
        }
    }

    if (!memory_map_found || actual_total_memory == 0) {
        actual_total_memory = PMM_FALLBACK_MEMORY;
    }

    pmm_frames_count = actual_total_memory / PAGE_SIZE;
    pmm_bitmap_size_words = (pmm_frames_count + 31) / 32;

    // Place pmm_bitmap 1MB after _kernel_end to absolutely ensure we do not overwrite any
    // Multiboot structures (like the memory map) that GRUB places right after the kernel.
    pmm_bitmap = (uint32_t *)((uint32_t)&_kernel_end + 0x100000);

    /* Directly behind the bitmap, one byte per frame. Both tables sit inside the
     * first 16 MB of kernel space, which boot.asm identity maps, so they are
     * reachable before any page table of our own exists. */
    pmm_refcount = (uint8_t *)(pmm_bitmap + pmm_bitmap_size_words);

    for (uint32_t i = 0; i < pmm_bitmap_size_words; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
    for (uint32_t i = 0; i < pmm_frames_count; i++) {
        pmm_refcount[i] = 0;
    }

    if (memory_map_found && (mboot_info->flags & MULTIBOOT_FLAG_MMAP)) {
        multiboot_memory_map_t *mmap = (multiboot_memory_map_t *)mboot_info->mmap_addr;
        while ((uint32_t)mmap < mboot_info->mmap_addr + mboot_info->mmap_length) {
            if (mmap->type == 1) {
                uint32_t start_addr = mmap->addr_low;
                uint32_t length = mmap->len_low;
                for (uint32_t i = 0; i < length; i += PAGE_SIZE) {
                    uint32_t frame = (start_addr + i) / PAGE_SIZE;
                    if (frame < pmm_frames_count) {
                        pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
                    }
                }
            }
            mmap = (multiboot_memory_map_t *)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    } else {
        for (uint32_t i = 0; i < pmm_frames_count; i++) {
            pmm_bitmap[i / 32] &= ~(1U << (i % 32));
        }
    }

    uint32_t kernel_start_frame = 0;
    /*
     * Physical end of everything the kernel already owns: the image, the 1 MB
     * margin GRUB's structures live in, the bitmap - and now the reference
     * table behind it, one byte per frame.
     *
     * That last term is not bookkeeping. Whatever is not counted here is memory
     * the allocator considers free and will hand out, and a table that is handed
     * out is a table that gets overwritten by its new owner. The symptom would
     * be reference counts changing on their own, and it would surface pages
     * later as a frame freed while still in use.
     */
    uint32_t kernel_end_phys = ((uint32_t)&_kernel_end - 0xC0000000) + 0x100000 +
                               (pmm_bitmap_size_words * 4) + pmm_frames_count;
    uint32_t kernel_end_frame = (kernel_end_phys + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = kernel_start_frame; i < kernel_end_frame; i++) {
        pmm_bitmap[i / 32] |= (1U << (i % 32));
    }

    /*
     * Seed the reference counts from the bitmap.
     *
     * The frames marked above never went through pmm_alloc_frame(), so nothing
     * has set their count. Leaving them at zero would mean the first
     * pmm_free_frame() on one of them decrements below zero and wraps to 255,
     * pinning the frame forever - or, read the other way, that a count of zero
     * is ambiguous between "not allocated" and "allocated by the boot path".
     */
    used_memory = 0;
    for (uint32_t i = 0; i < pmm_frames_count; i++) {
        if (pmm_bitmap[i / 32] & (1U << (i % 32))) {
            used_memory += PAGE_SIZE;
            pmm_refcount[i] = 1;
        }
    }
    
    lowest_free_idx = kernel_end_frame / 32;
    
    klog_hex(LOG_LEVEL_INFO, "PMM", "Initialized dynamic bitmap at VADDR", (uint32_t)pmm_bitmap);
    klog_int(LOG_LEVEL_INFO, "PMM", "Total Memory (MB)", actual_total_memory / (1024 * 1024));
}

/**
 * @brief Allocates a physical frame.
 * 
 * @return The physical address of the allocated frame, or 0xFFFFFFFF if out of memory.
 */
uint32_t pmm_alloc_frame(void) {
    spinlock_acquire(&pmm_lock);
    uint32_t frame = pmm_find_first_free();
    if (frame == 0xFFFFFFFF || frame >= pmm_frames_count) {
        spinlock_release(&pmm_lock);
        return 0xFFFFFFFF; // Out of memory
    }
    pmm_bitmap[frame / 32] |= (1U << (frame % 32));
    pmm_refcount[frame] = 1;
    used_memory += PAGE_SIZE;
    spinlock_release(&pmm_lock);
    return frame * PAGE_SIZE;
}

/**
 * @brief Takes an additional reference to an allocated frame.
 *
 * @param addr The physical address of the frame to share.
 */
void pmm_ref_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;

    /* The same low-memory guard pmm_free_frame() applies: frames below 1 MB are
     * not the allocator's to account for. */
    if (frame < PMM_LOW_MEMORY_FRAMES) return;
    if (frame >= pmm_frames_count) return;

    spinlock_acquire(&pmm_lock);

    if ((pmm_bitmap[frame / 32] & (1U << (frame % 32))) == 0) {
        spinlock_release(&pmm_lock);
        klog_hex(LOG_LEVEL_ERROR, "PMM", "Refused to share a frame that is not allocated", addr);
        return;
    }

    if (pmm_refcount[frame] == 0xFF) {
        spinlock_release(&pmm_lock);
        klog_hex(LOG_LEVEL_ERROR, "PMM", "Reference count saturated for frame", addr);
        return;
    }

    /* A boot-path frame that predates the seeding loop would read as zero; treat
     * it as the one owner it actually has rather than counting from nothing. */
    if (pmm_refcount[frame] == 0) pmm_refcount[frame] = 1;

    pmm_refcount[frame]++;
    spinlock_release(&pmm_lock);
}

/**
 * @brief Reads how many owners a frame currently has.
 *
 * A single byte, read without the lock: on x86 that load cannot tear, and every
 * caller is deciding something it will act on immediately with interrupts
 * already masked.
 *
 * @param addr The physical address of the frame.
 * @return Reference count; 0 when out of range.
 */
uint32_t pmm_frame_refcount(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;

    if (frame >= pmm_frames_count) return 0;
    return pmm_refcount[frame];
}

/**
 * @brief Counts the frames more than one address space is using.
 *
 * @return Number of frames with a reference count above one.
 */
uint32_t pmm_get_shared_frames(void) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < pmm_frames_count; i++) {
        if (pmm_refcount[i] > 1) count++;
    }
    return count;
}

/**
 * @brief Frees a physical frame.
 * 
 * @param addr The physical address of the frame to free.
 */
void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;
    if (frame < PMM_LOW_MEMORY_FRAMES) {
        return;
    }
    if (frame >= pmm_frames_count) return;
    
    spinlock_acquire(&pmm_lock);
    if ((pmm_bitmap[frame / 32] & (1U << (frame % 32))) == 0) {
        spinlock_release(&pmm_lock);
        return;
    }

    /*
     * One owner lets go; the frame only goes when the last one does.
     *
     * A count of zero falls through to the release below rather than wrapping:
     * it means nothing ever claimed this frame through pmm_alloc_frame(), and
     * the honest reading of "free it" there is the behaviour this call had
     * before frames could be shared.
     */
    if (pmm_refcount[frame] > 1) {
        pmm_refcount[frame]--;
        spinlock_release(&pmm_lock);
        return;
    }

    pmm_refcount[frame] = 0;
    pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
    used_memory -= PAGE_SIZE;
    if ((frame / 32) < lowest_free_idx) lowest_free_idx = frame / 32;
    spinlock_release(&pmm_lock);
}

/**
 * @brief Gets the total physical memory size.
 * 
 * @return Total memory size in bytes.
 */
uint32_t pmm_get_total_memory(void) { return actual_total_memory; }

/**
 * @brief Gets the available free physical memory size.
 * 
 * @return Free memory size in bytes.
 */
uint32_t pmm_get_free_memory(void) {
    if (used_memory > actual_total_memory) return 0;
    return actual_total_memory - used_memory;
}