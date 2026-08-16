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
 * @brief Function sys_kill
 */
void sys_kill(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    int sig_num = (int)regs->ecx;

    uint32_t my_uid = current_task->uid;
    int has_permission = 0;
    
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
