/*
 * File: test_passwd.c
 * Purpose: Passwd and Shadow security tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h" 
#include "process.h"

extern void ft_strcpy(char *dest, const char *src);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern process_t tasks[];

// Syscall bridge
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_passwd_tests(void) {
    printk("\n--- Passwd & Shadow Security Tests ---\n");
    serial_print("\n--- Passwd & Shadow Security Tests ---\n");

    // Preserve original state
    int original_uid = 0;
    if (current_task >= 0) original_uid = tasks[current_task].uid;

    // Map addresses to User-Space to bypass syscall security shield
    char *u_etc = (char *)0x502000;
    char *u_passwd = (char *)0x502100;
    char *u_hacked = (char *)0x502200;
    char *u_content = (char *)0x502300;

    ft_strcpy(u_etc, "etc");
    ft_strcpy(u_passwd, "passwd");
    ft_strcpy(u_hacked, "passwd_hacked");
    ft_strcpy(u_content, "root:x:0:0:root:/root:/bin/sh");

    // =========================================================================
    // [PREPARATION]: Create /etc and /etc/passwd as ROOT for test environment
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = 0;

    int etc_id = fs_get_entry_idx("etc", 0);
    if (etc_id == -1) {
        ktest_syscall(26, (int)u_etc, 0, 0); // SYSCALL_MKDIR
        etc_id = fs_get_entry_idx("etc", 0);
    }
    KTEST_ASSERT(etc_id != -1, "/etc directory exists under root");

    int passwd_idx = fs_get_entry_idx("passwd", etc_id);
    if (passwd_idx == -1) {
        ktest_syscall(8, (int)u_passwd, (int)u_content, etc_id); // SYSCALL_CREATE_FILE
    }
    KTEST_ASSERT(fs_get_entry_idx("passwd", etc_id) != -1, "/etc/passwd file protected in VFS");


    // =========================================================================
    // [SIMULATION]: Drop privileges and attack Critical System Files!
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = 1000; // Normal User

    // 1. Removal (RM) Attack
    int rm_res = ktest_syscall(22, (int)u_passwd, etc_id, 0); // SYSCALL_RM_FILE
    KTEST_ASSERT(rm_res < 0, "[STRICT] Normal user CANNOT DELETE /etc/passwd");

    // 2. Overwrite Attack
    int wr_res = ktest_syscall(8, (int)u_passwd, (int)u_content, etc_id); // SYSCALL_CREATE_FILE
    KTEST_ASSERT(wr_res < 0, "[STRICT] Normal user CANNOT OVERWRITE /etc/passwd");

    // 3. Rename (RENAME/BYPASS) Attack
    int mv_res = ktest_syscall(23, (int)u_passwd, (int)u_hacked, etc_id); // SYSCALL_MV_FILE
    KTEST_ASSERT(mv_res < 0, "[STRICT] Normal user CANNOT RENAME /etc/passwd");

    // =========================================================================
    // Cleanup: Restore privileges
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = original_uid;
}