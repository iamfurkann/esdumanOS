#ifndef ARCH_H
#define ARCH_H

#include "types.h"

#ifdef ARCH_X86
    // x86
    #define ARCH_WORD_SIZE 32
    
    typedef uint32_t arch_word_t;
    typedef uint32_t arch_paddr_t; // Fiziksel Adres
    typedef uint32_t arch_vaddr_t; // Sanal Adres

#elif defined(ARCH_RISCV64)
    // RISC-V (64-bit)
    #define ARCH_WORD_SIZE 64
    
    typedef uint64_t arch_word_t;
    typedef uint64_t arch_paddr_t; // Fiziksel Adres
    typedef uint64_t arch_vaddr_t; // Sanal Adres

#else
    #error "HATA: Mimari secilmedi! Makefile icinde -DARCH_X86 veya -DARCH_RISCV64 tanimlayin."
#endif

#endif