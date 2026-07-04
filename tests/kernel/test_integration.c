#include "ktest.h"
#include "syscall.h"
#include "process.h" // process_t ve tasks[] dizisi için

extern void ft_strcpy(char *dest, const char *src);
extern int load_and_exec_elf(const char *filename);
extern process_t tasks[]; // Syscall'daki gibi process tablosuna erişiyoruz

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

void run_integration_tests(void) {
    printk("\n--- Bilesenler Arasi Entegrasyon Testleri ---\n");
    serial_print("\n--- Bilesenler Arasi Entegrasyon Testleri ---\n");

    // =========================================================
    // 1. VFS ENTEGRASYONU: Yaz -> Sil -> Tekrar Yaz Döngüsü
    // Amaç: VFS'in silinmiş dosya isimlerini (Dizin tablosu) 
    // ve sektörleri (Bitmap) başarıyla geri dönüştürdüğünü kanıtlamak.
    // =========================================================
    char *u_file = (char *)0x501000;
    char *u_data1 = (char *)0x501100;
    char *u_data2 = (char *)0x501200;
    
    ft_strcpy(u_file, "cycle.txt");
    ft_strcpy(u_data1, "DATA_1");
    ft_strcpy(u_data2, "DATA_2");

    int cr1 = ktest_syscall(8 /* CREATE_FILE */, (int)u_file, (int)u_data1, 0);
    KTEST_ASSERT(cr1 >= 0, "[STRICT] VFS-INT: Dosya ilk kez basariyla diske yazildi");

    int del = ktest_syscall(22 /* RM_FILE */, (int)u_file, 0, 0);
    KTEST_ASSERT(del >= 0, "[STRICT] VFS-INT: Dosya basariyla silindi (Sektorler serbest birakildi)");

    int cr2 = ktest_syscall(8 /* CREATE_FILE */, (int)u_file, (int)u_data2, 0);
    KTEST_ASSERT(cr2 >= 0, "[STRICT] VFS-INT: Ayni isimle yeni dosya yazildi (VFS Index/Sektor donusumu basarili)");

    // =========================================================
    // 2. PROCESS ENTEGRASYONU: Exec + Task Table Zinciri
    // Amaç: VFS -> Paging -> Process hiyerarşisinin kusursuzluğunu
    // kriptografik uyuşmazlıklara takılmadan kanıtlamak.
    // =========================================================
    char *elf_name = (char *)0x501300;
    ft_strcpy(elf_name, "test_app.elf");

    // 52 Byte'lık geçerli bir Dummy (Sahte) ELF Başlığı oluşturuyoruz
    uint8_t *minimal_elf = (uint8_t *)0x501400;
    extern void ft_memset(void *s, int c, uint32_t n);
    ft_memset(minimal_elf, 0, 52); 

    minimal_elf[0] = 0x7F; minimal_elf[1] = 'E'; minimal_elf[2] = 'L'; minimal_elf[3] = 'F';
    minimal_elf[4] = 1; minimal_elf[5] = 1; minimal_elf[6] = 1; 
    minimal_elf[16] = 2; minimal_elf[18] = 3; minimal_elf[20] = 1; 
    minimal_elf[24] = 0x00; minimal_elf[25] = 0x00; minimal_elf[26] = 0x40; minimal_elf[27] = 0x00; // Entry: 0x400000
    minimal_elf[40] = 52; // Header Boyutu

    // [DİKKAT]: SYSCALL(8) ft_strlen kullandığı için binary (içinde 0x00 olan) 
    // dosyalarda boyutu yanlış hesaplar! Bu yüzden doğrudan VFS API'sini kullanıyoruz:
    extern int fs_create_file(const char *, const uint8_t *, uint32_t, uint8_t);
    int cr_elf = fs_create_file(elf_name, minimal_elf, 52, 0);
    KTEST_ASSERT(cr_elf >= 0, "[STRICT] PROC-INT: Test icin gecerli minimal ELF diske yazildi");

    // Şimdi kendi oluşturduğumuz, şifre formatı ve boyutu kusursuz olan bu ELF'i çalıştıralım!
    // [MİMARİ YAMASI]: load_and_exec_elf gerçek bir PID değil, Task Tablosundaki (tasks[]) 
    // index (slot) numarasını döner. Karışıklığı önlemek için ismini 'array_index' yaptık.
    int array_index = load_and_exec_elf(elf_name);
    KTEST_ASSERT(array_index > 0, "[STRICT] PROC-INT: load_and_exec_elf diskten okuyup gecerli bir Array Index dondu");

    if (array_index > 0) {
        KTEST_ASSERT(tasks[array_index].uid == 0, "[STRICT] PROC-INT: Yeni surece dogru UID (Root) atandi");
        KTEST_ASSERT(tasks[array_index].fd_table[0].type != 0, "[STRICT] PROC-INT: Surece standart I/O FD'leri tahsis edildi");
    }
}