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

extern process_t tasks[];
extern security_level_t current_sec_level; 
extern int current_layout;

extern void set_security_level(security_level_t level);
extern void dump_klog(void);
extern uint32_t timer_get_ticks(void);
extern int verify_user_password(const char *username, const char *password);
extern void bcache_flush(void);

uint32_t auth_fail_ticks[16] = {0};

void sys_auth(arch_regs_t *regs) {
    if (!validate_string_pointer((const char *)regs->ebx, 64) || 
        !validate_string_pointer((const char *)regs->ecx, 64)) { 
        regs->eax = -1; 
        return; 
    }

    uint32_t current_ticks = timer_get_ticks();

    if (auth_fail_ticks[current_task] != 0 && (current_ticks - auth_fail_ticks[current_task]) < 300) {
        klog(LOG_LEVEL_WARN, "AUTH", "Brute-force tespit edildi! Yeni deneme icin bekleyin.");
        regs->eax = -1; 
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
        auth_fail_ticks[current_task] = 0;
        regs->eax = auth_res;
    } else {
        auth_fail_ticks[current_task] = timer_get_ticks();
        regs->eax = -1;
    }
}

void sys_setuid_call(arch_regs_t *regs) {
    uint32_t requested_uid = (uint32_t)regs->ebx;
    char *provided_password = (char *)regs->ecx;

    if (tasks[current_task].uid == 0) {
        tasks[current_task].uid = requested_uid;
        regs->eax = E_OK; 
        klog_int(LOG_LEVEL_INFO, "AUTH", "SUDO: Yetki degistirildi. Yeni UID", requested_uid);
        return;
    }

    if (requested_uid == 0) {
        if (!validate_string_pointer((const char *)provided_password, 64)) {
            regs->eax = E_FAULT; 
            return;
        }

        uint32_t current_ticks = timer_get_ticks();
        
        if (auth_fail_ticks[current_task] != 0 && (current_ticks - auth_fail_ticks[current_task]) < 300) {
            klog(LOG_LEVEL_WARN, "AUTH", "SUDO Brute-force korumasi! Lutfen 3 saniye bekleyin.");
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
            tasks[current_task].uid = 0; 
            auth_fail_ticks[current_task] = 0;
            regs->eax = E_OK;
            klog_int(LOG_LEVEL_INFO, "AUTH", "Parola dogrulandi. ROOT yetkisine yukseltildi. PID", current_task);
        } else {
            auth_fail_ticks[current_task] = timer_get_ticks(); 
            klog(LOG_LEVEL_WARN, "AUTH", "Yanlis ROOT parolasi!");
            regs->eax = E_ACCES;
        }
        return;
    }
    
    regs->eax = E_PERM; 
}

void sys_set_layout(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Klavye duzeni icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    current_layout = regs->ebx;
    regs->eax = 0;
}

void sys_set_sec_level(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Guvenlik seviyesi icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    set_security_level((security_level_t)regs->ebx);
    regs->eax = 0;
}

void sys_lockdown(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Lockdown icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) {
        printk("Sistem zaten SECURE MODE altinda!\n");
    } else {
        set_security_level(SEC_LEVEL_LOCKDOWN);
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[DIKKAT] KERNEL KILITLENDI (SECURE MODE AKTIF)!\n\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    regs->eax = 0;
}

void sys_panic(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Kernel Panic tetiklemek icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    asm volatile("int $0x0");
    regs->eax = 0;
}

void sys_reboot(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Yeniden baslatma icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    bcache_flush(); 
    
    outb(0x64, 0xFE);
    regs->eax = 0;
}

void sys_halt(arch_regs_t *regs) {
    if (tasks[current_task].uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: Sistemi durdurmak icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
        return; 
    }
    bcache_flush(); 
    
    printk("Sistem guvenli bir sekilde durduruldu.\n"); 
    asm volatile("cli; hlt");
    regs->eax = 0;
}

void sys_dmesg(arch_regs_t *regs) {
    if (current_task >= 0 && tasks[current_task].uid == 0) {
        dump_klog();
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "Erisim Engellendi: DMESG okumak icin ROOT yetkisi gereklidir.");
        regs->eax = E_PERM; 
    }
}