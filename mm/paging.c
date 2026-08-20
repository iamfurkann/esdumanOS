/*
 * File: paging.c
 * Purpose: Virtual Memory Manager and paging initialization.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "klog.h"
#include "errno.h"
#include "serial.h"
#include "uaccess.h"
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

/**
 * @brief One user page recorded between the two halves of copy_user_space().
 */
typedef struct {
    uint32_t vaddr;
    uint32_t frame;
    uint32_t flags;
} copied_page_t;

/**
 * @brief Counts the present pages below the kernel split in the running directory.
 *
 * @return Number of present user-space pages.
 */
static uint32_t count_user_pages(void) {
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t count = 0;

    for (uint32_t pde = 0; pde < 768; pde++) {
        if ((pd[pde] & 1) == 0) continue;

        uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
        for (uint32_t pte = 0; pte < 1024; pte++) {
            if (pt[pte] & 1) count++;
        }
    }
    return count;
}

/**
 * @brief Gives a cloned address space the caller's user pages, shared until written.
 *
 * clone_page_directory() produces a directory that shares the kernel half and has
 * nothing at all below it. This is the other half of fork(): the child needs to
 * see the parent's user pages, and neither of them may see the other's writes.
 *
 * **Shared, not copied.** Every page used to be duplicated outright, which fork()
 * paid for in full - and the overwhelmingly common thing to do next is exec(),
 * which throws the whole duplicate away. What made copying necessary was the
 * teardown path: cleanup_process_memory() released every user frame it found
 * unconditionally, so a frame in two address spaces would be freed twice, the
 * second free handing a live page back to the allocator. Reference counting in
 * the physical allocator removed that obstacle, and both sides can now hold the
 * same frame and let go of it independently.
 *
 * What replaces the copy is a promise to make one later. A frame that was
 * writable is installed read-only on both sides with PAGE_COW set, and the first
 * write from either of them faults into cow_handle_fault(), which hands the
 * writer a private page. A frame that was already read-only is shared as it is:
 * writing to a program's text is an access violation in the child for exactly the
 * reason it is in the parent, and marking it copy-on-write would quietly turn
 * that into a copy.
 *
 * Both directions matter. The parent gives up its write access too - if it kept a
 * writable entry it would edit a page the child is still reading, which is the
 * same bug as sharing outright, just harder to see.
 *
 * Two passes, because no single directory can see both sides:
 *
 * 1. With the parent's directory still live, its tables are walked, each frame
 *    gains a reference and the entry is tightened. Where each one belongs is
 *    recorded rather than installed.
 * 2. The clone is loaded into CR3 and the records are installed with map_page(),
 *    which builds the intermediate page tables, sets the U/S bits and rejects
 *    conflicts exactly as it does anywhere else. Hand-rolling that here would be
 *    a second implementation of it.
 *
 * Running with a foreign directory in CR3 is safe because kernel text, the kernel
 * heap the stack is on, the page frame bitmap and reference table, the klog buffer
 * and the VGA mapping are all in the half that every directory shares.
 *
 * @param dst_pd Physical address of the directory to populate, from
 *               clone_page_directory().
 * @return E_OK, or E_NOMEM if the record array or a page table could not be
 *         allocated. On failure the references taken for entries that were not
 *         installed are dropped here; the installed ones belong to @p dst_pd, and
 *         the caller's cleanup_process_memory() reclaims them with the rest of it.
 */
int copy_user_space(uint32_t dst_pd) {
    if (dst_pd == 0) return E_INVAL;

    uint32_t page_count = count_user_pages();
    if (page_count == 0) return E_OK;

    copied_page_t *pages = (copied_page_t *)kmalloc(sizeof(copied_page_t) * page_count);
    if (!pages) {
        klog(LOG_LEVEL_ERROR, "VMM", "fork: out of memory recording the page list.");
        return E_NOMEM;
    }

    uint32_t *pd = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t n = 0;
    int result = E_OK;

    /* Pass 1: share them, parent's directory still in CR3. */
    for (uint32_t pde = 0; pde < 768; pde++) {
        if ((pd[pde] & 1) == 0) continue;

        uint32_t *pt = (uint32_t *)(RECURSIVE_PT_VADDR + (pde * PAGE_SIZE));
        for (uint32_t pte = 0; pte < 1024; pte++) {
            if ((pt[pte] & 1) == 0) continue;

            /* count_user_pages() walked the same tables a moment ago and nothing
             * can have run since - the kernel is not preemptible - but the bound
             * is cheap and the alternative is a heap overrun. */
            if (n >= page_count) break;

            uint32_t vaddr = (pde << 22) | (pte << 12);
            uint32_t frame = pt[pte] & 0xFFFFF000;
            uint32_t flags = pt[pte] & 0xFFF;

            /* The child is about to hold this frame as well. Taken before the
             * entry is touched, so there is no window in which the page is
             * reachable from two directories with one owner recorded. */
            pmm_ref_frame(frame);

            /*
             * Only a writable page becomes copy-on-write, and when it does, both
             * sides lose write access: the parent's entry is tightened here, and
             * the child's copy of the flags carries the same change.
             *
             * A read-only page is shared exactly as it stands. Marking it would
             * be worse than pointless - PAGE_COW means "read-only because it is
             * shared", and putting it on a page that is read-only on purpose
             * would turn a genuine access violation into a silent private copy.
             */
            if (flags & 0x02) {
                flags = (flags & ~0x02u) | PAGE_COW;
                pt[pte] = frame | flags;
                asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
            }

            pages[n].vaddr = vaddr;
            pages[n].frame = frame;
            pages[n].flags = flags;
            n++;
        }
        if (n >= page_count) break;
    }

    /* Pass 2: install them, clone's directory in CR3. Nothing in pass 1 can fail
     * any more - there is no allocation left in it - so result still reads E_OK
     * here and only this loop can change it. */
    uint32_t installed = 0;
    if (n > 0) {
        uint32_t parent_pd;
        uint32_t eflags;

        asm volatile("pushf; pop %0" : "=r"(eflags));
        asm volatile("cli");
        asm volatile("mov %%cr3, %0" : "=r"(parent_pd));
        asm volatile("mov %0, %%cr3" :: "r"(dst_pd) : "memory");

        for (uint32_t i = 0; i < n; i++) {
            if (map_page(pages[i].vaddr, pages[i].frame, pages[i].flags) != E_OK) {
                result = E_NOMEM;
                break;
            }
            installed++;
        }

        asm volatile("mov %0, %%cr3" :: "r"(parent_pd) : "memory");
        if (eflags & 0x200) {
            asm volatile("sti");
        }
    }

    /*
     * Give back the references taken for entries that never made it into the
     * clone. pmm_free_frame() drops one owner rather than releasing the page, so
     * this is exactly the undo of the pmm_ref_frame() above - the parent still
     * holds its own reference and keeps the frame.
     *
     * The entries the parent had tightened stay marked copy-on-write. That costs
     * nothing: with the child gone the count is back to one, and the first write
     * takes the branch in cow_handle_fault() that simply clears the bit.
     */
    for (uint32_t i = installed; i < n; i++) {
        pmm_free_frame(pages[i].frame);
    }

    kfree(pages);

    if (result == E_OK) {
        klog_int(LOG_LEVEL_DEBUG, "VMM", "fork: user pages shared", (int)n);
    }
    return result;
}

/**
 * @brief Resolves a write fault on a shared page by handing over a private copy.
 *
 * Runs in the faulting task's own address space - a page fault does not change
 * CR3 - so the recursive mapping below reaches exactly the tables that produced
 * the fault.
 *
 * @param faulting_addr Contents of CR2.
 * @return 1 if the fault was resolved and the instruction should be retried,
 *         0 if the page is not copy-on-write and this is somebody else's fault
 *         to handle, -1 if the page is shared and could not be split.
 */
int cow_handle_fault(uint32_t faulting_addr) {
    /* User space only. Nothing in the kernel half is ever marked shared, and the
     * bounds are the same ones the ELF loader and the pointer validators use. */
    if (faulting_addr < 0x400000 || faulting_addr >= 0xC0000000) return 0;

    uint32_t pd_index = faulting_addr >> 22;
    uint32_t pt_index = (faulting_addr >> 12) & 0x3FF;
    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;

    if ((pd_virt[pd_index] & 1) == 0) return 0;

    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));
    uint32_t entry = pt_virt[pt_index];

    if ((entry & 1) == 0) return 0;
    if ((entry & PAGE_COW) == 0) return 0;

    uint32_t page  = faulting_addr & 0xFFFFF000;
    uint32_t frame = entry & 0xFFFFF000;

    /* Writable again and no longer shared, whichever branch below runs. */
    uint32_t flags = (entry & 0xFFFu & ~(uint32_t)PAGE_COW) | 0x02u;

    /*
     * Sole owner: nothing to copy away from, so the page simply becomes private
     * again. This is the ordinary case rather than the exception - a fork()
     * followed by exec(), a child whose parent has already exited, and every
     * write after the first one all land here.
     */
    if (pmm_frame_refcount(frame) <= 1) {
        pt_virt[pt_index] = frame | flags;
        asm volatile("invlpg (%0)" ::"r"(page) : "memory");
        return 1;
    }

    uint32_t fresh = pmm_alloc_frame();
    if (fresh == 0xFFFFFFFF) {
        klog_hex(LOG_LEVEL_ERROR, "VMM", "Out of memory splitting a shared page at", page);
        return -1;
    }

    /*
     * The copy goes through the kernel-only window rather than reading the user
     * page directly, because with SMAP enabled a supervisor read of a
     * user-accessible page faults outright - a page fault with the present bit
     * set, which this same handler would report as a kernel fault and turn into
     * a panic. copy_user_space() paid for that lesson with a CI failure, since
     * SMAP is enabled in only one of the three test configurations.
     *
     * The bracket around it is the other half of the problem. This can be
     * reached from *inside* another copy: a syscall writing into a user buffer
     * that is still shared faults in kernel mode, and the copy below would
     * otherwise clear the fixup label that interrupted copy is relying on. Its
     * next fault would then find nowhere to go and panic.
     *
     * SMAP's AC flag needs no such care - it was pushed with EFLAGS when the
     * fault was taken and the iret restores it.
     */
    uaccess_state_t saved;
    uaccess_save_state(&saved);

    int copied;
    if (map_page(TEMP_MAP_VADDR, fresh, PAGE_KERNEL_ONLY) != E_OK) {
        copied = E_NOMEM;
    } else {
        copied = copy_from_user((void *)TEMP_MAP_VADDR, (const void *)page, PAGE_SIZE);
        unmap_page(TEMP_MAP_VADDR);
    }

    uaccess_restore_state(&saved);

    if (copied != E_OK) {
        pmm_free_frame(fresh);
        klog_hex(LOG_LEVEL_ERROR, "VMM", "A shared page could not be read while splitting it at", page);
        return -1;
    }

    /*
     * Written straight into the entry rather than through map_page(), which
     * refuses to replace a present entry pointing somewhere else - correctly, for
     * every other caller. The directory entry was walked above, so there is no
     * page table to build, and the alternative would be to unmap first and leave
     * the address briefly unmapped for no gain.
     */
    pt_virt[pt_index] = fresh | flags;
    asm volatile("invlpg (%0)" ::"r"(page) : "memory");

    /* One owner fewer for the page left behind. */
    pmm_free_frame(frame);

    return 1;
}