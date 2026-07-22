/*
 * File: test_security.c
 * Purpose: Security and Authorization tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h" 
#include "process.h" // To access process_t and tasks[] array

extern void ft_strcpy(char *dest, const char *src);

// Import kernel variables to clean up UID after test
extern process_t tasks[];

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

static inline int sys_setuid(int uid, const char *password) {
    return ktest_syscall(SYSCALL_SETUID, uid, (int)password, 0);
}

void run_security_tests(void) {
    printk("\n--- Security and Authorization Tests ---\n");
    serial_print("\n--- Security and Authorization Tests ---\n");
    
    // [TEST ISOLATION]: Save the original UID before starting the test
    int original_uid = 0;
    if (current_task >= 0) {
        original_uid = tasks[current_task].uid;
    }
    
    // Place addresses in User Space to bypass syscall security
    char *u_pass_wrong = (char *)0x500400;
    char *u_pass_correct = (char *)0x500500;
    char *u_empty = (char *)0x500600;
    
    ft_strcpy(u_pass_wrong, "yanlis_sifre_123");
    ft_strcpy(u_pass_correct, "1234"); // Valid root password in the system
    ft_strcpy(u_empty, "");

    // 1. DROP ROOT PRIVILEGES: Temporarily set UID to 1000 (Normal User)
    sys_setuid(1000, u_empty);

    // 2. CORRECT PASSWORD TEST: Must return 0! (Done first to avoid brute-force lock)
    int root_res_correct = sys_setuid(0, u_pass_correct);
    KTEST_ASSERT(root_res_correct == 0, "[STRICT] sys_setuid strictly returns 0 with the CORRECT password");

    // Drop root privileges again
    sys_setuid(1000, u_empty);

    // 3. WRONG PASSWORD TEST: Normal user tries to become Root with wrong password
    int root_res_wrong = sys_setuid(0, u_pass_wrong);
    KTEST_ASSERT(root_res_wrong < 0, "[STRICT] sys_setuid strictly returns < 0 with WRONG password");
    
    // 4. INVALID UID TEST: Must return -1!
    int invalid_uid = sys_setuid(9999, u_empty);
    KTEST_ASSERT(invalid_uid < 0, "[STRICT] sys_setuid strictly returns < 0 with invalid UID");

    // 5. BRUTE-FORCE LOCKOUT TEST: 
    // The brute-force counter is triggered due to the previous wrong password attempt.
    // Even if the CORRECT password is entered now, it MUST BE REJECTED because 3 seconds haven't passed!
    int lockout_res = sys_setuid(0, u_pass_correct);
    KTEST_ASSERT(lockout_res < 0, "[STRICT] Brute-Force Protection: Rejected even with correct password before lockout period expires");

    // Reset the brute-force timer to continue tests
    extern uint32_t auth_fail_ticks[];
    if (current_task >= 0) {
        auth_fail_ticks[current_task] = 0; 
    }

    // 6. INVALID SYSCALL NUMBER TEST
    int invalid_syscall = ktest_syscall(999, 0, 0, 0);
    KTEST_ASSERT(invalid_syscall < 0, "[STRICT] Kernel: Invalid (999) Syscall number rejected without locking up");

    if (current_task >= 0) {
        tasks[current_task].uid = original_uid; 
    }
}