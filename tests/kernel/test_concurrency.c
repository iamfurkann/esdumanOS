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

/**
 * @brief Evaluates the kernel's hardware-level concurrency primitives.
 *
 * This test suite focuses on the atomic operations that safeguard critical sections
 * in a multi-core or hardware-interrupt-driven environment. Since this kernel operates
 * with a non-preemptive architecture, software race conditions between threads are avoided
 * by design. Thus, testing focuses strictly on hardware lock acquisition and release.
 *
 * Expected behavior:
 * - A free hardware lock can be acquired atomically, returning its previous state.
 * - An already-held lock must reject subsequent acquisition attempts, ensuring mutual exclusion.
 * - Releasing a lock resets its state, allowing future acquisitions.
 *
 * Edge cases covered:
 * - Simultaneous acquisition simulation (locking an already locked variable).
 */
void run_concurrency_tests(void) {
    printk("\n--- Concurrency (Hardware Atomic Lock) Tests ---\n");
    serial_print("\n--- Concurrency (Hardware Atomic Lock) Tests ---\n");

    // Declare a volatile integer to act as our simulated hardware lock.
    // 'volatile' prevents compiler optimizations from caching the lock's value in a register,
    // ensuring every check physically queries memory.
    volatile int hw_lock = 0;

    // =========================================================================
    // 1. Acquire Lock Test
    // =========================================================================
    int old_val = __sync_lock_test_and_set(&hw_lock, 1);
    
    // We expect the old value to be 0 (unlocked) and the new value to be 1 (locked).
    KTEST_ASSERT(old_val == 0 && hw_lock == 1, 
        "[STRICT] CONC-01: CPU Hardware Lock (Atomic Lock) successfully acquired");

    // =========================================================================
    // 2. Lock Acquisition Attempt While Locked (Mutual Exclusion Test)
    // =========================================================================
    int try_fail = __sync_lock_test_and_set(&hw_lock, 1);
    
    // The returned old value MUST be 1, proving the second caller was denied the lock.
    KTEST_ASSERT(try_fail == 1 && hw_lock == 1, 
        "[STRICT] CONC-02: Concurrent access to locked resource hardware-REJECTED");

    // =========================================================================
    // 3. Lock Release Test
    // =========================================================================
    __sync_lock_release(&hw_lock);
    
    // Validate the lock is cleared and ready for the next critical section.
    KTEST_ASSERT(hw_lock == 0, 
        "[STRICT] CONC-03: Hardware Lock safely released");
}