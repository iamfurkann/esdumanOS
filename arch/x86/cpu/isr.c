#include "isr.h"
#include "stdio.h"
#include "tty.h"
#include "io.h"
#include "signal.h"
#include "fs.h"
#include "process.h"
#include "syscall.h"
#include "registers.h"
#include "security.h"

extern security_level_t current_sec_level;
extern void set_security_level(security_level_t level);

#define PIC1_COMMAND 0x20
#define PIC2_COMMAND 0xA0
#define PIC_EOI      0x20

#define IRQ0_TIMER  32
#define IRQ1_KEYBOARD 33
#define ISR_SYSCALL 128

extern void keyboard_interrupt_handler(void);
extern void timer_interrupt_handler(void);
extern void syscall_handler(arch_regs_t *regs);

const char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt",
    "Coprocessor Fault", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

void clean_registers(void) {
    asm volatile (
        "xor %%eax, %%eax\n"
        "xor %%ebx, %%ebx\n"
        "xor %%ecx, %%ecx\n"
        "xor %%edx, %%edx\n"
        "xor %%esi, %%esi\n"
        "xor %%edi, %%edi\n"
        :
        :
        : "eax", "ebx", "ecx", "edx", "esi", "edi"
    );
}

uint32_t saved_panic_stack[256];

void save_stack_before_panic(void) {
    uint32_t current_esp;

    asm volatile("mov %%esp, %0" : "=r"(current_esp));
    uint32_t *stack_ptr = (uint32_t *)current_esp;

    for (int i = 0; i < 256; i++)
        saved_panic_stack[i] = stack_ptr[i];
}

void page_fault_handler(arch_regs_t *regs) {
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int is_user = regs->err_code & 0x4;

    if (is_user) {
        printk("\n[SEGFAULT] Ihlal (PID: %d)! Yetkisiz bellek erisimi: 0x%x\n", 
               tasks[current_task].pid, faulting_address);
        
        tasks[current_task].state = TASK_DEAD; 
        schedule(regs);
    } else {
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[KERNEL PANIC] Cekirdek Page Fault yaratti: 0x%x\n", faulting_address);
        printk("Hata Kodu: %d\n", regs->err_code);
        asm volatile("cli; hlt");
    }
}

/*INTERRUPT DISPATCHER*/
void isr_handler(arch_regs_t *regs) {
    
    /* EXCEPTIONS (0-31)*/
    if (regs->int_no < 32) {
        if (regs->int_no == 14) {
            page_fault_handler(regs);
            return;
        }
        multitasking_enabled = 0;
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);

        printk("\n==================================================\n");
        printk("                KERNEL PANIC!                     \n");
        printk("==================================================\n");
        
        printk("Hata Turu : %s (Interrupt: %d)\n", exception_messages[regs->int_no], regs->int_no);
        printk("Hata Kodu : 0x%x\n\n", regs->err_code);

        printk("--- CPU REGISTERS ---\n");
        printk("EAX: 0x%x   EBX: 0x%x   ECX: 0x%x\n", regs->eax, regs->ebx, regs->ecx);
        printk("EDX: 0x%x   ESI: 0x%x   EDI: 0x%x\n", regs->edx, regs->esi, regs->edi);
        printk("EIP: 0x%x   EBP: 0x%x   ESP: 0x%x\n", regs->eip, regs->ebp, regs->esp);
        printk("CS:  0x%x   DS:  0x%x   EFLAGS: 0x%x\n", regs->cs, regs->ds, regs->eflags);
        
        printk("==================================================\n");
        printk("Sistem kilitlendi. Lutfen yeniden baslatin.\n");

        save_stack_before_panic();
        clean_registers();
        asm volatile("cli; hlt");
    }
    else if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no == IRQ0_TIMER) {
            timer_interrupt_handler();
            signal_tick_handler();
            schedule(regs);
        }
        else if (regs->int_no == IRQ1_KEYBOARD) {
            keyboard_interrupt_handler();
        }

        if (regs->int_no >= 40) {
            outb(PIC2_COMMAND, PIC_EOI); // Slave PIC
        }
        outb(PIC1_COMMAND, PIC_EOI);     // Master PIC
    }
    else if (regs->int_no == ISR_SYSCALL) {
        syscall_handler(regs);
    }
    else {
        printk("Bilinmeyen Interrupt: %d\n", regs->int_no);
    }
}