#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "multiboot.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PMM_FRAMES_COUNT   32768
#define BITMAP_SIZE        1024
#define PMM_TOTAL_MEMORY   134217728
#define PMM_FALLBACK_MEMORY (16 * 1024 * 1024)

void init_pmm(multiboot_info_t *mboot_info);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t addr);

uint32_t pmm_get_total_memory(void);
uint32_t pmm_get_free_memory(void);

#endif