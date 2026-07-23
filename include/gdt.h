#ifndef GDT_H
# define GDT_H

#include "types.h"

/**
 * @brief KERNEL AND USER SEGMENT SELECTOR MACROS
 */
#define GDT_KERNEL_CS 0x08 // Ring 0 Code Segment
#define GDT_KERNEL_DS 0x10 // Ring 0 Data Segment
#define GDT_USER_CS   0x23 // Ring 3 Code Segment (0x20 + RPL 3)
#define GDT_USER_DS   0x2B // Ring 3 Data Segment (0x28 + RPL 3)

/**
 * @brief Default EFLAGS value with Interrupt Enable (IF) set
 */
#define EFLAGS_DEFAULT 0x202 // IF (Interrupt Enable)

/**
 * @brief Global Descriptor Table entry structure defining a single memory segment
 */
struct gdt_entry_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

/**
 * @brief Global Descriptor Table pointer structure used for loading the GDT register
 */
struct gdt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

/**
 * @brief Initializes the Global Descriptor Table
 */
void init_gdt(void);

/**
 * @brief Configures a specific gate in the Global Descriptor Table
 * @param num Index of the GDT entry to configure
 * @param base Base address of the segment
 * @param limit Limit of the segment
 * @param access Access flags for the segment
 * @param gran Granularity flags for the segment
 */
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif