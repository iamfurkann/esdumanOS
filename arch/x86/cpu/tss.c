#include "tss.h"
#include "gdt.h"
#include "idt.h"
#include "stdio.h"
#include "tty.h"

tss_entry_t tss_entry;
tss_entry_t df_tss;

extern int kernel_panic_mode;

/**
 * @brief Dedicated stack for the double fault task.
 *
 * Deliberately separate from every process kernel stack, because the fault this
 * handler exists to report is normally one of those stacks running out.
 */
static uint8_t df_stack[4096] __attribute__((aligned(16)));

int df_stack_contains(uint32_t addr) {
    return addr >= (uint32_t)df_stack &&
           addr <  (uint32_t)df_stack + sizeof(df_stack);
}

/**
 * @brief Reports a double fault and halts.
 *
 * Entered by a hardware task switch, so it runs on df_stack with a known good
 * CR3 no matter what state the faulting task left behind. The processor saved
 * that task's context into the main TSS on the way in, which is where the
 * register values below come from.
 *
 * Never returns: there is nothing safe to return to.
 */
static void double_fault_handler(void) {
    kernel_panic_mode = 1;

    terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    printk("\n==================================================\n");
    printk("               DOUBLE FAULT (#DF)                 \n");
    printk("==================================================\n");
    printk("A fault was raised while handling another fault.\n");
    printk("Most often this is kernel stack exhaustion.\n\n");
    printk("Context of the task that faulted:\n");
    printk("  EIP: 0x%x   CS: 0x%x   EFLAGS: 0x%x\n",
           tss_entry.eip, tss_entry.cs, tss_entry.eflags);
    printk("  ESP: 0x%x   EBP: 0x%x   SS: 0x%x\n",
           tss_entry.esp, tss_entry.ebp, tss_entry.ss);
    printk("  CR3: 0x%x   ESP0: 0x%x\n", tss_entry.cr3, tss_entry.esp0);
    printk("==================================================\n");
    printk("System halted.\n");

    while (1) {
        asm volatile("cli; hlt");
    }
}

void init_double_fault_handler(void) {
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    uint8_t *p = (uint8_t *)&df_tss;
    for (uint32_t i = 0; i < sizeof(tss_entry_t); i++) {
        p[i] = 0;
    }

    uint32_t stack_top = (((uint32_t)df_stack + sizeof(df_stack)) & 0xFFFFFFF0) - 4;

    df_tss.cr3    = cr3;
    df_tss.eip    = (uint32_t)double_fault_handler;
    df_tss.eflags = 0x00000002;   // reserved bit 1 set, IF clear
    df_tss.esp    = stack_top;
    df_tss.ebp    = stack_top;
    df_tss.esp0   = stack_top;
    df_tss.ss0    = GDT_KERNEL_DS;
    df_tss.cs     = GDT_KERNEL_CS;
    df_tss.ss     = GDT_KERNEL_DS;
    df_tss.ds     = GDT_KERNEL_DS;
    df_tss.es     = GDT_KERNEL_DS;
    df_tss.fs     = GDT_KERNEL_DS;
    df_tss.gs     = GDT_KERNEL_DS;
    df_tss.iomap_base = sizeof(tss_entry_t);

    // Available 32-bit TSS descriptor (access type 9), byte granularity.
    gdt_set_gate(GDT_DF_TSS_SEL >> 3, (uint32_t)&df_tss,
                 sizeof(tss_entry_t) - 1, 0x89, 0x00);

    // Task gate: present, DPL 0, type 5. The offset field is ignored - the
    // selector names the TSS the processor switches to.
    idt_set_gate(8, 0, GDT_DF_TSS_SEL, 0x85);
}

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