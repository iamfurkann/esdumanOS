#include "idt.h"
#include "io.h"
#include "isr.h"

/* The isrN stubs are declared in isr.h, all forty-nine of them together. */

idt_entry_t idt[256];
idt_ptr_t idtp;

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

/**
 * Remaps the Programmable Interrupt Controllers (PICs).
 * Expected behavior: Changes the master and slave PIC vector offsets from 0x00-0x0F to 0x20-0x2F
 * to prevent conflicts with CPU exceptions.
 */
static void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);

    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);

    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /*
     * Unmask the lines this kernel actually services, instead of restoring
     * whatever the firmware left behind.
     *
     * The masks used to be read before ICW1 and written back verbatim, and
     * these were the only writes to the PIC data ports in the whole tree - so
     * nothing ever enabled an interrupt. It works under GRUB and QEMU because
     * that firmware hands over with the lines already open. A loader that
     * masked them (the 8259 power-on default is 0xFF) would leave IRQ0 and
     * IRQ1 dead with handlers installed for both, and the first boot would
     * hang forever inside the keyboard wait in kernel_main() with nothing able
     * to wake it.
     *
     * Master: IRQ0 (PIT), IRQ1 (keyboard) and IRQ2 (the cascade, without which
     * no slave interrupt is delivered at all) enabled; the rest masked.
     * Slave: IRQ14 (primary ATA) enabled; the rest masked.
     */
    outb(PIC1_DATA, (uint8_t)~((1 << 0) | (1 << 1) | (1 << 2)));
    outb(PIC2_DATA, (uint8_t)~(1 << 6));
}

/**
 * Sets an Interrupt Descriptor Table (IDT) gate.
 * Expected behavior: Configures a single IDT entry with the given base address,
 * segment selector, and attributes (flags).
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel     = sel; 
    idt[num].always0 = 0;
    idt[num].flags   = flags;
}

/**
 * Initializes the Interrupt Descriptor Table (IDT).
 * Expected behavior: Clears all IDT entries, sets up handlers for exceptions and IRQs,
 * remaps the PICs, and finally loads the new IDT into the CPU.
 */
void init_idt(void) {
    idtp.limit = (sizeof(idt_entry_t) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E); idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E); idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E); idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E); idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E); idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E); idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E); idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E); idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E); idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E); idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E); idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E); idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E); idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E); idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E); idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
    
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E); idt_set_gate(33, (uint32_t)isr33, 0x08, 0x8E);
    
    // [NEW]: Gates Opened
    idt_set_gate(34, (uint32_t)isr34, 0x08, 0x8E); idt_set_gate(35, (uint32_t)isr35, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)isr36, 0x08, 0x8E); idt_set_gate(37, (uint32_t)isr37, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)isr38, 0x08, 0x8E); idt_set_gate(39, (uint32_t)isr39, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)isr40, 0x08, 0x8E); idt_set_gate(41, (uint32_t)isr41, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)isr42, 0x08, 0x8E); idt_set_gate(43, (uint32_t)isr43, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr44, 0x08, 0x8E); idt_set_gate(45, (uint32_t)isr45, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)isr46, 0x08, 0x8E); idt_set_gate(47, (uint32_t)isr47, 0x08, 0x8E);

    idt_set_gate(128, (uint32_t)isr128, 0x08, 0xEE); // Syscall (User Mode)
    
    pic_remap();
    load_idt((uint32_t)&idtp);
}