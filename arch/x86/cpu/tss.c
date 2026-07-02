#include "tss.h"
#include "gdt.h"

tss_entry_t tss_entry;

void tss_install(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    gdt_set_gate(num, base, limit, 0x89, 0x00);

    uint8_t *tss_ptr = (uint8_t *)&tss_entry;
    for (uint32_t i = 0; i < sizeof(tss_entry_t); i++) {
        tss_ptr[i] = 0;
    }

    tss_entry.ss0  = ss0;
    tss_entry.esp0 = esp0;

    tss_entry.iomap_base = sizeof(tss_entry_t);
}

void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}