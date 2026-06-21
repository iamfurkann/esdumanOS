#include "isr.h"
#include "stdio.h"
#include "tty.h"
#include "io.h"
#include "signal.h"

#define PIC1_COMMAND 0x20
#define PIC_EOI		 0x20 //End of Interrupt
#define IRQ0_TIMER	32
#define IRQ1_KEYBOARD 33
#define ISR_SYSCALL 128

#define SYSCALL_EXIT 1
#define SYSCALL_READ 3
#define SYSCALL_WRITE 4
#define SYSCALL_EXEC 5
#define SYSCALL_YIELD 99

extern void keyboard_interrupt_handler(void);
extern void timer_interrupt_handler(void);

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

void isr_handler(registers_t *regs) {
    if (regs->int_no < 32) {
        extern int multitasking_enabled;
        multitasking_enabled = 0;
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);

        printk("\n==================================================\n");
        printk("                KERNEL PANIC!                     \n");
        printk("==================================================\n");
        
        printk("Hata Turu : %s (Interrupt: %d)\n", exception_messages[regs->int_no], regs->int_no);
        printk("Hata Kodu : 0x%x\n\n", regs->err_code);
        
        if (regs->int_no == 14) {
            uint32_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            printk("--> CR2 (Coken Bellek Adresi): 0x%x\n\n", cr2);
        }

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

    else if (regs->int_no == IRQ0_TIMER) {
        timer_interrupt_handler();
        signal_tick_handler();

        extern void schedule(registers_t *regs);
        schedule(regs);

        outb(PIC1_COMMAND, PIC_EOI);
    }
    else if (regs->int_no == IRQ1_KEYBOARD) {
        keyboard_interrupt_handler();
        outb(PIC1_COMMAND, PIC_EOI);

        extern void wakeup_all_tasks(void);
        wakeup_all_tasks();
    }
    else if (regs->int_no == ISR_SYSCALL) {
        extern char get_keyboard_char(void);
        extern void execute_command(char *cmd);

        if (regs->eax == SYSCALL_EXIT) {
            extern void exit_current_process(void);
            extern int current_task;
            
            terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            printk("\n[KERNEL] PID %d calismasini tamamladi (Exit Code: %d).\n> ", current_task, regs->ebx);
            terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

            exit_current_process();
            return;
        }

        if (regs->eax == SYSCALL_READ) {
            char c = get_keyboard_char();
            if (c == 0) {
                regs->eip -= 2; 
                
                extern void sleep_current_task(registers_t *regs);
                sleep_current_task(regs);
            } else {
                regs->eax = (uint32_t)c;
            }
        }
        else if (regs->eax == SYSCALL_YIELD) {
            asm volatile("sti; hlt; cli");
        }
        else if (regs->eax == SYSCALL_WRITE) {
            char *str = (char *)regs->ebx;
            printk("%s", str);
        }
        else if (regs->eax == SYSCALL_EXEC) {
            char *cmd = (char *)regs->ebx;
            execute_command(cmd);
        }
    }
}