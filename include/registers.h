#ifndef REGISTERS_H
#define REGISTERS_H

#include "arch.h"

#ifdef ARCH_X86
    //x86 Context
    typedef struct {
        uint32_t ds;                                     // Data Segment
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pusha ile itilenler
        uint32_t int_no, err_code;                       // Interrupt no ve hata kodu
        uint32_t eip, cs, eflags, useresp, ss;           // İşlemcinin otomatik ittikleri
    } arch_regs_t;

#elif defined(ARCH_RISCV64)
    // RISC-V (64-bit) Context
    typedef struct {
        uint64_t ra, sp, gp, tp;                         // Temel işaretçiler
        uint64_t t0, t1, t2;                             // Geçici (Temporary)
        uint64_t s0, s1;                                 // Kaydedilen (Saved)
        uint64_t a0, a1, a2, a3, a4, a5, a6, a7;         // Argümanlar / Dönüş (a7 = syscall no)
        uint64_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11; // Diğer Saved registerlar
        uint64_t t3, t4, t5, t6;                         // Diğer Temporary registerlar
        uint64_t sepc, sstatus, scause, stval;           // Supervisor CSR'lar (Trap durumu)
    } arch_regs_t;

#endif

#endif