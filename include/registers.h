#ifndef REGISTERS_H
#define REGISTERS_H

#include "arch.h"

#ifdef ARCH_X86
    /**
     * @brief CPU registers state for x86 architecture during interrupts
     */
    typedef struct {
        uint32_t ds;                                     // Data Segment
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
        uint32_t int_no, err_code;                       // Interrupt number and error code
        uint32_t eip, cs, eflags, useresp, ss;           // Automatically pushed by the processor
    } arch_regs_t;

#elif defined(ARCH_RISCV64)
    /**
     * @brief CPU registers state for RISC-V (64-bit) architecture during interrupts
     */
    typedef struct {
        uint64_t ra, sp, gp, tp;                         // Base pointers
        uint64_t t0, t1, t2;                             // Temporary
        uint64_t s0, s1;                                 // Saved
        uint64_t a0, a1, a2, a3, a4, a5, a6, a7;         // Arguments / Return (a7 = syscall no)
        uint64_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11; // Other saved registers
        uint64_t t3, t4, t5, t6;                         // Other temporary registers
        uint64_t sepc, sstatus, scause, stval;           // Supervisor CSRs (Trap state)
    } arch_regs_t;

#endif

#endif