/*
 * File: paging.c
 * Purpose: Virtual Memory Manager and paging initialization.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "paging.h"
#include "pmm.h"
#include "stdio.h"
#include "klog.h"
#include "errno.h"
#include "kernel.h"
#include "serial.h"
uint32_t *page_directory;

/**
 * @brief Initializes the paging system.
 */
void init_paging(void) {
    klog(LOG_LEVEL_INFO, "VMM", "Virtual Memory Manager (Paging) starting...");
    serial_print("[PAGING-DBG] Step 1: Allocating PD frame...\n");
    
    uint32_t pd_phys = pmm_alloc_frame();
    if (pd_phys == 0xFFFFFFFF) kernel_panic("Paging initialization failed: OOM");
    serial_print("[PAGING-DBG] Step 2: PD frame allocated. Clearing...\n");
    
    // We access the new directory via the higher-half mapping established by boot.asm
    volatile uint32_t *pd_virt = (volatile uint32_t *)(pd_phys + 0xC0000000);

    for (int i = 0; i < 1024; i++) {
        pd_virt[i] = PAGE_NOT_PRESENT;
    }
    serial_print("[PAGING-DBG] Step 3: PD cleared. Building page tables...\n");
    
    // Pre-allocate ALL page tables for the Kernel Space (3GB - 4GB)
    // Indices 768 to 1022 (1023 is recursive mapping)
    for (int i = 0; i < (1023 - 768); i++) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (pt_phys == 0xFFFFFFFF) kernel_panic("OOM while pre-allocating kernel PTs");
        volatile uint32_t *pt_virt = (volatile uint32_t *)(pt_phys + 0xC0000000);
        
        for (int j = 0; j < 1024; j++) {
            if (i < 4) {
                // Identity map the first 16MB of kernel space to 0-16MB physical
                uint32_t phys_addr = (i * 0x400000) + (j * PAGE_SIZE);
                if (phys_addr == 0) {
                    pt_virt[j] = PAGE_NOT_PRESENT;
                } else {
                    pt_virt[j] = phys_addr | PAGE_KERNEL_ONLY; 
                }
            } else {
                pt_virt[j] = PAGE_NOT_PRESENT;
            }
        }
        pd_virt[768 + i] = pt_phys | PAGE_KERNEL_ONLY;
    }
    serial_print("[PAGING-DBG] Step 4: Page tables built. Setting recursive mapping...\n");
    
    // Recursive mapping at 1023
    pd_virt[1023] = pd_phys | PAGE_KERNEL_ONLY;

    serial_print("[PAGING-DBG] Step 5: Loading new page directory...\n");
    // Load the new page directory (requires physical address)
    page_directory = (uint32_t *)pd_phys;
    load_page_directory((uint32_t *)pd_phys);
    
    serial_print("[PAGING-DBG] Step 6: Page directory loaded! Returning...\n");
    // Note: Identity mapping (0-16MB) is intentionally omitted in this new directory.
    // The previous identity mapping from boot.asm is now flushed out.
}

/**
 * @brief Maps a physical address to a virtual address.
 * 
 * @param virtual_addr The virtual address to map.
 * @param physical_addr The physical address to map to.
 * @param flags The paging flags for the mapped page.
 * @return E_OK on success, or a negative errno on failure.
 */
int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    if ((pd_virt[pd_index] & 1) == 0) {
        uint32_t new_table_phys = pmm_alloc_frame();
        if (new_table_phys == 0xFFFFFFFF) {
            klog(LOG_LEVEL_ERROR, "VMM", "Failed to create page table: Out of memory.");
            return E_NOMEM;
        }
        /* The directory entry must match the privilege the caller asked for.
         * Creating every new page table as user-accessible left the U/S bit set
         * on directory entries covering kernel-only regions; the page table
         * entries still denied access, but the outer gate was open for no
         * reason. */
        uint32_t pde_flags = (flags & 0x04) ? PAGE_USER_ACCESS : PAGE_KERNEL_ONLY;
        pd_virt[pd_index] = new_table_phys | pde_flags;
        asm volatile("invlpg (%0)" ::"r"(pt_virt) : "memory");
        for (int i = 0; i < 1024; i++) pt_virt[i] = 0;
    } else if ((flags & 0x04) && !(pd_virt[pd_index] & 0x04)) {
        /* A user page is going into a table that was created for kernel use.
         * On x86 the effective privilege is the AND of the directory and table
         * entries, so the directory entry has to be opened or Ring 3 could not
         * reach the page at all. Kernel entries in the same table keep their own
         * U/S bit clear and stay protected. */
        pd_virt[pd_index] |= 0x04;
        asm volatile("invlpg (%0)" ::"r"(pt_virt) : "memory");
    }

    if (pt_virt[pt_index] & 1) {
        uint32_t old_phys = pt_virt[pt_index] & 0xFFFFF000;
        uint32_t new_phys = physical_addr & 0xFFFFF000;
        if (old_phys != new_phys) {
            klog(LOG_LEVEL_ERROR, "PMM", "Virtual memory conflict detected.");
            return E_BUSY;
        }
    }

    pt_virt[pt_index] = (physical_addr & 0xFFFFF000) | (flags & 0xFFF);
    asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory");
    return 0; // E_OK
}

/**
 * @brief Unmaps a mapped virtual address.
 * 
 * @param virtual_addr The virtual address to unmap.
 */
void unmap_page(uint32_t virtual_addr) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    if (pd_virt[pd_index] & 1) {
        pt_virt[pt_index] = 0; 
        asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory"); 
    }
}

/**
 * @brief Clones the current page directory for a new process.
 *
 * The new directory shares the kernel half (entries 768..1022) with the caller
 * and starts out with no user-space mappings at all. Callers populate it through
 * map_page() once it is loaded into CR3.
 *
 * @return The physical address of the new page directory, or 0 on failure.
 *         It must be 0 and not a negative errno: the result goes straight into
 *         CR3, so any non-zero error value is loaded as a page directory base
 *         and triple-faults the machine.
 */
uint32_t clone_page_directory(void) {
    klog(LOG_LEVEL_DEBUG, "VMM", "Cloning Page Directory for the new process.");
    uint32_t new_pd_phys = pmm_alloc_frame();
    if (new_pd_phys == 0xFFFFFFFF) {
        klog(LOG_LEVEL_ERROR, "PMM", "Failed to clone PD: Out of memory.");
        return 0;
    }

    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    int res = map_page(TEMP_MAP_VADDR, new_pd_phys, PAGE_KERNEL_ONLY);
    if (res != 0) { // E_OK is assumed to be 0
        klog(LOG_LEVEL_ERROR, "PMM", "Temporary mapping failed during cloning.");
        
        // [SECURITY PATCH 1]: Interrupt Leak Fixed
        // Before exiting, restore interrupts if they were previously enabled!
        if (eflags & 0x200) {
            asm volatile("sti");
        }
        
        // [SECURITY PATCH 2]: Memory Leak Fixed
        // Since the operation is aborted, free the allocated physical page.
pmm_free_frame(new_pd_phys);

        return 0;
    }

    uint32_t *new_pd = (uint32_t *)TEMP_MAP_VADDR;
    uint32_t *current_pd = (uint32_t *)RECURSIVE_PD_VADDR;

    for (int i = 0; i < 1024; i++) {
        if (i >= 768 && i < 1023) {
            /* Kernel half: share the caller's page tables, with the user bit
             * cleared so Ring 3 cannot reach them. */
            new_pd[i] = (current_pd[i] & ~0x04) | PAGE_KERNEL_ONLY;
        }
        else if (i == 1023) {
            new_pd[i] = new_pd_phys | PAGE_KERNEL_ONLY;
        }
        else {
            /* Everything below the kernel split starts unmapped, entries 0..3
             * included. They used to be copied from the caller with the present
             * bit forced on: since those entries read back as PAGE_NOT_PRESENT
             * (2), "(2 & ~4) | 3" produced 3 - present, read/write, page table
             * base 0. Physical page zero, the real-mode interrupt vector table,
             * was therefore used as the page table for the first 16 MB of every
             * address space, shared by every process, and a stray kernel
             * pointer in that range translated through it instead of faulting. */
            new_pd[i] = PAGE_NOT_PRESENT;
        }
    }

    if (eflags & 0x200) {
        asm volatile("sti");
    }

    unmap_page(TEMP_MAP_VADDR);

    return new_pd_phys; 
}