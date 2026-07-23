/*
 * File: test_stress.c
 * Purpose: System limit, boundary, and stress tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "process.h"
#include "fs.h"

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Stress tests and boundary assessments for critical kernel subsystems.
 *
 * This file pushes the kernel's resource management and limits to their maximum
 * capacity to ensure graceful failure (error handling) rather than catastrophic failure (panics).
 *
 * Expected behavior:
 * - Exhausting available file descriptors returns an error rather than overflowing FD tables.
 * - Exceeding maximum filename lengths in the VFS results in safe truncation or rejection.
 * - Heavy recursive/nested directory creation does not corrupt the filesystem topology.
 * - Exhausting the process table via continuous forking/loading correctly rejects new tasks.
 *
 * Edge cases covered:
 * - Mathematical precision of limit boundaries (e.g., exact 16-FD exhaustion).
 * - Massive string allocations intended to force buffer overflows in VFS.
 * - Out-of-memory (OOM) / Out-of-tasks limit testing in a non-preempted state.
 */
void run_stress_tests(void) {
    printk("\n--- Stress and Boundary Tests ---\n");
    serial_print("\n--- Stress and Boundary Tests ---\n");
    
    // =========================================================================
    // 1. FD EXHAUSTION TEST
    // =========================================================================
    int u_fds = 0x500A00;
    int pipes_created = 0;
    int res = 0;
    
    for (int i = 0; i < 50; i++) {
        res = ktest_syscall(SYSCALL_PIPE, u_fds, 0, 0);
        if (res == 0) pipes_created++;
        else break; // Stop as soon as the kernel correctly rejects a request.
    }
    
    KTEST_ASSERT(res < 0 && pipes_created == 6, 
                 "[STRICT] FD Exhaustion: Exactly 6 pipes (12 FDs) opened and 7th attempt successfully REJECTED");

    // =========================================================================
    // 2. VFS LONG NAME (Buffer Overflow) TEST
    // =========================================================================
    char *long_name = (char *)0x500B00;
    for(int i=0; i<250; i++) long_name[i] = 'A';
    long_name[250] = '\0';
    
    // Attempt to create a file using this 250-character name.
    // The VFS layer must either truncate it cleanly or reject it with an error.
    // The critical success metric is that the kernel DOES NOT trigger a buffer overflow and crash.
    int vfs_res = ktest_syscall(8 /* CREATE_FILE */, (int)long_name, (int)"", 0);
    KTEST_ASSERT(vfs_res < 0 || vfs_res == 0, 
                 "[STRICT] VFS Long Name: System DID NOT CRASH on giant filename");

    // =========================================================================
    // 3. DEEP DIRECTORY NESTING STRESS TEST
    // =========================================================================
    extern int fs_mkdir(const char *name, uint8_t parent_id);
    extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
    extern int fs_delete(const char *name, uint8_t parent_id);
    extern disk_file_entry_t dir_table[];

    int current_parent = 0; // Root directory is ID 0.
    int depth = 0;
    char dir_name[10] = "d0";
    
    for(int i = 1; i <= 10; i++) {
        dir_name[1] = '0' + (i % 10);
        int m_res = fs_mkdir(dir_name, current_parent);
        if (m_res == 0) { // E_OK = 0
            int new_idx = fs_get_entry_idx(dir_name, current_parent);
            if (new_idx != -1) {
                current_parent = dir_table[new_idx].entry_id;
                depth++;
            } else break;
        } else break;
    }
    // Validate we reached a depth of 10 without exhausting stack space or VFS node tables.
    KTEST_ASSERT(depth == 10, "[STRICT] VFS Stress: 10-Level Deep (Nested) Directory successfully created");

    // =========================================================================
    // 4. PROCESS/TASK LIMIT EXHAUSTION STRESS TEST
    // =========================================================================
    extern int load_and_exec_elf(const char *filename, uint8_t parent_id);
    extern process_t tasks[];
    
    asm volatile("cli");
    
    int task_limit_hit = 0;
    int loaded_count = 0;
    
    for(int i = 0; i < 30; i++) {
        // "init.elf" serves as a dummy payload that is known to exist on the test filesystem.
        int pid = load_and_exec_elf("init.elf", 0);
        if (pid < 0) {
            // Rejection implies the task limit has been correctly enforced by the kernel.
            task_limit_hit = 1;
            break;
        } else {
            loaded_count++;
        }
    }
    
    // Validate the kernel hit the ceiling and threw an error instead of corrupting the task array.
    KTEST_ASSERT(task_limit_hit == 1, "[STRICT] Process Table: Rejected when maximum task (MAX_TASKS) limit reached");
    
    extern void kfree(void *);
    for(int i = 0; i < 16; i++) { // MAX_TASKS is usually 16 in this build configuration.
        // Identify valid dummy tasks (not empty, not our current task, and not kernel init).
        if (tasks[i].state != TASK_EMPTY && i != current_task && tasks[i].pid > 1) {
            if (tasks[i].page_directory) {
                // Free the slot by marking the state as empty. 
                // In a true environment we'd free the heap here too, but for testing
                // we tolerate minor unmapped page leaks as long as the OS doesn't OOM.
                tasks[i].state = TASK_EMPTY;
            }
        }
    }
    
    asm volatile("sti");
}