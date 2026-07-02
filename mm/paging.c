#include "paging.h"
#include "pmm.h"
#include "stdio.h"

uint32_t *page_directory;

void init_paging(void) {
    page_directory = (uint32_t *)pmm_alloc_frame();

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = PAGE_NOT_PRESENT;
    }

    for (int i = 0; i < 4; i++) {
        uint32_t *page_table = (uint32_t *)pmm_alloc_frame();
        for (int j = 0; j < 1024; j++) {
            uint32_t phys_addr = (i * 0x400000) + (j * PAGE_SIZE);
            if (phys_addr < 0x100000) {
                page_table[j] = phys_addr | PAGE_KERNEL_ONLY;
            } else {
                page_table[j] = phys_addr | PAGE_USER_ACCESS;
            }
        }
        page_directory[i] = ((uint32_t)page_table) | PAGE_USER_ACCESS;
    }

    // Fractal Mapping
    page_directory[1023] = ((uint32_t)page_directory) | PAGE_KERNEL_ONLY;

    load_page_directory((uint32_t *)page_directory);
    enable_paging();
}

void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = virtual_addr >> 22;
    uint32_t pt_index = (virtual_addr >> 12) & 0x3FF;

    uint32_t *pd_virt = (uint32_t *)RECURSIVE_PD_VADDR;
    uint32_t *pt_virt = (uint32_t *)(RECURSIVE_PT_VADDR + (pd_index * PAGE_SIZE));

    if ((pd_virt[pd_index] & 1) == 0) {
        uint32_t new_table_phys = pmm_alloc_frame();
        pd_virt[pd_index] = new_table_phys | PAGE_USER_ACCESS;
        asm volatile("invlpg (%0)" ::"r"(pt_virt) : "memory");
        for (int i = 0; i < 1024; i++) {
            pt_virt[i] = 0;
        }
    }

    pt_virt[pt_index] = physical_addr | flags;
    asm volatile("invlpg (%0)" ::"r"(virtual_addr) : "memory");
}

uint32_t clone_page_directory(void) {
    uint32_t new_pd_phys = pmm_alloc_frame();
    if (new_pd_phys == 0xFFFFFFFF) {
        printk("PANIC: Klonlama icin fiziksel RAM kalmadi!\n");
        return 0;
    }

    uint32_t eflags;
    asm volatile("pushf; pop %0" : "=r"(eflags));
    asm volatile("cli");

    map_page(TEMP_MAP_VADDR, new_pd_phys, PAGE_KERNEL_ONLY);

    uint32_t *new_pd = (uint32_t *)TEMP_MAP_VADDR;
    uint32_t *current_pd = (uint32_t *)RECURSIVE_PD_VADDR;

    for (int i = 0; i < 1024; i++) {
        if (i < 4) { // Kernel Identity Map
            new_pd[i] = current_pd[i];
        }
        else if (i >= 768 && i < 1023) { // Kernel Higher Half
            new_pd[i] = current_pd[i];
        } 
        else if (i == 1023) { // Fractal Mapping pointer
            new_pd[i] = new_pd_phys | PAGE_KERNEL_ONLY;
        } 
        else { // Kullanıcı Boş Alanları
            new_pd[i] = PAGE_NOT_PRESENT;
        }
    }

    if (eflags & 0x200) {
        asm volatile("sti");
    }

    return new_pd_phys; 
}