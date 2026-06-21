#include "kernel.h"
#include "isr.h"

#define MAX_TASKS 16
#define MAX_MESSAGES 8

typedef enum { TASK_EMPTY, TASK_RUNNING, TASK_WAITING, TASK_DEAD } task_state_t;

typedef struct {
    uint32_t sender_pid;
    uint32_t payload;
} message_t;

typedef struct {
    int pid;
    task_state_t state;
    registers_t regs;
    uint8_t priority;

    message_t mailbox[MAX_MESSAGES];
    uint8_t msg_head;
    uint8_t msg_tail;
    uint8_t msg_count;
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
        tasks[i].msg_head = 0;
        tasks[i].msg_tail = 0;
        tasks[i].msg_count = 0;
    }
    multitasking_enabled = 1;

    create_process((uint32_t)idle_task_process, (uint32_t)&idle_stack[255]);
    tasks[0].priority = 0;
}

int create_process(uint32_t eip, uint32_t esp) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) break;
    }
    if (i == MAX_TASKS) return -1;

    tasks[i].pid = next_pid++;
    tasks[i].state = TASK_RUNNING;
    tasks[i].priority = 10;
    tasks[i].msg_head = 0;
    tasks[i].msg_tail = 0;
    tasks[i].msg_count = 0;

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

void set_task_priority(int pid, uint8_t new_priority) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_EMPTY) {
            tasks[i].priority = new_priority;
            return;
        }
    }
}

int send_message(int target_pid, uint32_t payload) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == target_pid && tasks[i].state != TASK_EMPTY) {
            if (tasks[i].msg_count >= MAX_MESSAGES)
                return -1;
            
            int head = tasks[i].msg_head;
            tasks[i].mailbox[head].sender_pid = tasks[current_task].pid;
            tasks[i].mailbox[head].payload = payload;

            tasks[i].msg_head = (head + 1) % MAX_MESSAGES;
            tasks[i].msg_count++;
            return 0;
        }
    }
    return -1;
}

int receive_message(uint32_t *sender_out, uint32_t *payload_out) {
    if (tasks[current_task].msg_count == 0)
        return -1;
    
    int tail = tasks[current_task].msg_tail;
    *sender_out = tasks[current_task].mailbox[tail].sender_pid;
    *payload_out = tasks[current_task].mailbox[tail].payload;

    tasks[current_task].msg_tail = (tail + 1) % MAX_MESSAGES;
    tasks[current_task].msg_count--;
    return 0;
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

    int next_task = 0;
    uint8_t highest_priority = 0;
    int found = 0;
    
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_RUNNING) {
            if (!found || tasks[i].priority > highest_priority) {
                highest_priority = tasks[i].priority;
                next_task = i;
                found = 1;
            }
        }
    }

    current_task = next_task;
    *regs = tasks[current_task].regs;

    regs->eflags |= 0x200;
}

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner_pid = -1;
}

void mutex_lock(mutex_t *m) {
    if (!multitasking_enabled || current_task == -1) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;

    while (1) {
        asm volatile("cli");

        if (m->locked == 0) {
            m->locked = 1;
            m->owner_pid = tasks[current_task].pid;
            asm volatile("sti");
            return;
        }
        asm volatile("sti");
        asm volatile("hlt");
    }
}

void mutex_unlock(mutex_t *m) {
    if (!multitasking_enabled || current_task == -1) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;

    asm volatile("cli");
    if (m->locked == 1 && m->owner_pid == tasks[current_task].pid) {
        m->locked = 0;
        m->owner_pid = -1;
    }
    asm volatile("sti");
}