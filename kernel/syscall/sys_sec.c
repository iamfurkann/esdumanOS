/*
 * File: sys_sec.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
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
#include "uaccess.h"
#include "kheap.h"

/* Ceiling on the kernel staging buffer for a dmesg read. Mirrors
 * READDIR_STAGE_MAX in sys_fs.c and exists for the same reason: the size comes
 * from user space. */
#define DMESG_STAGE_MAX 4096

/**
 * @brief Function sys_auth
 */
void sys_auth(arch_regs_t *regs) {
    char user[MAX_FILENAME];
    char pass[MAX_FILENAME];
    if (!validate_string_pointer((const char *)regs->ebx, sizeof(user)) ||
        !validate_string_pointer((const char *)regs->ecx, sizeof(pass)) ||
        copy_string_from_user(user, (const char *)regs->ebx, sizeof(user)) != E_OK ||
        copy_string_from_user(pass, (const char *)regs->ecx, sizeof(pass)) != E_OK) {
        regs->eax = E_FAULT; 
        return; 
    }

    uint32_t current_ticks = timer_get_ticks();

    if (current_task->auth_fail_ticks != 0 && (current_ticks - current_task->auth_fail_ticks) < 300) {
        klog(LOG_LEVEL_WARN, "AUTH", "Brute-force detected! Wait before trying again.");
        regs->eax = E_FAULT; 
        return;
    }

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

    if (current_task->uid == 0) {
        current_task->uid = requested_uid;
        regs->eax = E_OK; 
        klog_int(LOG_LEVEL_INFO, "AUTH", "SUDO: Privilege changed. New UID", requested_uid);
        return;
    }

    if (requested_uid == 0) {
        char provided_password[64];
        if (!validate_string_pointer((const char *)regs->ecx, sizeof(provided_password)) ||
            copy_string_from_user(provided_password, (const char *)regs->ecx, sizeof(provided_password)) != E_OK) {
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
 * @brief Writes every dirty block-cache sector out to disk.
 *
 * The cache is write-back, and the automatic policy only promises that dirty data
 * reaches the platter within BCACHE_FLUSH_INTERVAL_TICKS or once
 * BCACHE_DIRTY_HIGH_WATER slots are outstanding. A caller that has just written
 * something it cannot afford to lose needs a way to close that window itself.
 *
 * Unprivileged on purpose: it writes back data the caller was already allowed to
 * write, creates nothing new, and the alternative - making durability a root-only
 * capability - would leave ordinary programs no way to commit their own work.
 *
 * Not gated on the security level either. IMMUTABLE stops new writes, but dirty
 * sectors already in the cache were produced while they were still permitted, and
 * refusing to flush them would guarantee exactly the loss this syscall exists to
 * prevent.
 */
void sys_sync(arch_regs_t *regs) {
    /* Before the flush, so the sectors this writes go out with everything else.
     * The log lived in RAM and went with the machine until now. */
    klog_persist();
    bcache_flush();
    regs->eax = E_OK;
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
    klog_persist();
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
    klog_persist();
    bcache_flush();

    printk("System halted safely.\n"); 
    asm volatile("cli; hlt");
    regs->eax = 0;
}

/**
 * @brief Function sys_dmesg
 *
 * Two forms. With a buffer it copies a slice of the log out and returns the
 * count, leaving the caller to write it wherever its descriptor 1 points; with a
 * null buffer it dumps to the screen, which is what it always did and what a
 * caller with no descriptors of its own still wants.
 *
 * The buffer form exists because the screen dump could not be piped or
 * redirected: it goes through terminal_putchar(), which knows nothing about the
 * calling process, so "dmesg | head" fed an empty pipe and "dmesg > file" left
 * the file empty. Writing to descriptor 1 from in here instead would not work
 * either - see klog_read() for why a kernel-side dump cannot block partway
 * through.
 *
 * @param regs ebx is a user buffer or 0, ecx its size, edx the byte offset in
 *             the log to read from. On return eax holds the bytes copied, 0 at
 *             the end of the log, or a negative errno.
 */
void sys_dmesg(arch_regs_t *regs) {
    if (current_task == 0 || current_task->uid != 0) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to read DMESG.");
        regs->eax = E_PERM;
        return;
    }

    char *user_buf = (char *)regs->ebx;
    int size = (int)regs->ecx;
    int offset = (int)regs->edx;

    if (user_buf == 0) {
        dump_klog();
        regs->eax = 0;
        return;
    }

    if (size <= 0 || offset < 0) { regs->eax = E_INVAL; return; }
    if (!validate_user_writable_pointer(user_buf, (size_t)size)) {
        regs->eax = E_FAULT;
        return;
    }

    /*
     * Staged in kernel memory and handed over with one copy_to_user(), the same
     * shape sys_readdir() uses and for the same reason: storing through the user
     * pointer directly skips the uaccess path, and under SMAP every such store
     * faults with no fixup installed.
     *
     * size comes from user space, so the staging buffer is capped - kmalloc()ing
     * whatever the caller asks for is a one-line way to exhaust the kernel heap.
     * A caller with a larger buffer gets a short read and comes back for more.
     */
    int cap = (size > DMESG_STAGE_MAX) ? DMESG_STAGE_MAX : size;

    char *kbuf = (char *)kmalloc((uint32_t)cap);
    if (!kbuf) { regs->eax = E_NOMEM; return; }

    int n = klog_read(kbuf, cap, offset);
    if (n > 0 && copy_to_user(user_buf, kbuf, (size_t)n) != E_OK) {
        kfree(kbuf);
        regs->eax = E_FAULT;
        return;
    }

    kfree(kbuf);
    regs->eax = n;
}
