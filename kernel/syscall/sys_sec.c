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
#include "entropy.h"
#include "acpi.h"
#include "bcache.h"
#include "kernel.h"
#include "rtc.h"
#include "uaccess.h"
#include "kheap.h"
/* For fs_keyslot_rewrap() and KEYSLOT_MAX_PASSPHRASE, which sys_setkey() needs.
 * fs.h pulls in keyslot.h, so the passphrase ceiling comes with it. */
#include "fs.h"

/* Ceiling on the kernel staging buffer for a dmesg read. Mirrors
 * READDIR_STAGE_MAX in sys_fs.c and exists for the same reason: the size comes
 * from user space. */
#define DMESG_STAGE_MAX 4096

/**
 * @brief Changes the disk passphrase; ebx is the old one, ecx the new one.
 *
 * The first call added after the ABI froze, and the freeze is why it is 68: the
 * rule is that new numbers continue from the highest assigned one, not that they
 * fill the gaps at 11, 28, 30, 31, 32 and 99.
 *
 * Root only, and the reason is narrower than it looks. This does not protect the
 * files from the caller - root can already read every one of them through the
 * mounted file system, because the disk is unlocked the moment the machine
 * booted. What it protects is the disk at rest: whoever can change the
 * passphrase decides who can mount this disk on the next boot, and that is not a
 * decision a second user gets to make.
 *
 * No brute-force throttle, unlike sys_auth(). A caller who is already root has
 * nothing to gain by guessing the old passphrase - everything it would unlock is
 * already open to them - and PBKDF2 at the configured work factor is its own
 * rate limit besides.
 *
 * Both buffers are wiped before returning. They sit on the kernel stack, which
 * the next syscall will reuse without clearing.
 */
void sys_setkey(arch_regs_t *regs) {
    char old_pass[KEYSLOT_MAX_PASSPHRASE];
    char new_pass[KEYSLOT_MAX_PASSPHRASE];
    int res;

    if (current_task->uid != 0) {
        klog(LOG_LEVEL_WARN, "SEC", "setkey: refused, only root may change the disk passphrase.");
        regs->eax = E_PERM;
        return;
    }

    if (!validate_string_pointer((const char *)regs->ebx, sizeof(old_pass)) ||
        !validate_string_pointer((const char *)regs->ecx, sizeof(new_pass)) ||
        copy_string_from_user(old_pass, (const char *)regs->ebx, sizeof(old_pass)) != E_OK ||
        copy_string_from_user(new_pass, (const char *)regs->ecx, sizeof(new_pass)) != E_OK) {
        regs->eax = E_FAULT;
        return;
    }

    /*
     * fs_keyslot_rewrap() carries the whole operation, including the refusal
     * under IMMUTABLE and the rule that a failed re-wrap leaves the live slot
     * untouched. Nothing is re-implemented here.
     */
    res = fs_keyslot_rewrap(old_pass, new_pass);

    {
        volatile char *o = old_pass;
        volatile char *n = new_pass;
        for (uint32_t i = 0; i < sizeof(old_pass); i++) { o[i] = 0; n[i] = 0; }
    }

    regs->eax = res;
}

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
        /* No group database, so the two move together - see process_s. */
        current_task->gid = requested_uid;
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
            current_task->gid = 0;
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
    
    /*
     * This one keeps printing from the kernel, and deliberately - v0.9.2 moved
     * the commands that produce *output* into the caller's descriptor 1, and
     * this does not produce output. It announces a state change to whoever is at
     * the console, in red, and it is the same class of message as the one
     * sys_halt() prints on the way down: there is no pipeline that wants it and
     * redirecting it would mean the warning went somewhere nobody was looking.
     *
     * What it was missing is the log. A security level that changed left no
     * record, so `dmesg` after the fact could not say when - or whether - the
     * system had been locked.
     */
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) {
        printk("System is already in SECURE MODE!\n");
    } else {
        set_security_level(SEC_LEVEL_LOCKDOWN);
        klog(LOG_LEVEL_WARN, "SEC", "Lockdown engaged; the system is in secure mode.");
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
    /* Before the flush, so the sectors these write go out with everything else.
     * The log lived in RAM and went with the machine until now, and the entropy
     * seed with it. Neither can fail this call: both record what went wrong and
     * the flush - which is what the caller actually asked for - still happens. */
    klog_persist();
    entropy_persist_seed();
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
    entropy_persist_seed();
    bcache_flush();

    /*
     * A ladder, and every rung answers a machine the one above it does not.
     *
     * This was one line - the i8042 pulse below - and that line assumed a
     * keyboard controller. A modern UEFI laptop need not have one at all, which
     * made `reboot` a command that returned successfully and did nothing on
     * exactly the hardware this project is aimed at.
     *
     * Each rung is tried in turn and none of them is expected to return. A rung
     * that does return has failed, whatever it wrote, so the next one is taken.
     * The order runs from the most specific description of this machine to the
     * least: the FADT's own reset register, then the port every modern chipset
     * answers on, then the keyboard controller, and finally a triple fault -
     * which is not a reset method so much as the absence of one, and is here
     * because a machine that will not restart politely still has to restart.
     */
    acpi_reset();

    /*
     * The PCI reset control register. Not ACPI and not firmware-described: it is
     * a fixed port that Intel and AMD chipsets have answered on for twenty
     * years, and it is what makes this ladder work on a machine whose FADT has
     * no reset register - which is most of them.
     */
    outb(0xCF9, 0x02);   /* select a hard reset */
    outb(0xCF9, 0x06);   /* and request it */

    outb(0x64, 0xFE);

    /*
     * A null interrupt descriptor table and an interrupt into it. There is no
     * handler, no double fault handler to catch that, and the processor gives up
     * - which is a reset. Reached only when every described method has been
     * tried and the machine is still here.
     */
    {
        struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = { 0, 0 };

        asm volatile("lidt %0; int $3" : : "m"(null_idt));
    }

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
    entropy_persist_seed();
    bcache_flush();

    /*
     * `halt` means what people expect it to mean now, and that is the whole of
     * what this release is for.
     *
     * It stopped the processor and left the machine powered, which is what the
     * word meant on hardware that had no other option and is not what anybody
     * has meant by it since. The fans went on running, the battery went on
     * draining, and the only way to finish was the power button - on the one
     * machine class this project is aiming at.
     *
     * No new syscall number was spent on this. 21 keeps its number, its
     * parameters and its name; the shell's `halt` builtin is unchanged and
     * simply reaches a machine that switches off. The frozen ABI promised the
     * numbers would not move and they have not.
     */
    if (acpi_poweroff() != E_OK) {
        /*
         * The fallback, and it is the old behaviour rather than a new failure.
         * A machine with no ACPI, or whose _S5_ this kernel could not read, ends
         * where it always ended - and acpi_init() has already said in the log
         * which of those it was, at boot, while there was still somewhere to
         * say it.
         */
        printk("System halted safely. Power is still on; this machine has no ACPI.\n");
        asm volatile("cli; hlt");
    }

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
 * **edx counts records, not bytes.** It was a byte offset into a flat ring, and
 * the ring holds records now - a position in bytes cannot survive one of them
 * being dropped between two reads, because every byte after the gap shifts and
 * the reader is handed a torn line. Index 0 is the oldest record still held and
 * a caller walks forward one at a time, which is also what lets it notice a
 * record it expected has gone.
 *
 * A buffer shorter than KLOG_LINE_MAX truncates the record rather than splitting
 * it across two reads: there is no position within a record to resume from, and
 * inventing one would put the byte offset back.
 *
 * @param regs ebx is a user buffer or 0, ecx its size, edx the record index to
 *             read. On return eax holds the bytes written, 0 past the last
 *             record, or a negative errno.
 */
void sys_dmesg(arch_regs_t *regs) {
    if (current_task == 0 || current_task->uid != 0) {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to read DMESG.");
        regs->eax = E_PERM;
        return;
    }

    char *user_buf = (char *)regs->ebx;
    int size = (int)regs->ecx;
    int index = (int)regs->edx;

    if (user_buf == 0) {
        dump_klog();
        regs->eax = 0;
        return;
    }

    if (size <= 0 || index < 0) { regs->eax = E_INVAL; return; }
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

    int n = klog_read(kbuf, cap, index);
    if (n > 0 && copy_to_user(user_buf, kbuf, (size_t)n) != E_OK) {
        kfree(kbuf);
        regs->eax = E_FAULT;
        return;
    }

    kfree(kbuf);
    regs->eax = n;
}

/**
 * @brief Inspects and controls the kernel log.
 *
 * The read side has been reachable since v0.4.x. Everything around it has not:
 * current_log_level has sat at INFO since boot with nothing able to move it, so
 * every DEBUG record the kernel composed was discarded unseen - a filter nobody
 * could adjust is a filter that only ever removes. There has been no way to
 * clear the ring either, and no way to ask what it lost when it wrapped.
 *
 * Split by consequence rather than by uniformity: clearing the log and moving
 * the threshold change what everyone else sees afterwards, and are root's.
 * Reading the threshold or the counters changes nothing and is anyone's, which
 * is what lets an ordinary program notice records went missing between two
 * reads.
 *
 * @param regs ebx is the operation, ecx its argument. On return eax holds the
 *             requested value, E_OK, or a negative errno.
 */
void sys_klog_ctl(arch_regs_t *regs) {
    if (current_task == 0) { regs->eax = E_SRCH; return; }

    uint32_t op = regs->ebx;

    switch (op) {
        case KLOG_CTL_CLEAR:
            if (current_task->uid != 0) {
                klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to clear the log.");
                regs->eax = E_PERM;
                return;
            }
            klog_clear();
            /* Recorded after the clear, so the log is never empty and can always
             * say why it is short. */
            klog(LOG_LEVEL_INFO, "KLOG", "Log cleared.");
            regs->eax = E_OK;
            return;

        case KLOG_CTL_SET_LEVEL:
            if (current_task->uid != 0) {
                klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required to set the log level.");
                regs->eax = E_PERM;
                return;
            }
            if ((int)regs->ecx < LOG_LEVEL_DEBUG || (int)regs->ecx > LOG_LEVEL_CRITICAL) {
                regs->eax = E_INVAL;
                return;
            }
            klog_set_level((int)regs->ecx);
            /*
             * Recorded at CRITICAL so that it survives whatever it was just set
             * to. Any other level would be filtered by the very change it is
             * reporting in one direction or the other, and a log that has gone
             * quiet without saying why is the thing this record exists to
             * prevent someone chasing.
             */
            klog_int(LOG_LEVEL_CRITICAL, "KLOG", "Log level set to", (int)regs->ecx);
            regs->eax = E_OK;
            return;

        case KLOG_CTL_GET_LEVEL:
            regs->eax = (uint32_t)klog_get_level();
            return;

        case KLOG_CTL_HELD:
            regs->eax = klog_held();
            return;

        case KLOG_CTL_DROPPED:
            regs->eax = klog_dropped();
            return;

        default:
            regs->eax = E_INVAL;
            return;
    }
}
