/*
 * File: test_process.c
 * Purpose: Process scheduler unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"

extern int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);

void run_process_tests(void) {
    printk("\n--- Process Scheduler Tests ---\n");
    serial_print("\n--- Process Scheduler Tests ---\n");

    // Disable interrupts to prevent the Scheduler (Timer) from intervening,
    // executing this dummy process, and causing a Kernel Panic (Triple Fault).
    asm volatile("cli");

    // 1. Process Creation
    int pid = create_process(0x1000, 0x2000, 0x3000);
    KTEST_ASSERT(pid > 0 && pid < MAX_TASKS, "Scheduler: create_process successfully returned a valid PID");
    
    int task_idx = -1;
    for(int i = 0; i < MAX_TASKS; i++) {
        if(tasks[i].pid == pid) {
            task_idx = i;
            break;
        }
    }
    
    KTEST_ASSERT(task_idx != -1, "Scheduler: New task placed in Task Array");
    
    if (task_idx != -1) {
        KTEST_ASSERT(tasks[task_idx].state == TASK_RUNNING, 
                     "Scheduler: New task started with TASK_RUNNING state");
        
        // 2. State Transition
        tasks[task_idx].state = TASK_WAITING;
        tasks[task_idx].wait_reason = WAIT_TIMER;
        KTEST_ASSERT(tasks[task_idx].state == TASK_WAITING, "Scheduler: Task successfully transitioned to WAITING state");
        
        // Cleanup (Prevent memory leak and scheduler crash)
        tasks[task_idx].state = TASK_EMPTY;
    }

    // Re-enable interrupts after we are done.
    asm volatile("sti");
}
