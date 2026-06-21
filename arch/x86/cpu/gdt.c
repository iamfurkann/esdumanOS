#include "gdt.h"
#include "tss.h"  // TSS'i cagirabilmek icin ekledik
#include "stdio.h"

gdt_entry_t *gdt_entries = (gdt_entry_t *)0x00000800;
gdt_ptr_t gdt_ptr;

extern void gdt_flush(uint32_t);
extern void tss_flush(void); // Assembly'deki TSS yukleyici

// static kelimesini kaldirdik, artik tss.c bu fonksiyonu kullanabilir
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

void init_gdt(void) {
    // Artik 8 elemanimiz var (7 segment + 1 TSS)
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 8) - 1;
    gdt_ptr.base = 0x00000800;

    gdt_set_gate(0, 0, 0, 0, 0); // Null Segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Kernel Code
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel Data
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel Stack
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User Code
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User Data
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User Stack

    // 7. Indeks olarak TSS'i kuruyoruz (tss.c icindeki fonksiyon)
    // ss0 = 0x10 (Kernel Data Segment)
    tss_install(7, 0x10, 0x0);

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush(); // Islemciye TSS'i tanit
}