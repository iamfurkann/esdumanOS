/*
 * File: sys_process.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "fs.h"
#include "process.h"
#include "errno.h"
#include "klog.h"
#include "elf.h"
#include "isr.h"
#include "kheap.h"
#include "pmm.h"
#include "stack.h"
/**
 * @brief Function sys_exit
 */
void sys_exit(arch_regs_t *regs) {
    exit_current_process(regs);
}

/**
 * @brief Function sys_exec
 */
void sys_exec(arch_regs_t *regs) {
    if (!validate_string_pointer((const char *)regs->ebx, 256)) { 
        regs->eax = E_FAULT; 
        return; 
    }
    char *target_path = (char *)regs->ebx;
    uint8_t calling_dir_id = (uint8_t)regs->ecx;

    char temp_args[128];
    for (int k = 0; k < 128; k++) temp_args[k] = '\0';
    char *args_str = (char *)regs->edx; 
    
    if (args_str) {
        if (!validate_string_pointer((const char *)args_str, 128)) { 
            regs->eax = E_FAULT; 
            return; 
        }
        int i = 0; 
        while (args_str[i] && i < 127) { 
            temp_args[i] = args_str[i]; 
            i++; 
        } 
        temp_args[i] = '\0';
    }

    char basename[MAX_FILENAME];
    int parent_id = vfs_resolve_path(target_path, calling_dir_id, basename);
    if (parent_id < 0 || basename[0] == '\0') { 
        regs->eax = E_NOENT; 
        return; 
    }

    int bin_idx = fs_get_entry_idx("bin", 0);
    int bin_id = (bin_idx != -1) ? dir_table[bin_idx].entry_id : -1;

    // ROOT permission is required for programs outside the /bin directory
    if (parent_id != bin_id && current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "exec: Permission denied! (ROOT required for programs outside /bin)");
        regs->eax = E_ACCES; 
        return; 
    }

    int child_idx = load_and_exec_elf(basename, parent_id); 
    if (child_idx >= 0) {
        foreground_task = child_idx;
        process_t *child_process = 0;
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == child_idx) { child_process = p; break; }
        }
        if (child_process) {
            int i = 0; 
            while (temp_args[i]) { 
                child_process->cmd_args[i] = temp_args[i]; 
            i++; 
            }
            child_process->cmd_args[i] = '\0';
        }

        sleep_current_task(regs, 5); // WAIT_CHILD = 5
        regs->eax = E_OK;
    } else { 
        regs->eax = E_NOEXEC; 
    }
}

/**
 * @brief Function sys_set_priority
 */
void sys_set_priority(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    uint8_t new_priority = (uint8_t)regs->ecx;
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
        // [SECURITY PATCH NH-005]: CPU Starvation protection for normal users
        if (my_uid != 0 && new_priority > 10) {
            klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Maximum limit exceeded! DoS prevented.");
            new_priority = 10;
        }
        set_task_priority(target_pid, new_priority);
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Permission denied (Unauthorized operation)!");
        regs->eax = E_PERM;
    }
}

/**
 * @brief Function sys_yield
 */
void sys_yield(arch_regs_t *regs) {
    schedule(regs);
}

/**
 * @brief Function sys_getuid
 */
void sys_getuid(arch_regs_t *regs) {
    if (current_task != 0) {
        regs->eax = current_task->uid;
    } else {
        regs->eax = E_FAULT;
    }
}

/**
 * @brief Function sys_get_args
 */
void sys_get_args(arch_regs_t *regs) {
    char *buf = (char *)regs->ebx;
    if (!validate_user_pointer((const void *)buf, 128)) { 
        regs->eax = E_FAULT; 
        return; 
    }
    
    int i = 0;
    while (current_task->cmd_args[i] && i < 127) {
        buf[i] = current_task->cmd_args[i];
        i++;
    }
    buf[i] = '\0';
    regs->eax = i;
}


/* ── DEBUGGING AND MEMORY QUERIES ──────────────────────────── */

/**
 * @brief Function sys_stack_dump
 */
void sys_stack_dump(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    print_kernel_stack();
    regs->eax = 0;
}

/**
 * @brief Function sys_meminfo
 */
void sys_meminfo(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    printk("RAM: Total %d MB | Free %d MB\n", pmm_get_total_memory() / (1024 * 1024), pmm_get_free_memory() / (1024 * 1024));
    regs->eax = 0;
}

/**
 * @brief Function sys_test_malloc
 */
void sys_test_malloc(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    char *w = (char *)kmalloc(50);
    if (w) { 
        w[0] = '4'; w[1] = '2'; w[2] = '\0'; 
        printk("Allocated: 0x%x, Size: %d\n", (uint32_t)w, kmalloc_size(w)); 
        kfree(w); 
    }
    regs->eax = 0;
}

/**
 * @brief Function sys_hexdump
 */
void sys_hexdump(arch_regs_t *regs) {
    if (!validate_user_pointer((const void *)regs->ebx, 64)) { 
        regs->eax = E_FAULT; 
        return; 
    }
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    print_hexdump(regs->ebx, 64);
    regs->eax = 0;
}