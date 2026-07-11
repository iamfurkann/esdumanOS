#include "pmm.h"
#include "stdio.h"
#include "kernel.h"

uint32_t pmm_bitmap[BITMAP_SIZE];
uint32_t used_memory = 0;

uint32_t actual_total_memory = 0; 

static uint32_t last_scanned_index = 0;
spinlock_t pmm_lock;

static uint32_t pmm_find_first_free(void) {
    for (int i = 0; i < BITMAP_SIZE; i++) {
        uint32_t idx = (last_scanned_index + i) % BITMAP_SIZE;
        
        if (pmm_bitmap[idx] != 0xFFFFFFFF) {
            for (int j = 0; j < 32; j++) {
                if (!(pmm_bitmap[idx] & (1U << j))) {
                    last_scanned_index = idx;
                    return (idx * 32) + j;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}

void init_pmm(multiboot_info_t *mboot_info) {
    spinlock_init(&pmm_lock);
    for (int i = 0; i < BITMAP_SIZE; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }

    int memory_map_found = 0;
    actual_total_memory = 0;

    if (mboot_info != 0) {
        printk("[PMM] Multiboot Flags Tespit Edildi: 0x%x\n", mboot_info->flags);
        if (mboot_info->flags & (1 << 6)) {
            multiboot_memory_map_t *mmap = (multiboot_memory_map_t *)mboot_info->mmap_addr;
            
            while ((uint32_t)mmap < mboot_info->mmap_addr + mboot_info->mmap_length) {
                if (mmap->type == 1) {
                    uint32_t start_addr = mmap->addr_low;
                    uint32_t length = mmap->len_low;
                    
                    for (uint32_t i = 0; i < length; i += PAGE_SIZE) {
                        uint32_t frame = (start_addr + i) / PAGE_SIZE;
                        if (frame < PMM_FRAMES_COUNT) {
                            pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
                            actual_total_memory += PAGE_SIZE;
                        }
                    }
                    memory_map_found = 1;
                }
                mmap = (multiboot_memory_map_t *)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
            }
            if (memory_map_found) {
                printk("[PMM] RAM haritasi basariyla MMAP (Bit 6) uzerinden okundu.\n");
            }
        }

        if (!memory_map_found && (mboot_info->flags & (1 << 0))) {
            actual_total_memory = (mboot_info->mem_upper * 1024) + (1024 * 1024);
            
            uint32_t frames_to_free = actual_total_memory / PAGE_SIZE;
            for (uint32_t i = 0; i < frames_to_free; i++) {
                if (i < PMM_FRAMES_COUNT) {
                    pmm_bitmap[i / 32] &= ~(1U << (i % 32));
                }
            }
            memory_map_found = 1;
            printk("[PMM] RAM haritasi MEM_UPPER (Bit 0) uzerinden okundu.\n");
        }
    }

    if (!memory_map_found) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[PMM KRITIK UYARI] Multiboot RAM verisi yok! Guvenlik icin RAM 16MB varsayiliyor.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        
        actual_total_memory = PMM_FALLBACK_MEMORY;
        uint32_t frames_to_free = actual_total_memory / PAGE_SIZE;
        for (uint32_t i = 0; i < frames_to_free; i++) {
            if (i < PMM_FRAMES_COUNT) {
                pmm_bitmap[i / 32] &= ~(1U << (i % 32));
            }
        }
    }

    used_memory = 0;
    for (int i = 0; i < 1024; i++) { 
        pmm_bitmap[i / 32] |= (1U << (i % 32));
    }

    for (uint32_t i = 0; i < actual_total_memory / PAGE_SIZE; i++) {
        if (pmm_bitmap[i / 32] & (1U << (i % 32))) {
            used_memory += PAGE_SIZE;
        }
    }
}

uint32_t pmm_alloc_frame(void) {
    spinlock_acquire(&pmm_lock);

    uint32_t frame = pmm_find_first_free();
    
    if (frame == 0xFFFFFFFF) {
        // Hata durumunda da kilidi bırakmayı unutma (Deadlock önlemi)
        spinlock_release(&pmm_lock);
        printk("KERNEL PANIC: Fiziksel Hafiza Doldu!\n");
        asm volatile("cli; hlt"); 
    }

    pmm_bitmap[frame / 32] |= (1U << (frame % 32));
    used_memory += PAGE_SIZE;
    
    spinlock_release(&pmm_lock);

    return frame * PAGE_SIZE;
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;

    if (frame < 1024) {
        printk("[PMM UYARISI] Kritik Kernel alanini (Frame %d) serbest birakma reddedildi!\n", frame);
        return; 
    }
    spinlock_acquire(&pmm_lock);

    pmm_bitmap[frame / 32] &= ~(1U << (frame % 32));
    used_memory -= PAGE_SIZE;
    if ((frame / 32) < last_scanned_index) {
        last_scanned_index = frame / 32;
    }
    spinlock_release(&pmm_lock);
}
uint32_t pmm_get_total_memory(void) {
    return actual_total_memory; 
}

uint32_t pmm_get_free_memory(void) {
    if (used_memory > actual_total_memory) return 0;
    return actual_total_memory - used_memory;
}