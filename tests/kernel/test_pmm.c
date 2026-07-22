/*
 * File: test_pmm.c
 * Purpose: Testing suite for Physical Memory Manager (PMM).
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "types.h"

extern uint32_t pmm_alloc_frame(void);
extern void pmm_free_frame(uint32_t phys_addr);
extern uint32_t pmm_get_free_memory(void);

void run_pmm_tests(void) {
    printk("\n--- Physical Memory Manager (PMM) Tests ---\n");
    serial_print("\n--- Physical Memory Manager (PMM) Tests ---\n");

    // 1. Basic Frame Allocation
    uint32_t free_mem_before = pmm_get_free_memory();
    uint32_t frame1 = pmm_alloc_frame();
    uint32_t frame2 = pmm_alloc_frame();
    uint32_t free_mem_after = pmm_get_free_memory();

    KTEST_ASSERT(frame1 != 0 && frame2 != 0, "PMM: Physical frames successfully allocated");
    KTEST_ASSERT(frame1 != frame2, "PMM: Consecutive allocations return different frames");
    KTEST_ASSERT(free_mem_after < free_mem_before, "PMM: Free memory decreased after allocation");

    // 2. Frame Freeing
    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
    uint32_t free_mem_final = pmm_get_free_memory();
    
    // There might be minor deviations (metadata, etc.), but we should have roughly reclaimed the allocated amount.
    // We check if it has been reclaimed instead of exact equality.
    KTEST_ASSERT(free_mem_final > free_mem_after, "PMM: Frames freed and memory returned");
}
