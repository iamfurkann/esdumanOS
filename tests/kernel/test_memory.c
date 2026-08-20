/*
 * File: test_memory.c
 * Purpose: Deep testing suite for kernel memory allocator (KHeap).
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "types.h"
#include "kheap.h"
#include "pmm.h"
/**
 * @brief Executes a comprehensive testing suite for the kernel memory allocator (KHeap).
 *
 * This function validates the core mechanisms of the kernel's dynamic memory allocation system.
 * It tests fundamental operations (allocation, isolation), edge cases (zero-byte requests, Out-Of-Memory conditions),
 * block coalescing upon freeing, resilience against memory leaks through stress testing, and reallocation capabilities.
 * 
 * @expected Success conditions involve valid pointer returns for standard allocations, appropriate failure handling (NULL) 
 *           for edge cases, preservation of block boundaries, and successful coalescing of adjacent free blocks.
 */
void run_memory_tests(void) {
    printk("\n--- Memory (KHeap) Deep Tests ---\n");
    
    void *ptr1 = kmalloc(128);
    void *ptr2 = kmalloc(256);
    KTEST_ASSERT(ptr1 != 0 && ptr2 != 0, "Basic Allocation: Successful");
    // Ensure that the allocated blocks do not overlap, validating memory isolation.
    KTEST_ASSERT((uint32_t)ptr2 >= ((uint32_t)ptr1 + 128), "Basic Allocation: Memory Isolation (No overlapping)");

    char *str1 = (char *)ptr1;
    char *str2 = (char *)ptr2;
    for(int i = 0; i < 128; i++) str1[i] = 'A';
    for(int i = 0; i < 256; i++) str2[i] = 'B';
    // Verify that the boundaries of the first block remain intact and uncorrupted.
    KTEST_ASSERT(str1[0] == 'A' && str1[127] == 'A', "Write Test: Ptr1 block boundaries preserved");
    // Verify that the boundaries of the second block are equally preserved.
    KTEST_ASSERT(str2[0] == 'B' && str2[255] == 'B', "Write Test: Ptr2 block boundaries preserved");

    void *zero_ptr = kmalloc(0);
    KTEST_ASSERT(zero_ptr == 0, "Edge Case: kmalloc(0) returns NULL (Safe reject)");
    
    // Request a huge amount of memory (3 GB) to trigger Out-Of-Memory (OOM) protection mechanisms.
    void *huge_ptr = kmalloc(1024 * 1024 * 1024 * 3U); // 3 GB
    KTEST_ASSERT(huge_ptr == 0, "Edge Case: Huge kmalloc returns NULL (OOM Protection)");

    kfree(ptr1);
    kfree(ptr2);
    // Since ptr1 and ptr2 are adjacent and now freed, they should be merged by the allocator.
    // Requesting a block size equivalent to their combined size should succeed.
    void *ptr3 = kmalloc(384); 
    KTEST_ASSERT(ptr3 != 0, "Kfree and Coalescing: Freed blocks successfully coalesced");
    kfree(ptr3);

    void *pointers[100];
    int leak_test_success = 1;
    // Perform numerous consecutive allocations.
    for (int i = 0; i < 100; i++) {
        pointers[i] = kmalloc(32);
        if (!pointers[i]) leak_test_success = 0;
    }
    KTEST_ASSERT(leak_test_success == 1, "Stress Test: 100 consecutive allocations successful");
    
    // Create fragmentation by freeing alternating blocks (even indices).
    for (int i = 0; i < 100; i += 2) { kfree(pointers[i]); }
    // Free the remaining blocks (odd indices) to trigger widespread coalescing.
    for (int i = 1; i < 100; i += 2) { kfree(pointers[i]); }
    
    // Attempt a large allocation requiring a contiguous chunk equal to the total previously fragmented space.
    // If fragmentation was not properly resolved via coalescing, this allocation will fail.
    void *huge_coalesce_ptr = kmalloc(3200);
    KTEST_ASSERT(huge_coalesce_ptr != 0, "Stress Test: 100 chunks successfully freed and coalesced (No leak)");
    if(huge_coalesce_ptr) kfree(huge_coalesce_ptr);

    void *r_ptr = kmalloc(10);
    KTEST_ASSERT(r_ptr != 0, "Realloc: Initial allocation");
    // Expand the allocated block to a significantly larger size, which may force a relocation.
    void *r_ptr_new = krealloc(r_ptr, 1024);
    KTEST_ASSERT(r_ptr_new != 0, "Realloc: Successfully moved to a larger area");
    // Confirm that the newly allocated block satisfies the size requirement.
    KTEST_ASSERT(kmalloc_size(r_ptr_new) >= 1024, "Realloc: New size verified");
    kfree(r_ptr_new);

    /*
     * A growth that fails has to leave nothing behind.
     *
     * heap_grow() takes frames one at a time and maps each one, and until this
     * release the result of the mapping was dropped: a failure advanced the heap
     * end over an address with nothing behind it, and the block header was then
     * written into the hole - a kernel page fault, surfacing at whatever
     * allocated next rather than here.
     *
     * The mapping step cannot be made to fail on demand from out here; there is
     * no injection point, and inventing one would be a larger change than the
     * fix. What is measurable is the rest of the contract, and it is what a
     * broken rollback breaks: a rejected allocation must cost no physical memory,
     * and the heap must still work afterwards. A rollback that frees nothing
     * fails the first, and one that leaves the heap end past the mapped region
     * fails the second.
     */
    uint32_t frames_before = pmm_get_free_memory();
    for (int i = 0; i < 4; i++) {
        KTEST_ASSERT(kmalloc(1024 * 1024 * 1024 * 3U) == 0,
                     "Failed Growth: a request beyond physical memory is rejected");
    }
    KTEST_ASSERT(pmm_get_free_memory() == frames_before,
                 "[STRICT] Failed Growth: rejected allocations consume no physical memory");

    void *after_fail = kmalloc(512);
    KTEST_ASSERT(after_fail != 0, "[STRICT] Failed Growth: the heap still allocates afterwards");
    if (after_fail) {
        char *probe = (char *)after_fail;
        for (int i = 0; i < 512; i++) probe[i] = 'C';
        KTEST_ASSERT(probe[0] == 'C' && probe[511] == 'C',
                     "[STRICT] Failed Growth: and the memory it returns is really mapped");
        kfree(after_fail);
    }

    /*
     * Grow and shrink repeatedly.
     *
     * The shrink path in kfree() is where the heap's one discontinuity comes
     * from: a tail block whose remainder is too small to hold a header is dropped
     * from the list, leaving a few dozen bytes between its predecessor and
     * whatever is appended next. Both merge sites now check that two neighbours
     * in the list are neighbours in memory before joining them; this drives the
     * path that produces such a pair, and checks that the frames come back either
     * way.
     */
    uint32_t cycle_before = pmm_get_free_memory();
    int cycles_ok = 1;
    for (int i = 0; i < 16; i++) {
        void *big = kmalloc(8192);
        if (!big) { cycles_ok = 0; break; }

        char *fill = (char *)big;
        for (int j = 0; j < 8192; j += 512) fill[j] = (char)i;
        for (int j = 0; j < 8192; j += 512) {
            if (fill[j] != (char)i) cycles_ok = 0;
        }
        kfree(big);
    }
    KTEST_ASSERT(cycles_ok == 1, "Grow/Shrink: every cycle allocated and read back intact");
    KTEST_ASSERT(pmm_get_free_memory() == cycle_before,
                 "[STRICT] Grow/Shrink: repeated growth and shrinking leaks no frames");
}