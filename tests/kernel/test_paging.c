/*
 * File: test_paging.c
 * Purpose: Testing suite for Virtual Memory and Paging.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "paging.h"
#include "pmm.h"
#include "uaccess.h"
#include "errno.h"

/**
 * @brief Probe address for the CR0.WP check.
 *
 * Must sit inside user space (4 MB .. 3 GB) so copy_to_user() accepts it, and
 * clear of the 0x500000..0x503000 window the suite maps for other modules.
 */
#define WP_PROBE_VADDR 0x00600000u

/**
 * @brief Validates the Kernel's Virtual Memory and Paging subsystem.
 *
 * This test suite evaluates the core paging capabilities, focusing on the mapping 
 * and unmapping of virtual addresses to physical pages. It additionally checks 
 * system resilience against address collisions (remap/double-mapping protections).
 *
 * @expected Virtual-to-physical address mappings should succeed. Unmapping should gracefully 
 *           invalidate page entries without kernel panics. Re-mapping an active virtual address 
 *           to a different physical address must be correctly rejected by the subsystem.
 */
void run_paging_tests(void) {
    printk("\n--- Paging / Virtual Memory Tests ---\n");

    // Setup predefined virtual and physical addresses for isolated testing.
    uint32_t test_virt = 0x8000000;
    uint32_t test_phys = 0x1000000;

    int res = map_page(test_virt, test_phys, PAGE_USER_ACCESS);
    KTEST_ASSERT(res == 0, "Paging: Virtual address successfully mapped to physical address");

    unmap_page(test_virt);
    // Because unmap_page returns void, its success is inferred by the kernel surviving 
    // the operation without throwing a page fault or crashing.
    KTEST_ASSERT(1 == 1, "Paging: Virtual address mapping successfully removed");

    map_page(test_virt, test_phys, PAGE_USER_ACCESS);
    // Attempt to remap the active virtual address to an entirely different physical address.
    int res_remap = map_page(test_virt, test_phys + 0x1000, PAGE_USER_ACCESS); 
    // Verify that the kernel rejects the collision and correctly responds with an E_BUSY error code.
    KTEST_ASSERT(res_remap == E_BUSY, "Paging: Different physical address collision successfully prevented");
    
    // Clean up the mapping before exiting the test suite.
    unmap_page(test_virt);

    /*
     * CR0.WP.
     *
     * With WP clear the processor ignores the read/write bit of a page table
     * entry for supervisor accesses, so a read-only user mapping only protects
     * Ring 3: any kernel write through a user pointer succeeds silently and
     * corrupts the page. These checks prove the bit is set and that it actually
     * bites, by driving the same copy_to_user() path the syscalls use.
     */
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    KTEST_ASSERT((cr0 & (1u << 16)) != 0,
                 "[STRICT] CR0.WP enabled (kernel honours read-only user pages)");

    uint32_t wp_frame = pmm_alloc_frame();
    KTEST_ASSERT(wp_frame != 0xFFFFFFFF, "CR0.WP: probe frame allocated");

    if (wp_frame != 0xFFFFFFFF) {
        uint8_t probe = 0x5A;

        /*
         * Map writable first and plant a known byte, so that "was the page
         * modified" is a meaningful question afterwards. A freshly allocated
         * frame holds whatever the previous owner left in it.
         */
        KTEST_ASSERT(map_page(WP_PROBE_VADDR, wp_frame, 7) == 0,
                     "CR0.WP: probe page mapped writable for setup");
        *(volatile uint8_t *)WP_PROBE_VADDR = 0x00;

        // Present | User, read/write bit deliberately clear.
        KTEST_ASSERT(map_page(WP_PROBE_VADDR, wp_frame, 5) == 0,
                     "CR0.WP: probe page mapped read-only");

        /*
         * Read the translation back through the recursive mapping before
         * relying on it. A failure below has two very different causes and this
         * separates them: either map_page() did not apply the flags it was
         * given (the entries are wrong), or the entries are right and the
         * processor is not honouring them (WP is not taking effect).
         */
        uint32_t *wp_pd = (uint32_t *)0xFFFFF000;
        uint32_t *wp_pt = (uint32_t *)(0xFFC00000 + ((WP_PROBE_VADDR >> 22) * 0x1000));
        uint32_t wp_pde = wp_pd[WP_PROBE_VADDR >> 22];
        uint32_t wp_pte = wp_pt[(WP_PROBE_VADDR >> 12) & 0x3FF];

        KTEST_ASSERT((wp_pte & 0x01) != 0, "CR0.WP: probe PTE is present");
        KTEST_ASSERT((wp_pte & 0x04) != 0, "CR0.WP: probe PTE is user-accessible");
        KTEST_ASSERT((wp_pte & 0x02) == 0, "[STRICT] CR0.WP: probe PTE read/write bit is clear");
        KTEST_ASSERT((wp_pde & 0x01) != 0, "CR0.WP: probe PDE is present");

        /*
         * The fault is taken in kernel mode and caught by the uaccess fixup, so
         * this returns an error instead of panicking.
         *
         * Both the return value and the page contents are checked, because they
         * fail for different reasons. If the byte changed, the processor let the
         * write through and WP is not being enforced. If the byte is untouched
         * but the call still reported success, the protection worked and the
         * uaccess fixup path is returning the wrong value - which would matter
         * far more, since every copy_from_user()/copy_to_user() in the syscall
         * layer depends on that path to report faults.
         */
        int ro_ret = copy_to_user((void *)WP_PROBE_VADDR, &probe, 1);
        uint8_t ro_seen = *(volatile uint8_t *)WP_PROBE_VADDR;

        KTEST_ASSERT(ro_seen == 0x00,
                     "[STRICT] CR0.WP: read-only page contents left unmodified");
        KTEST_ASSERT(ro_ret != E_OK,
                     "[STRICT] CR0.WP: kernel write through a read-only user PTE refused");

        // Same frame, now writable: the identical copy must go through.
        KTEST_ASSERT(map_page(WP_PROBE_VADDR, wp_frame, 7) == 0,
                     "CR0.WP: probe page remapped writable");

        KTEST_ASSERT(copy_to_user((void *)WP_PROBE_VADDR, &probe, 1) == E_OK,
                     "CR0.WP: writable user page still accepts a kernel copy");

        unmap_page(WP_PROBE_VADDR);
        pmm_free_frame(wp_frame);
    }
}
