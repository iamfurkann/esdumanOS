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

/**
 * @brief The main TSS.
 *
 * After a switch through the double fault task gate this holds the state of the
 * task that faulted, because the processor saves the outgoing context into the
 * TSS that TR pointed at.
 */
extern tss_entry_t tss_entry;

/**
 * @brief The TSS the processor switches to on a double fault.
 */
extern tss_entry_t df_tss;

/**
 * @brief Installs the double fault handler as a hardware task.
 *
 * Must be called after paging is up: the TSS carries the CR3 the handler will
 * run under, and that value is only meaningful once the kernel page directory
 * exists.
 */
void init_double_fault_handler(void);

/**
 * @brief Tests whether an address lies inside the dedicated double fault stack.
 *
 * @param addr Address to test.
 * @return 1 when the address is inside that stack, 0 otherwise.
 */
int df_stack_contains(uint32_t addr);

#endif