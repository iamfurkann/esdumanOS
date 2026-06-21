#include "pmm.h"
#include "stdio.h"

uint32_t pmm_bitmap[BITMAP_SIZE];
uint32_t used_memory = 0;

static uint32_t last_scanned_index = 0;

static uint32_t pmm_find_first_free(void) {
    for (int i = 0; i < 1024; i++) {
        uint32_t idx = (last_scanned_index + i) % BITMAP_SIZE;
        if (pmm_bitmap[i] != 0xFFFFFFFF) { 
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[i] & (1U << j))) {
                    last_scanned_index = idx;
                    return (i * 32) + j;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

void init_pmm(multiboot_info_t *mboot_info) {
    (void)mboot_info;

    for (int i = 0; i < BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    for (int i = 0; i < PMM_FRAMES_COUNT; i++) {
        pmm_bitmap[i / 32] &= ~(1U << (i % 32));
    }

    for (int i = 0; i < 1024; i++) {
        pmm_bitmap[i / 32] |= (1U << (i % 32));
    }

    used_memory = 1024 * PAGE_SIZE;
}

uint32_t pmm_alloc_frame(void) {
    uint32_t frame = pmm_find_first_free();
    
    if (frame == 0xFFFFFFFF) {
        printk("KERNEL PANIC: Fiziksel Hafiza Doldu!\n");
        asm volatile("cli; hlt"); 
    }

    pmm_bitmap[frame / 32] |= (1U << (frame % 32));
    used_memory += PAGE_SIZE;
    
    return frame * PAGE_SIZE; 
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;
    pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
    used_memory -= PAGE_SIZE;

    if ((frame / 32) < last_scanned_index) {
        last_scanned_index = frame / 32;
    }
}

uint32_t pmm_get_total_memory(void) {
    return PMM_TOTAL_MEMORY; 
}
uint32_t pmm_get_free_memory(void) {
    return PMM_TOTAL_MEMORY - used_memory;
}