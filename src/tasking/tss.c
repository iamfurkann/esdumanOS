#include "tss.h"
#include "gdt.h"

// Global TSS degiskenimiz
tss_entry_t tss_entry;

// TSS'i ayarlar ve gdt_set_gate fonksiyonunu cagirarak GDT'ye kaydeder
void tss_install(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = base + sizeof(tss_entry_t);

    // TSS icin GDT kaydini ekle (0xE9 = Present, Ring 3, 32-bit TSS)
    gdt_set_gate(num, base, limit, 0xE9, 0x00);

    // TSS'in icini tamamen sifirla (Cöplerden kurtul)
    uint8_t *tss_ptr = (uint8_t *)&tss_entry;
    for (uint32_t i = 0; i < sizeof(tss_entry_t); i++) {
        tss_ptr[i] = 0;
    }

    tss_entry.ss0  = ss0;  // Kernel Stack Segmenti
    tss_entry.esp0 = esp0; // Kernel Stack Pointer (Interrupt gelince buraya atlayacak)

    // IO Haritasi (Islemcinin TSS'in nerede bittigini anlamasi icin gerekli)
    tss_entry.iomap_base = sizeof(tss_entry_t);
}

// Bu fonksiyonu User Mode'dan Kernel Mode'a gecerken (Task switch) kullanacagiz
void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}