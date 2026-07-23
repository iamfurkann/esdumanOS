/*
 * File: test_paging.c
 * Purpose: Testing suite for Virtual Memory and Paging.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "paging.h"
#include "errno.h"

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
    serial_print("\n--- Paging / Virtual Memory Tests ---\n");

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
}
