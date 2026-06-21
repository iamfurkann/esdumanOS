#ifndef ISR_H
#define ISR_H

#include "types.h"

/* isr.h dosyasındaki güncellenmiş registers_t yapısı */

typedef struct {
    /* DÜZELTME: idt.asm içinde 'push eax' (DS) en son yapıldığı için, 
     * bellekteki ilk (en üst) değer DS olmalıdır! */
    uint32_t ds; 
    
    /* pusha komutunun yığına attığı register'lar (Ters sırayla okuyoruz) */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    
    /* İşlemcinin otomatik ve manuel yığına attığı diğer değerler */
    uint32_t int_no, err_code; 
    uint32_t eip, cs, eflags, useresp, ss; 
} registers_t;

void isr_handler(registers_t *regs);
void schedule(registers_t *regs);
void clean_registers(void);
void save_stack_before_panic(void);

#endif