#ifndef TSS_H
#define TSS_H

#include "types.h"

/**
 * @brief Task State Segment (TSS) entry structure defining a hardware task
 */
typedef struct tss_entry_struct {
    uint32_t prev_tss;
    uint32_t esp0;       // Stack Pointer to be used when transitioning to Ring 0
    uint32_t ss0;        // Stack Segment to be used when transitioning to Ring 0
    uint32_t esp1;       
    uint32_t ss1;
    uint32_t esp2;       
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

/**
 * @brief Initializes and installs the Task State Segment
 * @param num Index in the GDT where the TSS will be placed
 * @param ss0 Stack Segment for Ring 0
 * @param esp0 Stack Pointer for Ring 0
 */
void tss_install(int32_t num, uint16_t ss0, uint32_t esp0);

/**
 * @brief Updates the kernel stack pointer in the TSS
 * @param stack New kernel stack pointer value
 */
void set_kernel_stack(uint32_t stack);

#endif