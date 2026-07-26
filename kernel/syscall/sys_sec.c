/*
 * File: sys_sec.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "io.h"
#include "process.h"
#include "security.h"
#include "errno.h"
#include "klog.h"
#include "keyboard.h"
#include "bcache.h"
#include "kernel.h"
#include "rtc.h"

/**
 * @brief Function sys_auth
 */
void sys_auth(arch_regs_t *regs) {
    if (!validate_string_pointer((const char *)regs->ebx, MAX_FILENAME) || 
        !validate_string_pointer((const char *)regs->ecx, MAX_FILENAME)) { 
        regs->eax = E_FAULT; 
        return; 
    }

    uint32_t current_ticks = timer_get_ticks();

    if (current_task->auth_fail_ticks != 0 && (current_ticks - current_task->auth_fail_ticks) < 300) {
        klog(LOG_LEVEL_WARN, "AUTH", "Brute-force detected! Wait before trying again.");
        regs->eax = E_FAULT; 
        return;
    }

    char *user = (char *)regs->ebx;
    char *pass = (char *)regs->ecx;

    int p_len = 0;
    while (pass[p_len]) p_len++;
    while (p_len > 0 && (pass[p_len - 1] == '\n' || pass[p_len - 1] == '\r' || pass[p_len - 1] == ' ')) {
        pass[p_len - 1] = '\0';
        p_len--;
    }

    int auth_res = verify_user_password(user, pass);
    
    if (auth_res >= 0) {
        current_task->auth_fail_ticks = 0;
        regs->eax = auth_res;
    } else {
        current_task->auth_fail_ticks = timer_get_ticks();
        regs->eax = E_FAULT;
    }
}

/**
 * @brief Function sys_setuid_call
 */
void sys_setuid_call(arch_regs_t *regs) {
    uint32_t requested_uid = (uint32_t)regs->ebx;
    char *provided_password = (char *)regs->ecx;

    if (current_task->uid == 0) {
        current_task->uid = requested_uid;
        regs->eax = E_OK; 
        klog_int(LOG_LEVEL_INFO, "AUTH", "SUDO: Privilege changed. New UID", requested_uid);
        return;
    }

    if (requested_uid == 0) {
        if (!validate_string_pointer((const char *)provided_password, 64)) {
            regs->eax = E_FAULT; 
            return;
        }

        uint32_t current_ticks = timer_get_ticks();
        
        if (current_task->auth_fail_ticks != 0 && (current_ticks - current_task->auth_fail_ticks) < 300) {
            klog(LOG_LEVEL_WARN, "AUTH", "SUDO Brute-force protection! Please wait 3 seconds.");
            regs->eax = E_ACCES; 
            return;
        }

        int p_len = 0; 
        while (provided_password[p_len]) p_len++;
        while (p_len > 0 && (provided_password[p_len - 1] == '\n' || provided_password[p_len - 1] == '\r')) {
            provided_password[p_len - 1] = '\0'; p_len--;
        }
        
        int auth_uid = verify_user_password("root", provided_password);

        if (auth_uid == 0) { 
            current_task->uid = 0; 
            current_task->auth_fail_ticks = 0;
            regs->eax = E_OK;
            klog_int(LOG_LEVEL_INFO, "AUTH", "Password verified. Escalated to ROOT privilege. PID", current_task->pid);
        } else {
            current_task->auth_fail_ticks = timer_get_ticks(); 
            klog(LOG_LEVEL_WARN, "AUTH", "Incorrect ROOT password!");
            regs->eax = E_ACCES;
        }
        return;
    }
    
    regs->eax = E_PERM; 
}

/**
 * @brief Function sys_set_layout
 */
void sys_set_layout(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for keyboard layout.");
        regs->eax = E_PERM; 
        return; 
    }
    current_layout = regs->ebx;
    regs->eax = 0;
}

/**
 * @brief Function sys_set_sec_level
 */
void sys_set_sec_level(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for security level.");
        regs->eax = E_PERM; 
        return; 
    }
    set_security_level((security_level_t)regs->ebx);
    regs->eax = 0;
}

/**
 * @brief Function sys_lockdown
 */
void sys_lockdown(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for lockdown.");
        regs->eax = E_PERM; 
        return; 
    }
    
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) {
        printk("System is already in SECURE MODE!\n");
    } else {
        set_security_level(SEC_LEVEL_LOCKDOWN);
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[WARNING] KERNEL LOCKED (SECURE MODE ACTIVE)!\n\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    regs->eax = 0;
}

/**
 * @brief Function sys_panic
 */
void sys_panic(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to trigger Kernel Panic.");
        regs->eax = E_PERM; 
        return; 
    }
    asm volatile("int $0x0");
    regs->eax = 0;
}

/**
 * @brief Function sys_reboot
 */
void sys_reboot(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to reboot.");
        regs->eax = E_PERM; 
        return; 
    }
    bcache_flush(); 
    
    outb(0x64, 0xFE);
    regs->eax = 0;
}

/**
 * @brief Function sys_halt
 */
void sys_halt(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to halt system.");
        regs->eax = E_PERM; 
        return; 
    }
    bcache_flush(); 
    
    printk("System halted safely.\n"); 
    asm volatile("cli; hlt");
    regs->eax = 0;
}

/**
 * @brief Function sys_dmesg
 */
void sys_dmesg(arch_regs_t *regs) {
    if (current_task != 0 && current_task->uid == 0) {
        dump_klog();
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to read DMESG.");
        regs->eax = E_PERM; 
    }
}