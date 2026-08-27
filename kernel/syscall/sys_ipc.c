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
 * @brief The longest alarm that can be represented, in seconds.
 *
 * Written as the relationship rather than the number it works out to. Deadlines
 * are absolute values of a 32-bit tick counter and are compared as signed
 * differences - see expire_alarms() - so the furthest future a deadline can name
 * without reading as already past is half the counter's range. At TIMER_HZ that
 * is a little under 249 days.
 *
 * A request beyond it is refused rather than clamped. Clamping would answer a
 * question the caller did not ask, and the value it would answer with is one no
 * caller of this system has a use for.
 */
#define ALARM_MAX_SECONDS (0x7FFFFFFFu / TIMER_HZ)

/**
 * @brief Arms, re-arms or cancels the calling process's alarm.
 *
 * ebx is a count of seconds; zero cancels. eax comes back as the number of
 * seconds that were left on the previous alarm, or 0 when there was none - which
 * is POSIX's arrangement and is what lets a caller restore an alarm it had to
 * displace.
 *
 * This was not an alarm until v1.0.0. It read none of its arguments, delivered no
 * signal, and had no relation to the process that called it: it armed kernel
 * timer slot 1 for a fixed three seconds and let alarm_demo_callback() print a
 * green line on the console. The name had promised three things and kept none of
 * them, and it was the last call in the table that printed from inside the
 * kernel on a caller's behalf.
 *
 * The deadline lives in the caller's PCB rather than in a timer slot because the
 * slots are global and hold a bare void(*)(void) - they carry no pid, so there
 * is nobody for one to signal. That is the same reason sleep() keeps its own
 * deadline, and the note on those fields in process.h says so.
 *
 * Nothing here blocks. An alarm is something that happens to a running process,
 * and expire_alarms() delivers SIG_ALRM from schedule() when the deadline
 * passes; a caller that wants to wait for it blocks on whatever it actually
 * meant to wait for.
 */
void sys_alarm(arch_regs_t *regs) {
    int seconds = (int)regs->ebx;
    uint32_t now = timer_get_ticks();
    uint32_t remaining = 0;

    if (seconds < 0 || (uint32_t)seconds > ALARM_MAX_SECONDS) {
        regs->eax = E_INVAL;
        return;
    }

    /*
     * Read the old alarm before writing the new one: re-arming reports what it
     * displaced, and the two share the same pair of fields.
     */
    if (current_task->alarm_active) {
        int32_t left = (int32_t)(current_task->alarm_deadline - now);

        /*
         * Rounded up, so an alarm with any time at all left on it reports at
         * least one second. Rounding down would report 0, and 0 is the answer
         * that means there was no alarm - a caller restoring what it displaced
         * would drop an alarm that was about to fire.
         */
        if (left > 0) {
            remaining = ((uint32_t)left + TIMER_HZ - 1) / TIMER_HZ;
        }
    }

    if (seconds == 0) {
        current_task->alarm_active = 0;
        current_task->alarm_deadline = 0;
    } else {
        current_task->alarm_deadline = now + (uint32_t)seconds * TIMER_HZ;
        current_task->alarm_active = 1;
    }

    regs->eax = (int)remaining;
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
