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

/**
 * @brief Executes security and authorization tests for the kernel.
 *
 * This test suite verifies the robustness of the kernel's user identification (UID) 
 * system, privilege escalation barriers, and anti-brute-force mechanisms. 
 *
 * Expected behavior:
 * - Proper authentication allows UID transition.
 * - Invalid authentication (wrong password, invalid UID) strictly fails and returns an error.
 * - Brute-force protection must enforce a time-based lockout after a failed attempt.
 * - Invalid system call numbers must be safely rejected without destabilizing the kernel.
 *
 * Edge cases covered:
 * - Authentication attempts during an active brute-force lockout period, even if the password is correct.
 * - Exploitation attempts utilizing out-of-bounds syscall numbers.
 */
void run_security_tests(void) {
    printk("\n--- Security and Authorization Tests ---\n");
    serial_print("\n--- Security and Authorization Tests ---\n");
    
    int original_uid = 0;
    if (current_task >= 0) {
        original_uid = tasks[current_task].uid;
    }
    
    char *u_pass_wrong = (char *)0x500400;
    char *u_pass_correct = (char *)0x500500;
    char *u_empty = (char *)0x500600;
    
    // Populate the user-space addresses with test data.
    ft_strcpy(u_pass_wrong, "wrong_passwd_123"); // Intentionally incorrect password
    ft_strcpy(u_pass_correct, "1234");           // Valid root password configured in the system
    ft_strcpy(u_empty, "");

    sys_setuid(1000, u_empty);

    int root_res_correct = sys_setuid(0, u_pass_correct);
    KTEST_ASSERT(root_res_correct == 0, "[STRICT] sys_setuid strictly returns 0 with the CORRECT password");

    // Drop root privileges again for subsequent tests.
    sys_setuid(1000, u_empty);

    int root_res_wrong = sys_setuid(0, u_pass_wrong);
    KTEST_ASSERT(root_res_wrong < 0, "[STRICT] sys_setuid strictly returns < 0 with WRONG password");
    
    int invalid_uid = sys_setuid(9999, u_empty);
    KTEST_ASSERT(invalid_uid < 0, "[STRICT] sys_setuid strictly returns < 0 with invalid UID");

    int lockout_res = sys_setuid(0, u_pass_correct);
    KTEST_ASSERT(lockout_res < 0, "[STRICT] Brute-Force Protection: Rejected even with correct password before lockout period expires");

    extern uint32_t auth_fail_ticks[];
    if (current_task >= 0) {
        auth_fail_ticks[current_task] = 0; 
    }

    int invalid_syscall = ktest_syscall(999, 0, 0, 0);
    KTEST_ASSERT(invalid_syscall < 0, "[STRICT] Kernel: Invalid (999) Syscall number rejected without locking up");

    if (current_task >= 0) {
        tasks[current_task].uid = original_uid; 
    }
}