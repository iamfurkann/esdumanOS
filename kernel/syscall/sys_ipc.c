/*
 * File: sys_ipc.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "process.h"
#include "errno.h"
#include "klog.h"
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

    if (!validate_user_pointer((const void *)sender_ptr, 4) || 
        !validate_user_pointer((const void *)payload_ptr, 4)) { 
        regs->eax = E_FAULT; 
        return; 
    }
    
    regs->eax = receive_message(sender_ptr, payload_ptr);
}

/**
 * @brief Function sys_alarm
 */
void sys_alarm(arch_regs_t *regs) {
    printk("Alarm set! It will ring in 3 seconds...\n");
    schedule_kernel_timer(1, 55); 
    regs->eax = 0;
}

/**
 * @brief Function sys_signal_reg
 */
void sys_signal_reg(arch_regs_t *regs) {
    int sig_num = (int)regs->ebx;
    uint32_t handler_addr = (uint32_t)regs->ecx;
    if (!validate_user_pointer((const void *)handler_addr, 4)) { 
        regs->eax = E_FAULT;
        return; 
    }
    
    register_user_signal(sig_num, handler_addr);
    regs->eax = 0;
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