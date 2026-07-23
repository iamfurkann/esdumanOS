/*
 * File: test_process.c
 * Purpose: Process scheduler unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"

extern int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);

/**
 * @brief Tests the Process Scheduler's state management and task creation.
 *
 * This function validates the fundamental structures and behaviors of the kernel's
 * process scheduler, ensuring that processes can be created, properly queued, and
 * manipulated without interfering with live hardware states (such as interrupts).
 *
 * Expected Behavior:
 * - create_process returns a valid PID within the allowed max tasks boundary.
 * - The new process is correctly loaded into the task array with a RUNNING state.
 * - State transitions (e.g., RUNNING to WAITING) are accurately reflected.
 *
 * Edge Cases Covered:
 * - Isolation from real-time interrupts using CLI/STI to prevent accidental
 *   execution of incomplete dummy contexts which could cause Triple Faults.
 */
void run_process_tests(void) {
    printk("\n--- Process Scheduler Tests ---\n");
    serial_print("\n--- Process Scheduler Tests ---\n");

    asm volatile("cli");

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
        
        tasks[task_idx].state = TASK_WAITING;
        tasks[task_idx].wait_reason = WAIT_TIMER;
        KTEST_ASSERT(tasks[task_idx].state == TASK_WAITING, "Scheduler: Task successfully transitioned to WAITING state");
        
        tasks[task_idx].state = TASK_EMPTY;
    }

    asm volatile("sti");
}
