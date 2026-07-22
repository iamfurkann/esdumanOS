/*
 * File: test_adversarial.c
 * Purpose: Adversarial and malicious input security tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"

// Import the Kernel's internal security firewall function externally
extern int validate_user_pointer(const void *ptr, size_t size);

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_adversarial_tests(void) {
    printk("\n--- Adversarial Input (Adversarial/Security) Tests ---\n");
    serial_print("\n--- Adversarial Input (Adversarial/Security) Tests ---\n");

    int safe_content = 0x500C00;

    // =========================================================================
    // NEGATIVE (MUST BE REJECTED) OUT-OF-BOUNDS TESTS
    // (These prove that Syscall rejects external data)
    // =========================================================================
    int res_null = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0x0, safe_content, 0);
    KTEST_ASSERT(res_null < 0, "[STRICT] Syscall: NULL pointer argument strictly REJECTED");

    int res_kernel_read = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0x3FFFFF, safe_content, 0);
    KTEST_ASSERT(res_kernel_read < 0, "[STRICT] Syscall: Kernel memory (0x3FFFFF) access REJECTED");

    int res_high_mem = ktest_syscall(8 /* SYSCALL_CREATE_FILE */, 0xC0000000, safe_content, 0);
    KTEST_ASSERT(res_high_mem < 0, "[STRICT] Syscall: Unmapped High Memory address (0xC0000000) REJECTED");

    // =========================================================================
    // POSITIVE (MUST BE ACCEPTED) EXACT BOUNDARY TESTS (Boundary Value)
    // To avoid Syscall side-effects (Closed FD, 0 byte rejection, etc.) 
    // and avoid Page Faults (CR2), we test the security function DIRECTLY!
    // =========================================================================

    // 4. Exact Lower Bound
    // 0x400000 is the first byte of User Space. Firewall should return 1 (Valid) here.
    int res_lower_bound = validate_user_pointer((const void *)0x400000, 1);
    KTEST_ASSERT(res_lower_bound == 1, "[STRICT] Security: User Space Lower Bound (0x400000) successfully ACCEPTED");

    // 5. Exact Upper Bound
    // 0xBFFFFFFF is within bounds! Firewall should return 1 (Valid) here.
    // However, it's rejected if not mapped in Page Table, so we temporarily map it for the test.
    extern int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
    extern uint32_t pmm_alloc_frame(void);
    extern void pmm_free_frame(uint32_t addr);
    extern void unmap_page(uint32_t virtual_addr);
    
    uint32_t dummy_frame = pmm_alloc_frame();
    map_page(0xBFFFF000, dummy_frame, 7);

    int res_upper_bound = validate_user_pointer((const void *)0xBFFFFFFF, 1);
    KTEST_ASSERT(res_upper_bound == 1, "[STRICT] Security: User Space Upper Bound (0xBFFFFFFF) successfully ACCEPTED");

    unmap_page(0xBFFFF000);
    pmm_free_frame(dummy_frame);
}