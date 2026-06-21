#include "kernel.h"
#include "isr.h"

#define MAX_TASKS 16

typedef enum { TASK_EMPTY, TASK_RUNNING, TASK_WAITING, TASK_DEAD } task_state_t;

typedef struct {
    int pid;
    task_state_t state;
    registers_t regs;
} process_t;

process_t tasks[MAX_TASKS];
int current_task = -1;
int multitasking_enabled = 0;
int next_pid = 0;

int create_process(uint32_t eip, uint32_t esp);
uint32_t idle_stack[256];

void idle_task_process(void) {
    while (1) {
        asm volatile ("int $0x80" : : "a"(99));
    }
}

void init_multitasking(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_EMPTY;
    }
    multitasking_enabled = 1;

    create_process((uint32_t)idle_task_process, (uint32_t)&idle_stack[255]);
}

int create_process(uint32_t eip, uint32_t esp) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) break;
    }
    if (i == MAX_TASKS) return -1;

    tasks[i].pid = next_pid++;
    tasks[i].state = TASK_RUNNING;

    uint8_t *ptr = (uint8_t *)&tasks[i].regs;
    for (uint32_t j = 0; j < sizeof(registers_t); j++) ptr[j] = 0;

    tasks[i].regs.ds = 0x2B;
    tasks[i].regs.cs = 0x23;
    tasks[i].regs.ss = 0x2B;
    
    tasks[i].regs.eip = eip;         
    tasks[i].regs.useresp = esp;     
    tasks[i].regs.eflags = 0x202;
    tasks[i].regs.int_no = 32;
    tasks[i].regs.err_code = 0;

    if (current_task == -1) current_task = i;

    return tasks[i].pid;
}

void exit_current_process(void) {
    tasks[current_task].state = TASK_DEAD;
    asm volatile("int $32"); 
}

void sleep_current_task(registers_t *regs) {
    tasks[current_task].regs = *regs;
    tasks[current_task].state = TASK_WAITING;
    schedule(regs);
}

void wakeup_all_tasks(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_WAITING) {
            tasks[i].state = TASK_RUNNING;
        }
    }
}

void schedule(registers_t *regs) {
    if (!multitasking_enabled || current_task == -1) return;

    if ((regs->cs & 0x3) == 3) {
        if (tasks[current_task].state == TASK_RUNNING) {
            tasks[current_task].regs = *regs;

        }
    }

    int next_task = current_task;
    int found = 0;
    
    for (int i = 0; i < MAX_TASKS; i++) {
        next_task = (next_task + 1) % MAX_TASKS;
        if (next_task != 0 && tasks[next_task].state == TASK_RUNNING) {
            found = 1;
            break;
        }
    }

    if (!found && tasks[0].state == TASK_RUNNING) {
        next_task = 0;
        found = 1;
    }

    if (found) {
        current_task = next_task;
        *regs = tasks[current_task].regs;
        regs->eflags |= 0x200; 
    }
}