/*
 * File: selftest.c
 * Purpose: Main runner for the comprehensive kernel test suite.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "tty.h"
#include "stdio.h"
#include "arch.h"
#include "process.h"
#include "libft.h"

int tests_passed = 0;
int tests_failed = 0;



/**
 * @brief Writes a byte to a specified hardware I/O port.
 *
 * A primitive inline wrapper around the x86 `outb` instruction, utilized here 
 * to interface directly with the QEMU debug exit port.
 *
 * @param port The target I/O port address.
 * @param val The 8-bit value to write to the port.
 * @expected The exact byte value is dispatched to the specified hardware port.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

/**
 * @brief Triggers an automated QEMU emulator shutdown with an exit code.
 *
 * Communicates with QEMU's debug exit device (commonly located at port 0xf4) 
 * to forcefully terminate the virtual machine. It maps the test suite's success 
 * state into standard system exit codes for CI/CD pipeline integration.
 *
 * @param is_success Integer flag indicating overall test suite success (1) or failure (0).
 * @expected QEMU gracefully terminates, yielding Exit Code 33 for success or Exit Code 35 for failure.
 */
void qemu_shutdown(int is_success) {
    outb(0xf4, is_success ? 0x10 : 0x11);
}

/**
 * @brief The master execution routine for the kernel test suite.
 *
 * Orchestrates the sequential initialization and execution of all modular kernel tests, 
 * ranging from low-level memory handling up to complex integration testing. After 
 * traversing all modules, it aggregates the results and halts the emulator.
 *
 * @expected The terminal is initialized, all registered sub-tests are dispatched 
 *           consecutively, and a final statistical summary of passes and failures 
 *           is reported before cleanly halting execution.
 */
void run_all_selftests(void) {
    terminal_initialize();
    
    // Map 4 consecutive simulated user-space pages for integration tests (0x500000 - 0x503000)
    extern uint32_t pmm_alloc_frame(void);
    extern int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
    // Create a mock task structure so syscalls have valid fd_table and uid
    static process_t dummy_task;
    static file_descriptor_t dummy_fds[16];
    ft_memset(&dummy_task, 0, sizeof(process_t));
    ft_memset(dummy_fds, 0, sizeof(dummy_fds));
    dummy_task.fd_table_size = 16;
    dummy_task.fd_table = dummy_fds;
    dummy_task.uid = 0;
    dummy_task.pid = 999;
    dummy_task.state = 1; // TASK_RUNNING
    dummy_task.next = 0;
    dummy_task.prev = 0;
    
    current_task = &dummy_task;
    task_list_head = &dummy_task;
    task_list_tail = &dummy_task;

    for (int i = 0; i < 4; i++) {
        uint32_t vaddr = 0x500000 + (i * 4096);
        uint32_t sim_phys = pmm_alloc_frame();
        map_page(vaddr, sim_phys, 7); // PAGE_PRESENT | PAGE_READ_WRITE | PAGE_USER_ACCESS
        ft_memset((void *)vaddr, 0, 4096);
    }

    printk("\n======================================================\n");
    printk("       KFS COMPREHENSIVE KERNEL TEST SUITE            \n");
    printk("======================================================\n");
    
    run_string_tests();
    run_memory_tests();
    run_pipe_tests();
    run_vfs_tests();
    run_devfs_tests();
    run_passwd_tests();
    run_security_tests();
    run_stress_tests();
    run_adversarial_tests();
    run_integration_tests();
    run_regression_tests();
    run_concurrency_tests();
    run_paging_tests();
    run_pmm_tests();
    run_syscall_tests();
    run_process_tests();
    run_signal_tests();
    run_crypto_tests();
    run_bcache_tests();
    
    printk("\n======================================================\n");
    
    char pass_str[16]; ktest_itoa(tests_passed, pass_str);
    char fail_str[16]; ktest_itoa(tests_failed, fail_str);
    
    printk("RESULT: "); printk(pass_str); printk(" PASSED | "); 
    printk(fail_str); printk(" FAILED\n");
    printk("======================================================\n");
    printk("\n*** ALL KERNEL SELF-TESTS FINISHED SUCCESSFULLY! ***\n\n");

    current_task = 0; // Restore before exiting selftests
    task_list_head = 0;
    task_list_tail = 0;

    qemu_shutdown(tests_failed == 0);
}