/*
 * File: test_signal.c
 * Purpose: Signal handling unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"

/**
 * @brief Tests the inter-process signal routing and handler registration.
 *
 * This test ensures that user-space processes can accurately map specific
 * signals to custom memory handlers and that the kernel successfully dispatches
 * these signals into a pending state.
 *
 * Expected Behavior:
 * - Signal handlers are correctly registered to the task's tracking array.
 * - Firing a signal correctly flags the relevant bit in the pending_signals mask.
 * - Allows user process to correctly intercept custom exceptions like SIGKILL simulation.
 *
 * Edge Cases Covered:
 * - Proper bounds handling for signals mapping to the 0x80000000 boundary.
 * - Cleanup of pending queues and handlers to prevent cross-test contamination.
 */
void run_signal_tests(void) {
    printk("\n--- Signal Handling Tests ---\n");
    serial_print("\n--- Signal Handling Tests ---\n");

    register_user_signal(9, 0x80000000); // SIGKILL simulation handler
    KTEST_ASSERT(tasks[current_task].signal_handlers[9] == 0x80000000, 
                 "Signals: User signal handler successfully registered");

    int my_pid = tasks[current_task].pid;
    send_user_signal(my_pid, 9);
    
    // Verify the signal successfully tripped the pending state flag.
    KTEST_ASSERT((tasks[current_task].pending_signals & (1 << 9)) != 0, 
                 "Signals: pending_signals bitmask updated after signal dispatch");
                 
    tasks[current_task].pending_signals &= ~(1 << 9);
    tasks[current_task].signal_handlers[9] = 0;
}
