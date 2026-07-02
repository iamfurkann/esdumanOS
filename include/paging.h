#ifndef PAGING_H
#define PAGING_H

#include "types.h"

//SANAL BELLEK ADRES MAKTOLARI
#define RECURSIVE_PD_VADDR 0xFFFFF000 // Fractal haritalama (Page Directory kendi üzerinde)
#define RECURSIVE_PT_VADDR 0xFFC00000 // Page Table başlangıç adresi
#define USER_STACK_TOP     0xBFFFF000 // Ring 3 Kullanıcı yığını (stack) başlangıcı
#define TEMP_MAP_VADDR     0xE0000000 // Klonlama esnasında kullanılan geçici adres
#define PAGE_SIZE          4096       // Standart sayfa boyutu (4 KB)

// Page Table/Directory Flags
#define PAGE_KERNEL_ONLY   3          // Present=1, RW=1, User=0
#define PAGE_USER_ACCESS   7          // Present=1, RW=1, User=1
#define PAGE_NOT_PRESENT   2          // Present=0, RW=1
void init_paging(void);
void map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
//asm
extern void load_page_directory(uint32_t* dir);
extern void enable_paging(void);

#endif 