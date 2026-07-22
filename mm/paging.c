#include "paging.h"
#include "pmm.h"
#include "stdio.h"
#include "klog.h"
#include "errno.h"

extern uint32_t __bss_end;

uint32_t *page_directory;

void init_paging(void) {
    klog(LOG_LEVEL_INFO, "VMM", "Sanal Bellek Yoneticisi (Paging) baslatiliyor...");
    page_directory = (uint32_t *)pmm_alloc_frame();
    if ((uint32_t)page_directory == 0xFFFFFFFF) {
        kernel_panic("Paging initialization failed: OOM");
    }

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = PAGE_NOT_PRESENT;
    }
    
    for (int i = 0; i < 4; i++) {
        uint32_t *page_table = (uint32_t *)pmm_alloc_frame();
        for (int j = 0; j < 1024; j++) {
            uint32_t phys_addr = (i * 0x400000) + (j * PAGE_SIZE);
            page_table[j] = phys_addr | PAGE_KERNEL_ONLY; 
        }
        page_directory[i] = ((uint32_t)page_table) | PAGE_KERNEL_ONLY;
    }
    
    page_directory[1023] = ((uint32_t)page_directory) | PAGE_KERNEL_ONLY;

    load_page_directory((uint32_t *)page_directory);
    enable_paging();
}

int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    if ((pd_virt[pd_index] & 1) == 0) {
        uint32_t new_table_phys = pmm_alloc_frame();
        if (new_table_phys == 0xFFFFFFFF) { 
            klog(LOG_LEVEL_ERROR, "VMM", "Page table olusturulamadi: RAM tukendi.");
            return E_NOMEM;
        }
        
        pd_virt[pd_index] = new_table_phys | PAGE_USER_ACCESS;
        asm volatile("invlpg (%0)" ::"r"(pt_virt) : "memory");
        for (int i = 0; i < 1024; i++) pt_virt[i] = 0;
    }

    if (pt_virt[pt_index] & 1) {
        uint32_t old_phys = pt_virt[pt_index] & 0xFFFFF000;
        uint32_t new_phys = physical_addr & 0xFFFFF000;
        if (old_phys != new_phys) {
            klog(LOG_LEVEL_ERROR, "PMM", "Sanal bellek cakismasi tespit edildi.");
            return E_BUSY;
        }
    }

    pt_virt[pt_index] = (physical_addr & 0xFFFFF000) | (flags & 0xFFF);
    asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory");
    return E_OK;
}

void unmap_page(uint32_t virtual_addr) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    if (pd_virt[pd_index] & 1) {
        pt_virt[pt_index] = 0; 
        asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory"); 
    }
}

uint32_t clone_page_directory(void) {
    klog(LOG_LEVEL_DEBUG, "VMM", "Yeni surec (Process) icin Page Directory klonlaniyor.");
    uint32_t new_pd_phys = pmm_alloc_frame();
    if (new_pd_phys == 0xFFFFFFFF) {
        klog(LOG_LEVEL_ERROR, "PMM", "PD klonlanamadi: RAM tukendi.");
        return E_NOMEM;
    }

    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    int res = map_page(TEMP_MAP_VADDR, new_pd_phys, PAGE_KERNEL_ONLY);
    if (res != E_OK) {
        klog(LOG_LEVEL_ERROR, "PMM", "Klonlama sirasinda gecici esleme basarisiz.");
        
        // [GÜVENLİK YAMASI 1]: Interrupt Sızıntısı Düzeltildi
        // Çıkış yapmadan önce, kesmeler önceden açıksa mutlaka geri aç!
        if (eflags & 0x200) {
            asm volatile("sti");
        }
        
        // [GÜVENLİK YAMASI 2]: Memory Leak (Bellek Sızıntısı) Düzeltildi
        // İşlem iptal edildiği için ayrılan fiziksel sayfayı geri ver.
        extern void pmm_free_frame(uint32_t);
        pmm_free_frame(new_pd_phys);
        
        return E_NOMEM;
    }

    uint32_t *new_pd = (uint32_t *)TEMP_MAP_VADDR;
    uint32_t *current_pd = (uint32_t *)RECURSIVE_PD_VADDR;

    for (int i = 0; i < 1024; i++) {
        if (i < 4) {
            new_pd[i] = (current_pd[i] & ~0x04) | PAGE_KERNEL_ONLY;
        }
        else if (i >= 768 && i < 1023) {
            new_pd[i] = (current_pd[i] & ~0x04) | PAGE_KERNEL_ONLY;
        } 
        else if (i == 1023) {
            new_pd[i] = new_pd_phys | PAGE_KERNEL_ONLY;
        } 
        else {
            new_pd[i] = PAGE_NOT_PRESENT;
        }
    }

    if (eflags & 0x200) {
        asm volatile("sti");
    }

    unmap_page(TEMP_MAP_VADDR);

    return new_pd_phys; 
}