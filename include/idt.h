#ifndef IDT_H
#define IDT_H

#include "types.h"

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));
typedef struct idt_entry idt_entry_t;

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
typedef struct idt_ptr idt_ptr_t;

void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
extern void load_idt(uint32_t idt_ptr);

#endif