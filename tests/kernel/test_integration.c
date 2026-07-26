/*
 * File: test_integration.c
 * Purpose: Cross-component integration tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "fs.h"
#include "process.h"
#include "elf.h"
#include "libft.h"
#include "security.h"
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Executes integration tests across multiple kernel components.
 *
 * This test suite verifies that separate, independently tested kernel subsystems 
 * (like the Virtual File System, Process Loader, and ATA Disk Driver) interact 
 * correctly when chained together in real-world workflows.
 *
 * Expected behavior:
 * - File creation, deletion, and recreation sequences maintain VFS/disk consistency.
 * - The Process Loader (ELF) can successfully parse and map binary files pulled 
 *   directly from the VFS.
 *
 * Edge cases covered:
 * - Executing VFS logic while the scheduler is explicitly paused.
 * - Index fragmentation issues when recreating files with identical names.
 */
void run_integration_tests(void) {
    printk("\n--- Cross-Component Integration Tests ---\n");
    serial_print("\n--- Cross-Component Integration Tests ---\n");

    asm volatile("sti");
multitasking_enabled = 0; 

    int old_sec_level = current_sec_level;
    current_sec_level = 0; 

    fs_delete("int_test.txt", 0);
    fs_delete("dummy.elf", 0);

    char u_file[] = "int_test.txt";
    char u_data[] = "Integration Test Data";
    char u_elf_name[] = "dummy.elf";
    uint8_t u_elf_data[64];

    // =========================================================================
    // VFS-INT: Disk Writing, Deletion, and Recreation Integration
    // =========================================================================
    
    int res1 = fs_create_file(u_file, (uint8_t *)u_data, ft_strlen(u_data), 0); 
    KTEST_ASSERT(res1 >= 0, "[STRICT] VFS-INT: File written to disk successfully for the first time");

    int res2 = fs_delete(u_file, 0); 
    KTEST_ASSERT(res2 >= 0, "[STRICT] VFS-INT: File successfully deleted (Sectors freed)");

    int res3 = fs_create_file(u_file, (uint8_t *)u_data, ft_strlen(u_data), 0);
    KTEST_ASSERT(res3 >= 0, "[STRICT] VFS-INT: New file written with the same name (VFS Index transformation successful)");

    // =========================================================================
    // PROC-INT: Process Loader (ELF) and VFS Integration
    // =========================================================================
    
    for (int i = 0; i < 64; i++) u_elf_data[i] = 0;
    u_elf_data[0] = 0x7F; u_elf_data[1] = 'E'; u_elf_data[2] = 'L'; u_elf_data[3] = 'F'; // ELF Magic Bytes
    u_elf_data[4] = 1;   // 32-bit architecture flag
    u_elf_data[16] = 2;  // Executable file type
    u_elf_data[18] = 3;  // Target machine (x86)
    
    int res4 = fs_create_file(u_elf_name, u_elf_data, 64, 0);
    KTEST_ASSERT(res4 >= 0, "[STRICT] PROC-INT: Valid minimal ELF written to disk for testing");

    int p_idx = load_and_exec_elf(u_elf_name, 0);
    KTEST_ASSERT(p_idx >= 0, "[STRICT] PROC-INT: load_and_exec_elf read from disk and returned valid Array Index");
    
    fs_delete(u_file, 0);
    fs_delete(u_elf_name, 0);
if (p_idx >= 0) { 
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == p_idx) {
                p->state = TASK_EMPTY;
                break;
            }
        }
    }

    current_sec_level = old_sec_level;
    multitasking_enabled = 1; 
}