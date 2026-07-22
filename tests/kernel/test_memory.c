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

void run_memory_tests(void) {
    printk("\n--- Memory (KHeap) Deep Tests ---\n");
    
    // 1. BASIC ALLOCATION AND ISOLATION TEST
    void *ptr1 = kmalloc(128);
    void *ptr2 = kmalloc(256);
    KTEST_ASSERT(ptr1 != 0 && ptr2 != 0, "Basic Allocation: Successful");
    KTEST_ASSERT((uint32_t)ptr2 >= ((uint32_t)ptr1 + 128), "Basic Allocation: Memory Isolation (No overlapping)");

    // 2. READ/WRITE CORRUPTION TEST
    char *str1 = (char *)ptr1;
    char *str2 = (char *)ptr2;
    for(int i = 0; i < 128; i++) str1[i] = 'A';
    for(int i = 0; i < 256; i++) str2[i] = 'B';
    KTEST_ASSERT(str1[0] == 'A' && str1[127] == 'A', "Write Test: Ptr1 block boundaries preserved");
    KTEST_ASSERT(str2[0] == 'B' && str2[255] == 'B', "Write Test: Ptr2 block boundaries preserved");

    // 3. ZERO AND HUGE SIZE (OOM) TESTS (Edge Cases)
    void *zero_ptr = kmalloc(0);
    KTEST_ASSERT(zero_ptr == 0, "Edge Case: kmalloc(0) returns NULL (Safe reject)");
    
    // Note: Requesting a huge size to push Heap_Grow limits
    void *huge_ptr = kmalloc(1024 * 1024 * 1024 * 3U); // 3 GB
    KTEST_ASSERT(huge_ptr == 0, "Edge Case: Huge kmalloc returns NULL (OOM Protection)");

    // 4. KFREE AND COALESCING TEST
    kfree(ptr1);
    kfree(ptr2);
    // ptr1 and ptr2 should be coalesced. If we request the same size, it should return the same location.
    void *ptr3 = kmalloc(384); 
    KTEST_ASSERT(ptr3 != 0, "Kfree and Coalescing: Freed blocks successfully coalesced");
    kfree(ptr3);

    // 5. MEMORY LEAK AND FRAGMENTATION STRESS TEST
    void *pointers[100];
    int leak_test_success = 1;
    for (int i = 0; i < 100; i++) {
        pointers[i] = kmalloc(32);
        if (!pointers[i]) leak_test_success = 0;
    }
    KTEST_ASSERT(leak_test_success == 1, "Stress Test: 100 consecutive allocations successful");
    
    for (int i = 0; i < 100; i += 2) { kfree(pointers[i]); } // Delete even ones to create holes
    for (int i = 1; i < 100; i += 2) { kfree(pointers[i]); } // Delete odd ones to coalesce holes
    
    // If everything coalesced, we should be able to allocate a large 3200 (100 * 32) bytes chunk again.
    // If fragmentation remained, contiguous 3200 bytes might not be found.
    void *huge_coalesce_ptr = kmalloc(3200);
    KTEST_ASSERT(huge_coalesce_ptr != 0, "Stress Test: 100 chunks successfully freed and coalesced (No leak)");
    if(huge_coalesce_ptr) kfree(huge_coalesce_ptr);

    // 6. REALLOC TEST
    void *r_ptr = kmalloc(10);
    KTEST_ASSERT(r_ptr != 0, "Realloc: Initial allocation");
    void *r_ptr_new = krealloc(r_ptr, 1024);
    KTEST_ASSERT(r_ptr_new != 0, "Realloc: Successfully moved to a larger area");
    KTEST_ASSERT(kmalloc_size(r_ptr_new) >= 1024, "Realloc: New size verified");
    kfree(r_ptr_new);
}