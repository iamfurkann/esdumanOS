#include "ktest.h"
#include "syscall.h" 
#include "process.h"

extern void ft_strcpy(char *dest, const char *src);
extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern process_t tasks[];
extern int current_task;

// Syscall köprüsü
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_passwd_tests(void) {
    printk("\n--- Passwd & Shadow Security Tests ---\n");
    serial_print("\n--- Passwd & Shadow Security Tests ---\n");

    // Orijinal durumu koru
    int original_uid = 0;
    if (current_task >= 0) original_uid = tasks[current_task].uid;

    // Syscall güvenlik zırhını aşmak için adresleri User-Space bölgesine haritala
    char *u_etc = (char *)0x502000;
    char *u_passwd = (char *)0x502100;
    char *u_hacked = (char *)0x502200;
    char *u_content = (char *)0x502300;

    ft_strcpy(u_etc, "etc");
    ft_strcpy(u_passwd, "passwd");
    ft_strcpy(u_hacked, "passwd_hacked");
    ft_strcpy(u_content, "root:x:0:0:root:/root:/bin/sh");

    // =========================================================================
    // [HAZIRLIK]: Test ortamı için /etc ve /etc/passwd'yi ROOT olarak oluştur
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = 0;

    int etc_id = fs_get_entry_idx("etc", 0);
    if (etc_id == -1) {
        ktest_syscall(26, (int)u_etc, 0, 0); // SYSCALL_MKDIR
        etc_id = fs_get_entry_idx("etc", 0);
    }
    KTEST_ASSERT(etc_id != -1, "/etc dizini root altinda mevcut");

    int passwd_idx = fs_get_entry_idx("passwd", etc_id);
    if (passwd_idx == -1) {
        ktest_syscall(8, (int)u_passwd, (int)u_content, etc_id); // SYSCALL_CREATE_FILE
    }
    KTEST_ASSERT(fs_get_entry_idx("passwd", etc_id) != -1, "/etc/passwd dosyasi VFS'te korumaya alindi");


    // =========================================================================
    // [SİMÜLASYON]: Yetkileri düşür ve Kritik Sistem Dosyalarına saldır!
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = 1000; // Normal Kullanıcı

    // 1. Silme (RM) Saldırısı
    int rm_res = ktest_syscall(22, (int)u_passwd, etc_id, 0); // SYSCALL_RM_FILE
    KTEST_ASSERT(rm_res == -1, "[STRICT] Normal kullanici /etc/passwd dosyasini SILEMEZ");

    // 2. Üzerine Yazma / Ezme (OVERWRITE) Saldırısı
    int wr_res = ktest_syscall(8, (int)u_passwd, (int)u_content, etc_id); // SYSCALL_CREATE_FILE
    KTEST_ASSERT(wr_res == -1, "[STRICT] Normal kullanici /etc/passwd uzerine YAZAMAZ");

    // 3. Yeniden Adlandırma (RENAME/BYPASS) Saldırısı
    int mv_res = ktest_syscall(23, (int)u_passwd, (int)u_hacked, etc_id); // SYSCALL_MV_FILE
    KTEST_ASSERT(mv_res == -1, "[STRICT] Normal kullanici /etc/passwd ismini DEGISTIREMEZ");

    // =========================================================================
    // Temizlik: Yetkileri geri ver
    // =========================================================================
    if (current_task >= 0) tasks[current_task].uid = original_uid;
}