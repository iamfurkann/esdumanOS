#include "paging.h"
#include "pmm.h"
#include "stdio.h"

uint32_t *page_directory;

void init_paging(void) {
    // 1. Page Directory (Sayfa Dizini) tahsis et
    page_directory = (uint32_t *)pmm_alloc_frame();

    // Dizin içindeki tüm kayıtları temizle (Not Present, Read/Write, Supervisor)
    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }
    
    // 2. İlk 16 MB (4 Sayfa Tablosu x 4MB) için Identity Map yapıyoruz.
    // Bu, Kernel'in ve PMM'den tahsis edilen page table'ların hatasız çalışmasını sağlar.
    for (int i = 0; i < 4; i++) {
        uint32_t *page_table = (uint32_t *)pmm_alloc_frame();
        for (int j = 0; j < 1024; j++) {
            // Adresleri 1'e 1 eşliyoruz (Virtual == Physical)
            page_table[j] = ((i * 0x400000) + (j * 0x1000)) | (PAGE_PRESENT | PAGE_RW | PAGE_USER);
        }
        page_directory[i] = ((uint32_t)page_table) | 7;
    }

    load_page_directory((uint32_t *)page_directory);
    enable_paging();
}

// Sanal adresi, fiziksel adrese bağlayan dinamik haritalama fonksiyonu
void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    // İlgili bölgede henüz bir Page Table yoksa yarat
    if ((page_directory[pd_index] & 1) == 0) {
        uint32_t *new_table = (uint32_t *)pmm_alloc_frame();
        
        // Tabloyu temizle (Fiziksel adresi doğrudan kullanabiliriz çünkü ilk 16MB mapli)
        for (int i = 0; i < 1024; i++) {
            new_table[i] = 0;
        }
        page_directory[pd_index] = ((uint32_t)new_table) | flags;
    }

    // Page Table adresini al ve fiziksel sayfayı içine yaz
    uint32_t *page_table = (uint32_t *)(page_directory[pd_index] & ~0xFFF);
    page_table[pt_index] = physical_addr | flags;

    // İşlemci TLB (Cache) güncellemesi
    asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory");
}