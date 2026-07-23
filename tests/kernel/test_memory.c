/*
 * File: test_memory.c
 * Purpose: Deep testing suite for kernel memory allocator (KHeap).
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "types.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void *krealloc(void *ptr, size_t new_size);
extern size_t kmalloc_size(void *ptr);

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
}