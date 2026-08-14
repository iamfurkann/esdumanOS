#include "isr.h"
#include "stdio.h"
#include "tty.h"
#include "io.h"
#include "signal.h"
#include "process.h"
#include "registers.h"
#include "keyboard.h"
#define PIC1_COMMAND 0x20
#define PIC2_COMMAND 0xA0
#define PIC_EOI      0x20

#define IRQ0_TIMER  32
#define IRQ1_KEYBOARD 33
#define ISR_SYSCALL 128
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

/**
 * Cleans the general purpose registers.
 * Expected behavior: Clears eax, ebx, ecx, edx, esi, and edi to zero.
 */
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

/**
 * Saves the current stack state before a kernel panic.
 * Expected behavior: Copies the top 256 DWORDs of the stack into a buffer for debugging.
 */
void save_stack_before_panic(void) {
    uint32_t current_esp;

    asm volatile("mov %%esp, %0" : "=r"(current_esp));
    uint32_t *stack_ptr = (uint32_t *)current_esp;

    for (int i = 0; i < 256; i++)
        saved_panic_stack[i] = stack_ptr[i];
}

/**
 * Handles page fault exceptions.
 * Expected behavior: Identifies if the fault occurred in user or kernel mode.
 * Kills the faulting user task or triggers a kernel panic if it originated in kernel space.
 */
void page_fault_handler(arch_regs_t *regs) {
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int is_user = regs->err_code & 0x4;

    if (is_user) {
        printk("\n[SEGFAULT] Violation (PID: %d)! Unauthorized memory access: 0x%x\n",
               current_task ? current_task->pid : -1, faulting_address);

        /*
         * Tear the task down the same way exit() does, rather than only marking
         * it dead.
         *
         * Setting TASK_DEAD and rescheduling was the entire teardown, and it
         * skipped everything exit_current_process() exists to do: file and pipe
         * reference counts were never dropped, a parent blocked on WAIT_CHILD
         * was never woken, and the task was neither unlinked from the run list
         * nor placed on the zombie list - so the reaper in schedule() never saw
         * it and its address space, page tables, user stacks, descriptor table
         * and process_t leaked permanently.
         *
         * The visible symptom was worse than the leak: a user program with an
         * ordinary null-pointer bug left the shell that had exec'd it parked on
         * WAIT_CHILD forever, with no console.
         *
         * 139 is the conventional 128 + SIGSEGV, so a parent can tell a crash
         * from an ordinary non-zero exit.
         */
        if (current_task) {
            current_task->exit_code = 139;
            exit_current_process(regs);
            /* Does not return: it switches away and never resumes this task. */
        }
        schedule(regs);
    } else {
        extern volatile uint32_t current_fault_handler;
        extern void uaccess_note_fault(void);
        if (current_fault_handler != 0) {
            /* A user copy faulted. Record it before redirecting, because the
             * copy helper reports its verdict from that flag rather than from
             * which exit path it lands on. */
            uaccess_note_fault();
            regs->eip = current_fault_handler;
            return;
        }

        extern int kernel_panic_mode;
        kernel_panic_mode = 1;
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[KERNEL PANIC] Kernel generated Page Fault: 0x%x\n", faulting_address);
        printk("Error Code: %d\n", regs->err_code);
        asm volatile("cli; hlt");
    }
}

/**
 * The main interrupt dispatcher.
 * Expected behavior: Routes the interrupt to the appropriate handler based on the interrupt number.
 * Handles exceptions by panicking, and hardware interrupts by acknowledging the PIC and calling specific drivers.
 */
void isr_handler(arch_regs_t *regs) {
    
    /* EXCEPTIONS (0-31)*/
    if (regs->int_no < 32) {
        if (regs->int_no == 14) {
            page_fault_handler(regs);
            return;
        }
        extern int kernel_panic_mode;
        kernel_panic_mode = 1;
        multitasking_enabled = 0;
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);

        printk("\n==================================================\n");
        printk("                KERNEL PANIC!                     \n");
        printk("==================================================\n");
        
        printk("Error Type : %s (Interrupt: %d)\n", exception_messages[regs->int_no], regs->int_no);
        printk("Error Code : 0x%x\n\n", regs->err_code);

        printk("--- CPU REGISTERS ---\n");
        printk("EAX: 0x%x   EBX: 0x%x   ECX: 0x%x\n", regs->eax, regs->ebx, regs->ecx);
        printk("EDX: 0x%x   ESI: 0x%x   EDI: 0x%x\n", regs->edx, regs->esi, regs->edi);
        printk("EIP: 0x%x   EBP: 0x%x   ESP: 0x%x\n", regs->eip, regs->ebp, regs->esp);
        printk("CS:  0x%x   DS:  0x%x   EFLAGS: 0x%x\n", regs->cs, regs->ds, regs->eflags);
        
        printk("==================================================\n");
        printk("System halted. Please restart.\n");

        save_stack_before_panic();
        clean_registers();
        asm volatile("cli; hlt");
    }
    else if (regs->int_no >= 32 && regs->int_no <= 47) {
        if (regs->int_no >= 40) {
            outb(PIC2_COMMAND, PIC_EOI); // Slave PIC
        }
        outb(PIC1_COMMAND, PIC_EOI);     // Master PIC
        
        
        if (regs->int_no == IRQ0_TIMER) {
            timer_interrupt_handler();
            if ((regs->cs & 3) == 3) {
                schedule(regs);
            }
        }
        else if (regs->int_no == IRQ1_KEYBOARD) {
            keyboard_interrupt_handler();
        }

        //IRQ14 (PRIMARY ATA)
        #define IRQ14_ATA 46
        else if (regs->int_no == IRQ14_ATA) {
            ata_irq_handler();
        }

    }
    else if (regs->int_no == ISR_SYSCALL) {
        syscall_handler(regs);
    }
    else {
        printk("Unknown Interrupt: %d\n", regs->int_no);
    }
}