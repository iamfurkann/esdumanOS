#ifndef ISR_H
#define ISR_H

#include "types.h"
#include "registers.h"

/*
 * typedef struct {
 *     uint32_t ds;                                     // Last pushed DS
 *     uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // pusha order (Reverse)
 *     uint32_t int_no, err_code;                       // Pushed by the ISR Macro
 *     uint32_t eip, cs, eflags, useresp, ss;           // Automatically pushed by the processor
 * } registers_t;
 */

/**
 * @brief Mutex structure for process synchronization
 */
typedef struct {
    uint32_t locked;
    int owner_pid;
} mutex_t;

/**
 * @brief Main handler for Interrupt Service Routines (ISRs)
 * @param regs Pointer to the saved CPU registers
 */
void isr_handler(arch_regs_t *regs);

/**
 * @brief Scheduler function to context switch between processes
 * @param regs Pointer to the saved CPU registers
 */
void schedule(arch_regs_t *regs);

/**
 * @brief Cleans up general purpose registers
 */
void clean_registers(void);

/**
 * @brief Saves the current stack state before initiating a kernel panic
 */
void save_stack_before_panic(void);


// --- Added by Refactor Script ---
extern void isr12(void);
extern void isr16(void);
extern void isr20(void);
extern void isr24(void);
extern void isr28(void);
extern void isr32(void);
extern void isr34(void);
extern void isr38(void);
extern void isr42(void);
extern void isr46(void);
extern void isr128(void);


// --- Added by Refactor Script 2 ---
extern void isr0(void);
extern void isr4(void);
extern void isr8(void);
extern void timer_interrupt_handler(void);
extern void syscall_handler(arch_regs_t *regs);
extern void ata_irq_handler(void);

#endif