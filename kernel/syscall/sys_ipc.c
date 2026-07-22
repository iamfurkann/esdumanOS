#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "process.h"
#include "errno.h"
#include "klog.h"

extern process_t tasks[];
extern int send_message(int target_pid, uint32_t payload);
extern int receive_message(uint32_t *sender_out, uint32_t *payload_out);
extern void schedule_kernel_timer(int sig_num, uint32_t delay_ticks);
extern void register_user_signal(int sig_num, uint32_t handler_addr);
extern void send_user_signal(int target_pid, int sig_num);

void sys_ipc_send(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    uint32_t payload = regs->ecx;
    
    regs->eax = send_message(target_pid, payload);
}

void sys_ipc_receive(arch_regs_t *regs) {
    uint32_t *sender_ptr = (uint32_t *)regs->ebx;
    uint32_t *payload_ptr = (uint32_t *)regs->ecx;

    if (!validate_user_pointer((const void *)sender_ptr, 4) || 
        !validate_user_pointer((const void *)payload_ptr, 4)) { 
        regs->eax = -1; // E_FAULT 
        return; 
    }
    
    regs->eax = receive_message(sender_ptr, payload_ptr);
}

void sys_alarm(arch_regs_t *regs) {
    printk("Alarm kuruldu! 3 saniye sonra calacak...\n");
    schedule_kernel_timer(1, 55); 
    regs->eax = 0;
}

void sys_signal_reg(arch_regs_t *regs) {
    int sig_num = (int)regs->ebx;
    uint32_t handler_addr = (uint32_t)regs->ecx;
    if (!validate_user_pointer((const void *)handler_addr, 4)) { 
        regs->eax = -1; // E_FAULT
        return; 
    }
    
    register_user_signal(sig_num, handler_addr);
    regs->eax = 0;
}

void sys_kill(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    int sig_num = (int)regs->ecx;

    uint32_t my_uid = tasks[current_task].uid;
    int has_permission = 0;
    
    if (my_uid == 0) {
        has_permission = 1;
    } else {
        for (int i = 0; i < 16; i++) {
            if (tasks[i].pid == target_pid && tasks[i].state != 0) {
                if (tasks[i].uid == my_uid) {
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
        klog(LOG_LEVEL_WARN, "SYSCALL", "kill: Erisim Engellendi (Yetkisiz islem)!");
        regs->eax = -1; // E_PERM
    }
}