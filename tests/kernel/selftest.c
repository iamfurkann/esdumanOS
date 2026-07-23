/*
 * File: selftest.c
 * Purpose: Main runner for the comprehensive kernel test suite.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "stdio.h"

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

    if (tests_failed == 0) {
        qemu_shutdown(1);
    } else {
        qemu_shutdown(0);
    }
}