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



static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void qemu_shutdown(int is_success) {
    // Sending 0x10 gives Exit Code 33 (SUCCESS)
    // Sending 0x11 gives Exit Code 35 (FAILURE)
    outb(0xf4, is_success ? 0x10 : 0x11);
}

void run_all_selftests(void) {
    terminal_initialize();
    printk("\n======================================================\n");
    printk("       KFS COMPREHENSIVE KERNEL TEST SUITE            \n");
    printk("======================================================\n");
    
    // Call Modules
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