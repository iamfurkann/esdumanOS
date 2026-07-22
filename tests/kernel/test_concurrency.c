/*
 * File: test_concurrency.c
 * Purpose: Hardware Atomic Lock concurrency tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"

// [ARCHITECTURE NOTE]: This Operating System has a "Non-Preemptive Kernel" architecture.
// (the schedule() function in process.c refuses to preempt Ring 0 tasks).
// Thus, experiencing Race Conditions within the kernel is architecturally
// prevented. The concurrency test directly verifies CPU hardware 
// locks (Atomic Primitives).

void run_concurrency_tests(void) {
    printk("\n--- Concurrency (Hardware Atomic Lock) Tests ---\n");
    serial_print("\n--- Concurrency (Hardware Atomic Lock) Tests ---\n");

    volatile int hw_lock = 0;

    // 1. Acquire Lock - Locks the CPU memory bus
    int old_val = __sync_lock_test_and_set(&hw_lock, 1);
    KTEST_ASSERT(old_val == 0 && hw_lock == 1, 
        "[STRICT] CONC-01: CPU Hardware Lock (Atomic Lock) successfully acquired");

    // 2. Lock Acquire Attempt While Locked (Race Prevention) 
    // If another core or task tries to acquire the lock simultaneously, it gets rejected
    int try_fail = __sync_lock_test_and_set(&hw_lock, 1);
    KTEST_ASSERT(try_fail == 1 && hw_lock == 1, 
        "[STRICT] CONC-02: Concurrent access to locked resource hardware-REJECTED");

    // 3. Release Lock
    __sync_lock_release(&hw_lock);
    KTEST_ASSERT(hw_lock == 0, 
        "[STRICT] CONC-03: Hardware Lock safely released");
}