#ifndef PMM_H
#define PMM_H

#include "types.h"
#include "multiboot.h"

#define PMM_FRAME_SIZE 4096

void init_pmm(multiboot_info_t *mboot_info);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t addr);

uint32_t pmm_get_total_memory(void);
uint32_t pmm_get_free_memory(void);

#endif