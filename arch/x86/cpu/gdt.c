#include "gdt.h"
#include "tss.h"
#include "stdio.h"

gdt_entry_t gdt_entries[GDT_ENTRIES];
gdt_ptr_t gdt_ptr;
/**
 * Configures a single descriptor gate in the Global Descriptor Table (GDT).
 * Expected behavior: Sets up the base address, limit, access flags, and granularity
 * for the specified GDT entry index.
 */
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

/**
 * Initializes the Global Descriptor Table (GDT).
 * Expected behavior: Sets up default segments including the null segment,
 * kernel code/data/stack, and user code/data/stack segments. It also configures
 * the Task State Segment (TSS) and flushes the new GDT to the CPU.
 */
void init_gdt(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0); // Null Segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Kernel Code
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel Data
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0x92, 0xCF); // Kernel Stack
    gdt_set_gate(4, 0, 0xBFFFE, 0xFA, 0xCF); // User Code
    gdt_set_gate(5, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User Data
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User Stack

    uint32_t default_kernel_esp = (((uint32_t)kernel_stack_ring0 + sizeof(kernel_stack_ring0)) & 0xFFFFFFF0) - 4;
    tss_install(GDT_TSS_SEL >> 3, GDT_KERNEL_DS, default_kernel_esp);

    /*
     * The double fault TSS lives at index 8 but cannot be filled in yet: it
     * carries the CR3 the handler runs with, and the kernel page directory does
     * not exist until init_paging(). Left as a null descriptor here and
     * installed by init_double_fault_handler(); the GDT limit already covers it,
     * so no reload is needed then.
     */
    gdt_set_gate(GDT_DF_TSS_SEL >> 3, 0, 0, 0, 0);

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}