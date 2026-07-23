#ifndef IDT_H
#define IDT_H

#include "types.h"

/**
 * @brief Interrupt Descriptor Table entry structure defining a single interrupt gate
 */
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));
typedef struct idt_entry idt_entry_t;

/**
 * @brief Interrupt Descriptor Table pointer structure used for loading the IDTR register
 */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
typedef struct idt_ptr idt_ptr_t;

/**
 * @brief Initializes the Interrupt Descriptor Table
 */
void init_idt(void);

/**
 * @brief Configures a specific gate in the Interrupt Descriptor Table
 * @param num Index of the IDT entry to configure
 * @param base Base address of the interrupt handler
 * @param sel Segment selector for the interrupt handler
 * @param flags Access flags for the interrupt gate
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

/**
 * @brief Loads the IDT pointer into the IDTR register
 * @param idt_ptr Address of the IDT pointer structure
 */
extern void load_idt(uint32_t idt_ptr);

#endif