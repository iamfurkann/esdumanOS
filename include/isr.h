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


/*
 * Assembly interrupt stubs, from arch/x86/cpu/idt_s.asm.
 *
 * Vectors 0-31 are the CPU exceptions, 32-47 the remapped PIC IRQs, and 128 the
 * syscall gate. These were split arbitrarily: fourteen of them were declared
 * here and the other thirty-five in idt.c, in two blocks a refactor script had
 * appended at different times. The two sets were exactly complementary, which is
 * the giveaway - nobody chose the split, it was whatever happened to be
 * undeclared when each pass ran. idt.c is the only consumer and it already
 * includes this header.
 */
extern void isr0(void);   extern void isr1(void);   extern void isr2(void);
extern void isr3(void);   extern void isr4(void);   extern void isr5(void);
extern void isr6(void);   extern void isr7(void);   extern void isr8(void);
extern void isr9(void);   extern void isr10(void);  extern void isr11(void);
extern void isr12(void);  extern void isr13(void);  extern void isr14(void);
extern void isr15(void);  extern void isr16(void);  extern void isr17(void);
extern void isr18(void);  extern void isr19(void);  extern void isr20(void);
extern void isr21(void);  extern void isr22(void);  extern void isr23(void);
extern void isr24(void);  extern void isr25(void);  extern void isr26(void);
extern void isr27(void);  extern void isr28(void);  extern void isr29(void);
extern void isr30(void);  extern void isr31(void);

/* IRQ0-IRQ15, remapped to vectors 32-47 by pic_remap(). */
extern void isr32(void);  extern void isr33(void);  extern void isr34(void);
extern void isr35(void);  extern void isr36(void);  extern void isr37(void);
extern void isr38(void);  extern void isr39(void);  extern void isr40(void);
extern void isr41(void);  extern void isr42(void);  extern void isr43(void);
extern void isr44(void);  extern void isr45(void);  extern void isr46(void);
extern void isr47(void);

/* The syscall gate, DPL 3. */
extern void isr128(void);

/*
 * Handlers the stubs dispatch into. They are defined in their own subsystems -
 * timer.c, syscall.c and ata.c - rather than in the ISR module, but this is the
 * header that owns the interrupt vectors, so they are declared with them.
 */
extern void timer_interrupt_handler(void);
extern void syscall_handler(arch_regs_t *regs);
extern void ata_irq_handler(void);

#endif