#include "ktest.h"
#include "syscall.h"
#include "fs.h"
#include "process.h"

extern void ft_strcpy(char *dest, const char *src);
extern int load_and_exec_elf(const char *name, uint8_t parent_id);
extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
extern int fs_delete(const char *name, uint8_t parent_id); 
extern int ft_strlen(const char *s);
extern int current_sec_level;
extern disk_file_entry_t dir_table[];

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_integration_tests(void) {
    printk("\n--- Bilesenler Arasi Entegrasyon Testleri ---\n");
    serial_print("\n--- Bilesenler Arasi Entegrasyon Testleri ---\n");

    // [KRİTİK ÇÖZÜM]: Syscall'u aradan çıkardığımız için donanım kesmeleri kapalı kaldı.
    // ATA diskinin ve Timer'ın çalışabilmesi için kesmeleri burada manuel açıyoruz!
    asm volatile("sti");

    extern int multitasking_enabled;
    multitasking_enabled = 0; // Testleri böldürmemek için Zamanlayıcıyı durdur

    int old_sec_level = current_sec_level;
    current_sec_level = 0; 

    // VFS TEMİZLİK (GÜVENLİ MANUEL SIFIRLAMA)
    for (int i = 1; i < 64; i++) { 
        if (dir_table[i].is_used == 1) {
            if (dir_table[i].filename[0] == 'd' || dir_table[i].filename[0] == 'o') {
                dir_table[i].is_used = 0; 
                dir_table[i].filename[0] = '\0';
            }
        }
    }

    // Stack değişkenleri (Güvenli bellek)
    char u_file[] = "int_test.txt";
    char u_data[] = "Entegrasyon Test Verisi";
    char u_elf_name[] = "dummy.elf";
    uint8_t u_elf_data[64];

    // Doğrudan Kernel VFS fonksiyonları kullanılıyor
    int res1 = fs_create_file(u_file, (uint8_t *)u_data, ft_strlen(u_data), 0); 
    KTEST_ASSERT(res1 >= 0, "[STRICT] VFS-INT: Dosya ilk kez basariyla diske yazildi");

    int res2 = fs_delete(u_file, 0); 
    KTEST_ASSERT(res2 >= 0, "[STRICT] VFS-INT: Dosya basariyla silindi (Sektorler serbest birakildi)");

    int res3 = fs_create_file(u_file, (uint8_t *)u_data, ft_strlen(u_data), 0);
    KTEST_ASSERT(res3 >= 0, "[STRICT] VFS-INT: Ayni isimle yeni dosya yazildi (VFS Index donusumu basarili)");

    // =========================================================================
    // PROC-INT: Process Yükleyici (ELF) ve VFS Entegrasyonu
    // =========================================================================
    for (int i = 0; i < 64; i++) u_elf_data[i] = 0;
    u_elf_data[0] = 0x7F; u_elf_data[1] = 'E'; u_elf_data[2] = 'L'; u_elf_data[3] = 'F'; 
    u_elf_data[4] = 1;   
    u_elf_data[16] = 2;  
    u_elf_data[18] = 3;  
    
    int res4 = fs_create_file(u_elf_name, u_elf_data, 64, 0);
    KTEST_ASSERT(res4 >= 0, "[STRICT] PROC-INT: Test icin gecerli minimal ELF diske yazildi");

    int p_idx = load_and_exec_elf(u_elf_name, 0);
    KTEST_ASSERT(p_idx >= 0, "[STRICT] PROC-INT: load_and_exec_elf diskten okuyup gecerli Array Index dondu");
    
    // [DÜZELTİLDİ]: Aşağıdaki eski ktest_syscall silme işlemleri de fs_delete ile güncellendi
    fs_delete(u_file, 0);
    fs_delete(u_elf_name, 0);
    
    // Sahte (dummy) görevi yok et
    extern process_t tasks[];
    if (p_idx >= 0 && p_idx < 16) { 
        tasks[p_idx].state = 0;     // 0 = TASK_EMPTY
    }

    current_sec_level = old_sec_level;
    multitasking_enabled = 1; // Sistemi normale döndür
}