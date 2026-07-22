/*
 * File: test_signal.c
 * Purpose: Signal handling unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"

void run_signal_tests(void) {
    printk("\n--- Signal Handling Tests ---\n");
    serial_print("\n--- Signal Handling Tests ---\n");

    // 1. Signal Handler Registration Test
    // Address 0x80000000 is already mapped for User Space by init_multitasking.
    register_user_signal(9, 0x80000000); // SIGKILL simulation handler
    KTEST_ASSERT(tasks[current_task].signal_handlers[9] == 0x80000000, 
                 "Signals: User signal handler successfully registered");

    // 2. Signal Sending (Pending Signal Queue) Test
    int my_pid = tasks[current_task].pid;
    send_user_signal(my_pid, 9);
    
    KTEST_ASSERT((tasks[current_task].pending_signals & (1 << 9)) != 0, 
                 "Signals: pending_signals bitmask updated after signal dispatch");
                 
    // Cleanup
    tasks[current_task].pending_signals &= ~(1 << 9);
    tasks[current_task].signal_handlers[9] = 0;
}
