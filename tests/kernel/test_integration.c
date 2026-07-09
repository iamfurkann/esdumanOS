#include "ktest.h"
#include "syscall.h"
#include "fs.h"

extern void ft_strcpy(char *dest, const char *src);
extern int load_and_exec_elf(const char *name, uint8_t parent_id);
extern int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
// [YENİ]: VFS'i formatlamak için delete fonksiyonunu içeri aktarıyoruz
extern int fs_delete(const char *name, uint8_t parent_id); 
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

    // 1. ZIRH İPTALİ: VFS'in "Lockdown / Salt-Okunur" modunu geçici olarak kaldır.
    int old_sec_level = current_sec_level;
    current_sec_level = 0; 

    // =========================================================================
    // 2. VFS TAM TEMİZLİK (FORMAT İŞLEMİ)
    // Önceki VFS ve Sınır testleri dizin tablosunu (dir_table) ve sektörleri 
    // doldurduğu için (E_NOMEM hatası), geriye kalan tüm çöp dosyaları siliyoruz!
    // =========================================================================
    for (int i = 1; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1) {
            fs_delete(dir_table[i].filename, dir_table[i].parent_id);
        }
    }

    // 3. POINTER ZIRHI AŞIMI: Syscall'un reddetmemesi için veriler User-Space'e alınıyor.
    char *u_file = (char *)0x600000;
    char *u_data = (char *)0x600100;
    char *u_elf_name = (char *)0x600200;
    uint8_t *u_elf_data = (uint8_t *)0x600300;

    ft_strcpy(u_file, "int_test.txt");
    ft_strcpy(u_data, "Entegrasyon Test Verisi");
    ft_strcpy(u_elf_name, "dummy.elf");

    // VFS-INT 1: Dosyayı İlk Kez Yazma (8 = SYSCALL_CREATE_FILE)
    int res1 = ktest_syscall(8, (int)u_file, (int)u_data, 0); 
    KTEST_ASSERT(res1 >= 0, "[STRICT] VFS-INT: Dosya ilk kez basariyla diske yazildi");

    // VFS-INT 2: Dosyayı Silme (22 = SYSCALL_RM_FILE)
    int res2 = ktest_syscall(22, (int)u_file, 0, 0); 
    KTEST_ASSERT(res2 >= 0, "[STRICT] VFS-INT: Dosya basariyla silindi (Sektorler serbest birakildi)");

    // VFS-INT 3: Aynı İsimle Yeniden Yazma
    int res3 = ktest_syscall(8, (int)u_file, (int)u_data, 0);
    KTEST_ASSERT(res3 >= 0, "[STRICT] VFS-INT: Ayni isimle yeni dosya yazildi (VFS Index donusumu basarili)");

    // =========================================================================
    // PROC-INT: Process Yükleyici (ELF) ve VFS Entegrasyonu
    // =========================================================================
    // Syscall üzerinden ikili (binary) dosya yollarsak, Syscall'daki "strlen" 
    // '\0' (Null) baytında dosyayı keseceği için ELF Header bozuluyordu. 
    // Bu tuzağı aşmak için doğrudan fs_create_file ile 64 byte yazdırıyoruz!
    for (int i = 0; i < 64; i++) u_elf_data[i] = 0;
    u_elf_data[0] = 0x7F; u_elf_data[1] = 'E'; u_elf_data[2] = 'L'; u_elf_data[3] = 'F'; 
    u_elf_data[4] = 1;   // 32-bit mimari
    u_elf_data[16] = 2;  // e_type = Executable
    u_elf_data[18] = 3;  // e_machine = x86
    
    int res4 = fs_create_file(u_elf_name, u_elf_data, 64, 0);
    KTEST_ASSERT(res4 >= 0, "[STRICT] PROC-INT: Test icin gecerli minimal ELF diske yazildi");

    int p_idx = load_and_exec_elf("dummy.elf", 0);
    KTEST_ASSERT(p_idx >= 0, "[STRICT] PROC-INT: load_and_exec_elf diskten okuyup gecerli Array Index dondu");
    
    // Temizlik ve Güvenlik Zırhını Geri Açma
    ktest_syscall(22, (int)u_file, 0, 0);
    ktest_syscall(22, (int)u_elf_name, 0, 0);
    current_sec_level = old_sec_level;
}