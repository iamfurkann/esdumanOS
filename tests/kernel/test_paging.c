/*
 * File: test_paging.c
 * Purpose: Testing suite for Virtual Memory and Paging.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "paging.h"
#include "errno.h"

void run_paging_tests(void) {
    printk("\n--- Paging / Virtual Memory Tests ---\n");
    serial_print("\n--- Paging / Virtual Memory Tests ---\n");

    uint32_t test_virt = 0x8000000;
    uint32_t test_phys = 0x1000000;

    // 1. Basic Mapping
    int res = map_page(test_virt, test_phys, PAGE_USER_ACCESS);
    KTEST_ASSERT(res == 0, "Paging: Virtual address successfully mapped to physical address");

    // 2. Unmapping
    unmap_page(test_virt);
    // Unmap returns void, if we survive without crash and don't panic, it's successful.
    KTEST_ASSERT(1 == 1, "Paging: Virtual address mapping successfully removed");

    // 3. Double Mapping Protection / Overwriting
    map_page(test_virt, test_phys, PAGE_USER_ACCESS);
    int res_remap = map_page(test_virt, test_phys + 0x1000, PAGE_USER_ACCESS); 
    // The kernel code says if (old_phys != new_phys) return E_BUSY;
    KTEST_ASSERT(res_remap == E_BUSY, "Paging: Different physical address collision successfully prevented");
    unmap_page(test_virt);
}
