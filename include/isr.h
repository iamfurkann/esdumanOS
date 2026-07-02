#ifndef ISR_H
#define ISR_H

#include "types.h"
#include "registers.h"

/*typedef struct {
    uint32_t ds;                                     // En son itilen DS
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // pusha sırası (Ters)
    uint32_t int_no, err_code;                       // ISR Makrosunun ittikleri
    uint32_t eip, cs, eflags, useresp, ss;           // İşlemcinin otomatik ittikleri
} registers_t;*/

typedef struct {
    uint32_t locked;
    int owner_pid;
} mutex_t;

void isr_handler(arch_regs_t *regs);
void schedule(arch_regs_t *regs);
void clean_registers(void);
void save_stack_before_panic(void);

#endif