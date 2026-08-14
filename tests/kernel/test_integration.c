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

/*
 * No ktest_syscall() helper here, unlike most of the sibling modules. There was
 * one, copied along with the includes, and nothing in this file ever called it:
 * these tests drive the VFS and the ELF loader through their kernel entry points
 * directly rather than across the trap gate. Removed rather than left as a
 * stub that quietly implies a coverage this module does not have.
 */

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

    asm volatile("sti");
multitasking_enabled = 0; 

    int old_sec_level = current_sec_level;
    current_sec_level = 0; 

    fs_delete("int_test.txt", 0);
    fs_delete("dummy.elf", 0);

    char u_file[] = "int_test.txt";
    char u_data[] = "Integration Test Data";
    char u_elf_name[] = "dummy.elf";
    uint8_t u_elf_data[sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t) + 1];

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
    
    ft_memset(u_elf_data, 0, sizeof(u_elf_data));
    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)u_elf_data;
    ehdr->e_ident[0] = 0x7F; ehdr->e_ident[1] = 'E';
    ehdr->e_ident[2] = 'L';  ehdr->e_ident[3] = 'F';
    ehdr->e_ident[4] = 1;    ehdr->e_ident[5] = 1;
    ehdr->e_ident[6] = 1;
    ehdr->e_type = 2;
    ehdr->e_machine = 3;
    ehdr->e_version = 1;
    ehdr->e_entry = 0x00400000;
    ehdr->e_phoff = sizeof(elf32_ehdr_t);
    ehdr->e_ehsize = sizeof(elf32_ehdr_t);
    ehdr->e_phentsize = sizeof(elf32_phdr_t);
    ehdr->e_phnum = 1;

    elf32_phdr_t *phdr = (elf32_phdr_t *)(u_elf_data + ehdr->e_phoff);
    phdr->p_type = 1;
    phdr->p_offset = sizeof(elf32_ehdr_t) + sizeof(elf32_phdr_t);
    phdr->p_vaddr = 0x00400000;
    phdr->p_filesz = 1;
    phdr->p_memsz = 1;
    phdr->p_flags = 5;
    u_elf_data[phdr->p_offset] = 0xF4; // HLT, if this fixture is ever scheduled.
    
    int res4 = fs_create_file(u_elf_name, u_elf_data, sizeof(u_elf_data), 0);
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
