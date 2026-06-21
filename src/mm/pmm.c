#include "pmm.h"
#include "stdio.h"

uint32_t pmm_bitmap[1024];
uint32_t used_memory = 0;

static uint32_t pmm_find_first_free(void) {
    for (int i = 0; i < 1024; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) { 
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1U << j))) {
                    return (i * 32) + j;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

void init_pmm(multiboot_info_t *mboot_info) {
    (void)mboot_info;

    for (int i = 0; i < 1024; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    for (int i = 0; i < 32768; i++) {
        pmm_bitmap[i / 32] &= ~(1U << (i % 32));
    }

    for (int i = 0; i < 1024; i++) {
        pmm_bitmap[i / 32] |= (1U << (i % 32));
    }

    used_memory = 4194304;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame = pmm_find_first_free();
    
    if (frame == 0xFFFFFFFF) {
        printk("KERNEL PANIC: Fiziksel Hafiza Doldu!\n");
        asm volatile("cli; hlt"); 
    }

    pmm_bitmap[frame / 32] |= (1U << (frame % 32));
    used_memory += 4096;
    
    return frame * 4096; 
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / 4096;
    pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
    used_memory -= 4096;
}

uint32_t pmm_get_total_memory(void) {
    return 134217728; 
}
uint32_t pmm_get_free_memory(void) {
    return 134217728 - used_memory;
}