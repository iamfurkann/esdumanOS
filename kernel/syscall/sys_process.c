#include "syscalls_internal.h"
#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "fs.h"
#include "process.h"
#include "errno.h"
#include "klog.h"

extern process_t tasks[];
extern int foreground_task;
extern disk_file_entry_t dir_table[];

extern void exit_current_process(arch_regs_t *regs);
extern void sleep_current_task(arch_regs_t *regs, int reason);
extern void schedule(arch_regs_t *regs);
extern int load_and_exec_elf(const char *name, uint8_t parent_id);
extern void set_task_priority(int pid, uint8_t new_priority);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern void print_kernel_stack(void);
extern uint32_t pmm_get_total_memory(void);
extern uint32_t pmm_get_free_memory(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern uint32_t kmalloc_size(void *ptr);

void sys_exit(arch_regs_t *regs) {
    exit_current_process(regs);
}

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

    char basename[64];
    int parent_id = vfs_resolve_path(target_path, calling_dir_id, basename);
    if (parent_id == -1 || basename[0] == '\0') { 
        regs->eax = E_NOENT; 
        return; 
    }

    int bin_idx = fs_get_entry_idx("bin", 0);
    int bin_id = (bin_idx != -1) ? dir_table[bin_idx].entry_id : -1;

    // Sadece /bin klasörü dışındaki programlar için ROOT yetkisi istenir
    if (parent_id != bin_id && tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "exec: Erisim Engellendi! (/bin disindaki programlar icin ROOT gerekir)");
        regs->eax = E_ACCES; 
        return; 
    }

    int child_idx = load_and_exec_elf(basename, parent_id); 
    if (child_idx >= 0) {
        foreground_task = child_idx;
        int i = 0; 
        while (temp_args[i]) { 
            tasks[child_idx].cmd_args[i] = temp_args[i]; 
            i++; 
        }
        tasks[child_idx].cmd_args[i] = '\0';

        sleep_current_task(regs, 5); // WAIT_CHILD = 5
        regs->eax = E_OK;
    } else { 
        regs->eax = E_NOEXEC; 
    }
}

void sys_set_priority(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    uint8_t new_priority = (uint8_t)regs->ecx;
    uint32_t my_uid = tasks[current_task].uid;
    
    int has_permission = 0;
    if (my_uid == 0) {
        has_permission = 1;
    } else {
        for (int i = 0; i < 16; i++) { // MAX_TASKS
            if (tasks[i].pid == target_pid && tasks[i].state != 0) {
                if (tasks[i].uid == my_uid) {
                    has_permission = 1;
                }
                break;
            }
        }
    }

    if (has_permission) {
        // [GÜVENLİK YAMASI NH-005]: Normal kullanıcılar için CPU Starvation koruması
        if (my_uid != 0 && new_priority > 10) {
            klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Maksimum sinir asildi! DoS engellendi.");
            new_priority = 10;
        }
        set_task_priority(target_pid, new_priority);
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Erisim Engellendi (Yetkisiz islem)!");
        regs->eax = -1; // E_PERM
    }
}

void sys_yield(arch_regs_t *regs) {
    schedule(regs);
}

void sys_getuid(arch_regs_t *regs) {
    if (current_task >= 0) {
        regs->eax = tasks[current_task].uid;
    } else {
        regs->eax = -1;
    }
}

void sys_get_args(arch_regs_t *regs) {
    char *buf = (char *)regs->ebx;
    if (!validate_user_pointer((const void *)buf, 128)) { 
        regs->eax = -1; 
        return; 
    }
    
    int i = 0;
    while (tasks[current_task].cmd_args[i] && i < 127) {
        buf[i] = tasks[current_task].cmd_args[i];
        i++;
    }
    buf[i] = '\0';
    regs->eax = i;
}


/* ── HATA AYIKLAMA (DEBUG) VE BELLEK SORGULARI ──────────────────────────── */

void sys_stack_dump(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Bu komut icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    print_kernel_stack();
    regs->eax = 0;
}

void sys_meminfo(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Bu komut icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    printk("RAM: Toplam %d MB | Bos %d MB\n", pmm_get_total_memory() / (1024 * 1024), pmm_get_free_memory() / (1024 * 1024));
    regs->eax = 0;
}

void sys_test_malloc(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Bu komut icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    char *w = (char *)kmalloc(50);
    if (w) { 
        w[0] = '4'; w[1] = '2'; w[2] = '\0'; 
        printk("Ayrilan: 0x%x, Boyut: %d\n", (uint32_t)w, kmalloc_size(w)); 
        kfree(w); 
    }
    regs->eax = 0;
}

void sys_hexdump(arch_regs_t *regs) {
    if (!validate_user_pointer((const void *)regs->ebx, 64)) { 
        regs->eax = -1; 
        return; 
    }
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Bu komut icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    print_hexdump(regs->ebx, 64);
    regs->eax = 0;
}