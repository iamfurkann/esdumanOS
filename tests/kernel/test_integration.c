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

extern void ft_strcpy(char *dest, const char *src);
extern int load_and_exec_elf(const char *name, uint8_t parent_id);
extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
extern int fs_delete(const char *name, uint8_t parent_id); 
extern int ft_strlen(const char *s);
extern int current_sec_level;
extern disk_file_entry_t dir_table[];

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_integration_tests(void) {
    printk("\n--- Cross-Component Integration Tests ---\n");
    serial_print("\n--- Cross-Component Integration Tests ---\n");

    // [CRITICAL FIX]: Since we bypassed Syscall, hardware interrupts remained disabled.
    // We manually enable interrupts here for ATA disk and Timer to work!
    asm volatile("sti");

    extern int multitasking_enabled;
    multitasking_enabled = 0; // Stop the Scheduler so it doesn't interrupt tests

    int old_sec_level = current_sec_level;
    current_sec_level = 0; 

    // VFS CLEANUP (Delete only the files we created)
    fs_delete("int_test.txt", 0);
    fs_delete("dummy.elf", 0);

    // Stack variables (Safe memory)
    char u_file[] = "int_test.txt";
    char u_data[] = "Integration Test Data";
    char u_elf_name[] = "dummy.elf";
    uint8_t u_elf_data[64];

    // Direct Kernel VFS functions are used
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
    u_elf_data[0] = 0x7F; u_elf_data[1] = 'E'; u_elf_data[2] = 'L'; u_elf_data[3] = 'F'; 
    u_elf_data[4] = 1;   
    u_elf_data[16] = 2;  
    u_elf_data[18] = 3;  
    
    int res4 = fs_create_file(u_elf_name, u_elf_data, 64, 0);
    KTEST_ASSERT(res4 >= 0, "[STRICT] PROC-INT: Valid minimal ELF written to disk for testing");

    int p_idx = load_and_exec_elf(u_elf_name, 0);
    KTEST_ASSERT(p_idx >= 0, "[STRICT] PROC-INT: load_and_exec_elf read from disk and returned valid Array Index");
    
    // [FIXED]: The following old ktest_syscall deletions were also updated with fs_delete
    fs_delete(u_file, 0);
    fs_delete(u_elf_name, 0);
    
    // Destroy the fake (dummy) task
    extern process_t tasks[];
    if (p_idx >= 0 && p_idx < 16) { 
        tasks[p_idx].state = 0;     // 0 = TASK_EMPTY
    }

    current_sec_level = old_sec_level;
    multitasking_enabled = 1; // Restore system to normal
}