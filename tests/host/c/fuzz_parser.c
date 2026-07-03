#include "elf.h"

// =========================================================================
// libFuzzer Giriş Noktası
// LLVM, bu fonksiyona saniyede binlerce kez rastgele Data ve Size gönderir.
// Amacımız: Bu bozuk verilerle Kernel'in çöküp çökmediğini görmek!
// =========================================================================
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    
    // ---------------------------------------------------------
    // TEST 1: VFS Dosya Adı (Filename) Ayrıştırma Fuzzing'i
    // ---------------------------------------------------------
    // VFS'e aşırı uzun veya bozuk karakterli dosya isimleri gelirse ne olur?
    if (Size > 0 && Size < 500) {
        char filename[256];
        // Maksimum 255 karaktere kadar kopyalamaya çalış
        size_t copy_size = Size < 255 ? Size : 255;
        for(size_t i = 0; i < copy_size; i++) {
            filename[i] = Data[i];
        }
        filename[copy_size] = '\0';
        
        // Burada isimdeki karakterleri simüle eden bir VFS döngüsü:
        volatile int valid = 1;
        for (size_t i = 0; i < copy_size; i++) {
            if (filename[i] < 32 || filename[i] > 126) {
                valid = 0; // Basılamayan (non-printable) karakter koruması
            }
        }
    }

    // ---------------------------------------------------------
    // TEST 2: ELF Header ve Program Header (Phdr) Fuzzing'i
    // ---------------------------------------------------------
    // Senin load_and_exec_elf() fonksiyonundaki o tehlikeli header okuma kısmı.
    // Fuzzer, Magic Number'ı doğru verip offset'leri bozuk verirse Paging çöker mi?
    if (Size >= sizeof(elf32_ehdr_t)) {
        elf32_ehdr_t *ehdr = (elf32_ehdr_t *)Data;
        
        // Sadece "ELF" başlangıcına sahip rastgele verilerde derinlemesine in!
        if (ehdr->e_ident[0] == 0x7F && ehdr->e_ident[1] == 'E' &&
            ehdr->e_ident[2] == 'L' && ehdr->e_ident[3] == 'F') {
            
            uint32_t phoff = ehdr->e_phoff;  // Program Header Offset
            uint16_t phnum = ehdr->e_phnum;  // Kaç tane segment var? (Örn: Fuzzer burayı 65000 yapabilir!)
            
            // phnum kadar dönmeye çalış (Integer Overflow veya Out-of-Bounds Read testi)
            // Eğer Kernel'inde boyut kontrolü (Size Check) yoksa libFuzzer anında ASan ile çökertecek!
            for (int i = 0; i < phnum; i++) {
                uint32_t chunk_size = i * sizeof(elf32_phdr_t);
                uint32_t offset = phoff + chunk_size;
                
                // GÜVENLİ SINIR KONTROLÜ (Integer Overflow Korumalı)
                // 1. offset < phoff -> Toplama işlemi sınırları aşıp sıfırlandı mı? (Overflow check)
                // 2. offset >= Size -> Offset, dosya boyutundan büyük mü?
                // 3. Size - offset < sizeof(elf32_phdr_t) -> Kalan alan 32 byte okumak için yeterli mi?
                if (offset < phoff || offset >= Size || Size - offset < sizeof(elf32_phdr_t)) {
                    break; // Hacker saldırısı tespit edildi! İşlemi iptal et.
                }

                // Eger buraya ulaştıysa, matematiksel olarak %100 güvenlidir.
                elf32_phdr_t *phdr = (elf32_phdr_t *)(Data + offset);
                
                volatile uint32_t type = phdr->p_type; 
                volatile uint32_t vaddr = phdr->p_vaddr;
                (void)type;
                (void)vaddr;
            }
        }
    }

    return 0; // 0 Dönmesi Fuzzer için "Sistem Çökmedi, harika!" demektir.
}