#ifndef PAGING_H
#define PAGING_H

#include "types.h"

#define PAGE_PRESENT 0x01
#define PAGE_RW 0x02
#define PAGE_USER 0x04

void init_paging(void);
void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
//asm
extern void load_page_directory(uint32_t* dir);
extern void enable_paging(void);

#endif 