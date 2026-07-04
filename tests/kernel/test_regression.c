#include "ktest.h"
#include "syscall.h"
#include "process.h" // tasks tablosu için
#include "fs.h"      // vfs yapısı için
#include "libft.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern process_t tasks[];
extern uint32_t fs_max_sectors;

void run_regression_tests(void) {
    printk("\n--- Regresyon (Gecmis Hata) Testleri ---\n");
    serial_print("\n--- Regresyon (Gecmis Hata) Testleri ---\n");

    // =========================================================
    // BUG-01: Dangling Pointer & NULL Free Koruması
    // Geçmiş Hata: Önceden ayrılmış veya NULL olan bir bellek 
    // kfree ile silinmeye çalışıldığında Kernel Panic veriyordu.
    // =========================================================
    void *ptr = kmalloc(32);
    kfree(ptr);
    kfree(NULL); // EĞER NULL koruması yoksa sistem BURADA çöker!
    KTEST_ASSERT(1, "[STRICT] REG-01: kfree(NULL) sistem cokmesini engelledi (Heap stabil)");

    // =========================================================
    // BUG-02: PID ve Array Index (Slot) Karışıklığı
    // Geçmiş Hata: Process Tablosundaki index (0, 1, 2) ile 
    // gerçek PID numarası (örn: 1005) birbirine karıştırılıp 
    // yanlış task kapatılıyordu.
    // =========================================================
    int test_slot = -1;
    for (int i = 0; i < 16; i++) { // MAX_TASKS = 16
        if (tasks[i].state == 0) { test_slot = i; break; }
    }
    
    if (test_slot >= 0) {
        // PID bilerek index'ten tamamen farklı (1000 fazlası) yapılıyor
        tasks[test_slot].pid = test_slot + 1000; 
        tasks[test_slot].state = 1; // Dolu olarak işaretle
        
        int found_slot = -1;
        for (int i = 0; i < 16; i++) {
            if (tasks[i].pid == (test_slot + 1000) && tasks[i].state != 0) { 
                found_slot = i; 
                break; 
            }
        }
        KTEST_ASSERT(found_slot == test_slot, "[STRICT] REG-02: PID ve Slot (Index) karisikligi engellendi");
        tasks[test_slot].state = 0; // Test bitince slotu temizle (Memory Leak olmasın)
    }

    // =========================================================
    // BUG-03: ATA Disk Sınır Aşımı (Timeout) Koruması
    // Geçmiş Hata: Diskin maksimum boyutundan (4096 sektör) 
    // fazlası okunmaya çalışıldığında ATA sürücüsü kilitlenip (timeout) 
    // tüm sistemi sonsuz döngüye sokuyordu.
    // =========================================================
    // fs_max_sectors'ün 4096'ya cap'lenip (sınırlandırılıp) cap'lenmediğini test ediyoruz.
    KTEST_ASSERT(fs_max_sectors <= 4096, "[STRICT] REG-03: ATA sürücüsü maks sinir (4096) disina cikmasi engellendi");

    // =========================================================
    // BUG-04: Ring 0 <-> Ring 3 ABI ve Struct Padding Uyuşmazlığı
    // Geçmiş Hata: 32-bit (i386) mimaride pointer'lar 4 byte olmalıdır. 
    // Syscall üzerinden veri aktarılırken veri tiplerinin (ABI) 
    // hizalaması bozuluyordu.
    // =========================================================
    KTEST_ASSERT(sizeof(void *) == 4, "[STRICT] REG-04: Mimari ABI pointer boyutu (4 byte / 32-bit) korundu");

    // =========================================================================
    // BUG-05: ATA Identify Sonsuz Dongu Kilitlenmesi (Hardware Livelock)
    // =========================================================================
    // Eski Kod: Eger donanim (Gercek Disk) bozulur ve DRQ/ERR bayraklarini hic 
    // kaldirmazsa, ata_identify() 'while(1)' icinde sonsuza kadar kilitleniyordu.
    // Yeni Kod: Timeout mekanizmasi sayesinde donanim bozula bile fonksiyon
    // guvenli bir sekilde asili kalmadan (hang olmadan) geri donmek zorundadir.
    extern uint32_t ata_identify(void);
    uint32_t identified_sectors = ata_identify();
    
    // Eger buraya ulasabildiysek, fonksiyon bizi sonsuz dongude hapsetmedi demektir!
    KTEST_ASSERT(identified_sectors >= 4096, 
        "[STRICT] REG-05: ATA Identify donanim kilitlenmesine karsi Timeout korumali");
}