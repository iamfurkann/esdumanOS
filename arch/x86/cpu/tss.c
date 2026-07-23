#include "tss.h"
#include "gdt.h"

tss_entry_t tss_entry;

/**
 * Installs and initializes the Task State Segment (TSS).
 * Expected behavior: Sets up a GDT entry for the TSS, zeroes out the TSS structure,
 * and sets the initial kernel stack pointer (Ring 0 stack).
 */
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

/**
 * Updates the kernel stack pointer within the TSS.
 * Expected behavior: Modifies the esp0 field of the TSS so the CPU knows where the
 * Ring 0 stack is located during privilege level changes (e.g., system calls or interrupts).
 */
void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}