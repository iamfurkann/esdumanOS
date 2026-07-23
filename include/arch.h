#ifndef ARCH_H
#define ARCH_H

#include "types.h"

#ifdef ARCH_X86
    /**
     * @brief Architecture word size for x86 (32-bit)
     */
    #define ARCH_WORD_SIZE 32
    
    /**
     * @brief Architecture-specific word type
     */
    typedef uint32_t arch_word_t;
    
    /**
     * @brief Architecture-specific physical address type
     */
    typedef uint32_t arch_paddr_t; // Physical Address
    
    /**
     * @brief Architecture-specific virtual address type
     */
    typedef uint32_t arch_vaddr_t; // Virtual Address

#elif defined(ARCH_RISCV64)
    /**
     * @brief Architecture word size for RISC-V (64-bit)
     */
    #define ARCH_WORD_SIZE 64
    
    /**
     * @brief Architecture-specific word type
     */
    typedef uint64_t arch_word_t;
    
    /**
     * @brief Architecture-specific physical address type
     */
    typedef uint64_t arch_paddr_t; // Physical Address
    
    /**
     * @brief Architecture-specific virtual address type
     */
    typedef uint64_t arch_vaddr_t; // Virtual Address

#else
    #error "ERROR: Architecture not selected! Define -DARCH_X86 or -DARCH_RISCV64 in Makefile."
#endif

#endif