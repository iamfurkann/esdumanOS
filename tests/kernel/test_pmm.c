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

/**
 * @brief Tests the kernel's Physical Memory Manager (PMM).
 *
 * This function validates the fundamental mechanisms of the PMM, specifically 
 * the allocation and deallocation of physical page frames. It ensures that 
 * requests return unique valid frames and that freed memory appropriately reflects 
 * in the system's tracking of available physical memory.
 *
 * @expected Frames should be successfully allocated with unique physical addresses. 
 *           The tracked free memory count should decrease upon allocation and increase 
 *           back toward its original value after frames are freed.
 */
void run_pmm_tests(void) {
    printk("\n--- Physical Memory Manager (PMM) Tests ---\n");
    serial_print("\n--- Physical Memory Manager (PMM) Tests ---\n");

    uint32_t free_mem_before = pmm_get_free_memory();
    uint32_t frame1 = pmm_alloc_frame();
    uint32_t frame2 = pmm_alloc_frame();
    
    // Measure free memory again to evaluate the depletion after allocations.
    uint32_t free_mem_after = pmm_get_free_memory();

    // Verify that both requests returned valid (non-zero) physical frames.
    KTEST_ASSERT(frame1 != 0 && frame2 != 0, "PMM: Physical frames successfully allocated");
    // Verify that the allocations are unique and do not overlap the exact same page.
    KTEST_ASSERT(frame1 != frame2, "PMM: Consecutive allocations return different frames");
    // Ensure the system's tracked free memory count has correctly decreased.
    KTEST_ASSERT(free_mem_after < free_mem_before, "PMM: Free memory decreased after allocation");

    pmm_free_frame(frame1);
    pmm_free_frame(frame2);
    
    // Retrieve the final count of available physical memory.
    uint32_t free_mem_final = pmm_get_free_memory();
    
    // Verify that the available memory increased after the frames were reclaimed.
    // We check if it has been reclaimed instead of exact equality, accounting for potential 
    // internal kernel metadata allocations that might have occurred concurrently.
    KTEST_ASSERT(free_mem_final > free_mem_after, "PMM: Frames freed and memory returned");
}
