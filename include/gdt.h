#ifndef GDT_H
# define GDT_H

#include "types.h"

//KERNEL VE USER SEGMENT SELECTOR MACROLARI
#define GDT_KERNEL_CS 0x08 // Ring 0 Kod Segmenti
#define GDT_KERNEL_DS 0x10 // Ring 0 Veri Segmenti
#define GDT_USER_CS   0x23 // Ring 3 Kod Segmenti (0x20 + RPL 3)
#define GDT_USER_DS   0x2B // Ring 3 Veri Segmenti (0x28 + RPL 3)

#define EFLAGS_DEFAULT 0x202 // IF (Interrupt Enable)

struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

void init_gdt(void);
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif