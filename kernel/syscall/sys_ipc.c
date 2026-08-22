/*
 * File: sys_ipc.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "registers.h"
#include "stdio.h"
#include "process.h"
#include "errno.h"
#include "klog.h"
#include "uaccess.h"
#include "signal.h"
#include "rtc.h"
/**
 * @brief Function sys_ipc_send
 */
void sys_ipc_send(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    uint32_t payload = regs->ecx;
    
    regs->eax = send_message(target_pid, payload);
}

/**
 * @brief Function sys_ipc_receive
 */
void sys_ipc_receive(arch_regs_t *regs) {
    uint32_t *sender_ptr = (uint32_t *)regs->ebx;
    uint32_t *payload_ptr = (uint32_t *)regs->ecx;

    if (!validate_user_writable_pointer((const void *)sender_ptr, 4) ||
        !validate_user_writable_pointer((const void *)payload_ptr, 4)) {
        regs->eax = E_FAULT; 
        return; 
    }

    uint32_t sender;
    uint32_t payload;
    int ret = receive_message(&sender, &payload);
    if (ret == E_OK &&
        (copy_to_user(sender_ptr, &sender, sizeof(sender)) != E_OK ||
         copy_to_user(payload_ptr, &payload, sizeof(payload)) != E_OK)) {
        ret = E_FAULT;
    }
    regs->eax = ret;
}

/**
 * @brief Function sys_alarm
 */
/**
 * @brief Arms the demo alarm timer.
 *
 * The delay used to be the literal 55, which at TIMER_HZ is 0.55 seconds - while
 * the message printed right above it promised three. The mismatch survived
 * because this call was compiled against the invented declaration that used to be
 * in process.h, where the second parameter was a pid rather than a tick count, so
 * neither the compiler nor a reader had reason to read 55 as a duration.
 *
 * Timer slot 1 is the one kernel_main() registers alarm_demo_callback() on.
 */
#define ALARM_DEMO_SECONDS 3

void sys_alarm(arch_regs_t *regs) {
    printk("Alarm set! It will ring in %d seconds...\n", ALARM_DEMO_SECONDS);
    schedule_kernel_timer(1, TIMER_HZ * ALARM_DEMO_SECONDS);
    regs->eax = 0;
}

/**
 * @brief Function sys_signal_reg
 *
 * The handler address used to be validated here as well as inside
 * register_user_signal(), the same test written out twice. Adding SIG_IGN as a
 * disposition meant relaxing that test, and only the copy in process.c was
 * relaxed - so a Ring 3 caller asking to ignore a signal was refused with
 * E_FAULT before register_user_signal() ever saw it, while a kernel-mode caller
 * of the same function succeeded. The check lives in one place now and this
 * reports what it decided.
 */
void sys_signal_reg(arch_regs_t *regs) {
    int sig_num = (int)regs->ebx;
    uint32_t handler_addr = (uint32_t)regs->ecx;

    regs->eax = register_user_signal(sig_num, handler_addr);
}

/**
 * @brief Sends a signal to a process, or to every member of a process group.
 *
 * A negative pid names the group -pid. That is POSIX's spelling and it is what
 * the shell needs to continue a stopped job: a job is a group, and the process
 * the shell forked is often not the only member of it - that process may itself
 * have started the program the user is looking at, and Ctrl-Z stopped both. A
 * continue delivered to the one pid the shell happens to know would leave the
 * other stopped, with the first blocked waiting for it.
 *
 * @param regs ebx is the pid, or the negated group; ecx is the signal number.
 */
void sys_kill(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    int sig_num = (int)regs->ecx;

    uint32_t my_uid = current_task->uid;
    int has_permission = 0;

    if (target_pid < 0) {
        uint32_t pgid = (uint32_t)(-target_pid);
        int members = 0;
        int foreign = 0;

        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->state == TASK_EMPTY || p->state == TASK_DEAD) continue;
            if (p->pgid != pgid) continue;
            members++;
            if (p->uid != my_uid) foreign = 1;
        }

        /* An empty group is not a group. Reported rather than quietly succeeding,
         * so a shell asking to continue a job that has since finished can tell
         * that from a job that ignored it. */
        if (members == 0) {
            regs->eax = E_SRCH;
            return;
        }

        /* Every member, not just one: a group holding a process the caller does
         * not own is a group the caller cannot signal. Root is exempt, as it is
         * for a single target. */
        if (my_uid == 0 || !foreign) {
            send_signal_to_group(pgid, sig_num);
            regs->eax = 0;
        } else {
            klog(LOG_LEVEL_WARN, "SYSCALL", "kill: Permission denied (group holds another user's process)!");
            regs->eax = E_PERM;
        }
        return;
    }

    if (my_uid == 0) {
        has_permission = 1;
    } else {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == target_pid && p->state != 0) {
                if (p->uid == my_uid) {
                    has_permission = 1; 
                }
                break;
            }
        }
    }
    
    if (has_permission) {
        send_user_signal(target_pid, sig_num);
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "kill: Permission denied (Unauthorized operation)!");
        regs->eax = E_PERM;
    }
}
