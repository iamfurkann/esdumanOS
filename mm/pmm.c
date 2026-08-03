/**
 * @file pmm.c
 * @brief Physical Memory Manager implementation with dynamic bitmap.
 */
/*
 * File: pmm.c
 * Purpose: Physical Memory Manager implementation with dynamic bitmap.
 */
#include "pmm.h"
#include "stdio.h"
#include "kernel.h"
#include "klog.h"
#include "errno.h"

extern uint32_t _kernel_end;

uint32_t *pmm_bitmap = 0;
uint32_t pmm_bitmap_size_words = 0;
uint32_t pmm_frames_count = 0;
uint32_t used_memory = 0;
uint32_t actual_total_memory = 0; 

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
        if (mboot_info->flags & (1 << 6)) {
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

    for (uint32_t i = 0; i < pmm_bitmap_size_words; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    if (memory_map_found && (mboot_info->flags & (1 << 6))) {
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
    // Calculate physical end including the 1MB margin!
    uint32_t kernel_end_phys = ((uint32_t)&_kernel_end - 0xC0000000) + 0x100000 + (pmm_bitmap_size_words * 4);
    uint32_t kernel_end_frame = (kernel_end_phys + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t i = kernel_start_frame; i < kernel_end_frame; i++) {
        pmm_bitmap[i / 32] |= (1U << (i % 32));
    }

    used_memory = 0;
    for (uint32_t i = 0; i < pmm_frames_count; i++) {
        if (pmm_bitmap[i / 32] & (1U << (i % 32))) {
            used_memory += PAGE_SIZE;
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
    used_memory += PAGE_SIZE;
    spinlock_release(&pmm_lock);
    return frame * PAGE_SIZE;
}

/**
 * @brief Frees a physical frame.
 * 
 * @param addr The physical address of the frame to free.
 */
void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;
    if (frame < 256) { 
        return; 
    }
    if (frame >= pmm_frames_count) return;
    
    spinlock_acquire(&pmm_lock);
    if ((pmm_bitmap[frame / 32] & (1U << (frame % 32))) == 0) {
        spinlock_release(&pmm_lock);
        return;
    }
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