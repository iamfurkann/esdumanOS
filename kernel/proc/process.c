#include "kernel.h"

extern uint32_t *page_directory;
extern uint32_t clone_page_directory(void);

process_t tasks[MAX_TASKS];
int current_task = -1;
int multitasking_enabled = 0;
int next_pid = 0;
int foreground_task = -1;

uint32_t idle_stack[256];
void idle_task_process(void) {
    while (1) {
        asm volatile ("int $0x80" : : "a"(99));
    }
}

void init_multitasking(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_EMPTY;
        tasks[i].wait_reason = WAIT_NONE;
        tasks[i].msg_head = 0;
        tasks[i].msg_tail = 0;
        tasks[i].msg_count = 0;
    }

    create_process((uint32_t)idle_task_process, (uint32_t)&idle_stack[255], (uint32_t)page_directory);
    tasks[0].base_priority = 0;
    tasks[0].current_priority = 0;
}

int create_process(uint32_t eip, uint32_t esp, uint32_t cr3) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) break;
    }
    if (i == MAX_TASKS) return -1;

    tasks[i].pid = next_pid++;

    if (current_task == -1 || current_task == 0) {
        tasks[i].parent_pid = -1;
        tasks[i].uid = 0;
    } else {
        tasks[i].parent_pid = tasks[current_task].pid;
        tasks[i].uid = tasks[current_task].uid;
    }

    tasks[i].in_signal_handler = 0;
    tasks[i].state = TASK_RUNNING;
    tasks[i].wait_reason = WAIT_NONE;
    tasks[i].held_mutex = 0;
    tasks[i].pending_signals = 0;
    for (int k = 0; k < MAX_USER_SIGNALS; k++) {
        tasks[i].signal_handlers[k] = 0;
    }
    
    tasks[i].base_priority = 10;
    tasks[i].current_priority = 10;
    
    tasks[i].msg_head = 0;
    tasks[i].msg_tail = 0;
    tasks[i].msg_count = 0;

    tasks[i].page_directory = cr3;

    uint8_t *ptr = (uint8_t *)&tasks[i].regs;
    for (uint32_t j = 0; j < sizeof(arch_regs_t); j++) ptr[j] = 0;

    if (i == 0) {
        tasks[i].regs.cs = 0x08; // KERNEL_CS
        tasks[i].regs.ds = 0x10; // KERNEL_DS
        tasks[i].regs.ss = 0x10;
    } else {
        tasks[i].regs.cs = GDT_USER_CS; // USER_CS (Ring 3)
        tasks[i].regs.ds = GDT_USER_DS; // USER_DS (Ring 3)
        tasks[i].regs.ss = GDT_USER_DS;
    }
    
    tasks[i].regs.eip = eip;         
    tasks[i].regs.useresp = esp;     
    tasks[i].regs.eflags = EFLAGS_DEFAULT;
    tasks[i].regs.int_no = 32;
    tasks[i].regs.err_code = 0;

    if (current_task == -1) current_task = i;

    return tasks[i].pid;
}

void set_task_priority(int pid, uint8_t new_priority) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_EMPTY) {
            tasks[i].base_priority = new_priority;
            tasks[i].current_priority = new_priority;
            return;
        }
    }
}

int send_message(int target_pid, uint32_t payload) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == target_pid && tasks[i].state != TASK_EMPTY) {
            if (tasks[i].msg_count >= MAX_MESSAGES)
                return E_BUSY;
            
            int head = tasks[i].msg_head;
            tasks[i].mailbox[head].sender_pid = tasks[current_task].pid;
            tasks[i].mailbox[head].payload = payload;

            tasks[i].msg_head = (head + 1) % MAX_MESSAGES;
            tasks[i].msg_count++;
            return E_OK;
        }
    }
    return E_NOENT;
}

int receive_message(uint32_t *sender_out, uint32_t *payload_out) {
    if (tasks[current_task].msg_count == 0)
        return E_NOENT;
    
    int tail = tasks[current_task].msg_tail;
    *sender_out = tasks[current_task].mailbox[tail].sender_pid;
    *payload_out = tasks[current_task].mailbox[tail].payload;

    tasks[current_task].msg_tail = (tail + 1) % MAX_MESSAGES;
    tasks[current_task].msg_count--;
    return E_OK;
}

void exit_current_process(arch_regs_t *regs) {
    if (tasks[current_task].held_mutex != 0) {
        mutex_unlock(tasks[current_task].held_mutex);
    }
    
    int parent_pid = tasks[current_task].parent_pid;
    tasks[current_task].state = TASK_DEAD;

    int parent_idx = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == parent_pid && tasks[i].state == TASK_WAITING && tasks[i].wait_reason == WAIT_CHILD) {
            tasks[i].state = TASK_RUNNING;
            tasks[i].wait_reason = WAIT_NONE;
            parent_idx = i;
            break;
        }
    }

    if (parent_idx != -1) {
        foreground_task = parent_idx;
    } else {
        foreground_task = 0; 
    }
    schedule(regs);
}

void sleep_current_task(arch_regs_t *regs, int reason) {
    tasks[current_task].regs = *regs;
    tasks[current_task].state = TASK_WAITING;
    tasks[current_task].wait_reason = (wait_reason_t)reason;
    schedule(regs);
}

void wakeup_tasks(int reason) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_WAITING && tasks[i].wait_reason == (wait_reason_t)reason) {
            tasks[i].state = TASK_RUNNING;
            tasks[i].wait_reason = WAIT_NONE;
        }
    }
}

void schedule(arch_regs_t *regs) {
    if (!multitasking_enabled || current_task == -1) return;
    if ((regs->cs & 0x03) == 0 && current_task != 0) {
        return; 
    }
    
    tasks[current_task].regs = *regs;

    int next_task = 0;
    int max_priority = -1;

    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (current_task + i) % MAX_TASKS;
        if (idx == 0) continue; 

        if (tasks[idx].state == TASK_RUNNING) {
            if ((int)tasks[idx].current_priority > max_priority) {
                max_priority = tasks[idx].current_priority;
                next_task = idx;
            }
        }
    }

    if (next_task == 0 && current_task == 0) return; 

    current_task = next_task;
    
    uint32_t k_stack_top = (((uint32_t)tasks[current_task].kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 
    
    asm volatile ("mov %0, %%cr3" : : "r"(tasks[current_task].page_directory));

    *regs = tasks[current_task].regs;
    regs->eflags |= 0x200;
    check_and_deliver_signals(regs);
}
void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner_pid = -1;
}

void mutex_lock(mutex_t *m, arch_regs_t *regs) {
    if (!multitasking_enabled || current_task == -1) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;
    uint32_t spin_count = 0; 

    while (1) {
        asm volatile("cli"); 

        if (m->locked == 0) {
            m->locked = 1;
            m->owner_pid = tasks[current_task].pid;
            tasks[current_task].held_mutex = m;

            asm volatile("sti"); 
            return;
        }

        if (regs != 0 && (regs->cs & 0x03) != 0) {
            regs->eip -= 2;
            asm volatile("sti");
            sleep_current_task(regs, WAIT_MUTEX);
            return;
        } else {
            asm volatile("sti"); 
            asm volatile("nop");
            spin_count++;
            if (spin_count > 100000000) { 
                terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
                printk("\n[KERNEL PANIC] Spinlock Deadlock Tespit Edildi!\n");
                printk("PID %d (Kernel Mode) sonsuz donguye girdi. Mutex sahibi: PID %d\n", 
                       tasks[current_task].pid, m->owner_pid);
                asm volatile("cli; hlt");
            }
        }
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
        tasks[current_task].held_mutex = 0;
        wakeup_tasks(WAIT_MUTEX);
    }

    asm volatile("sti"); 
}

void register_user_signal(int sig_num, uint32_t handler_addr) {
    if (current_task == -1 || sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    tasks[current_task].signal_handlers[sig_num] = handler_addr;
}

void send_user_signal(int target_pid, int sig_num) {
    if (sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == target_pid && tasks[i].state != TASK_EMPTY && tasks[i].state != TASK_DEAD) {
            tasks[i].pending_signals |= (1 << sig_num);
            if (tasks[i].state == TASK_WAITING) {
                tasks[i].state = TASK_RUNNING;
                tasks[i].wait_reason = WAIT_NONE;
            }
            return;
        }
    }
}

void check_and_deliver_signals(arch_regs_t *regs) {
    if (current_task == -1) return;
    process_t *curr = &tasks[current_task];
    
    if (curr->in_signal_handler) return; 

    if (curr->pending_signals > 0) {
        for (int i = 0; i < MAX_USER_SIGNALS; i++) {
            if (curr->pending_signals & (1 << i)) {
                
                curr->pending_signals &= ~(1 << i);
                
                uint32_t handler = curr->signal_handlers[i];
                if (handler != 0) {
                    curr->signal_saved_regs = *regs;
                    curr->in_signal_handler = 1;
                    regs->eip = handler;
                    
                    return; 
                }
            }
        }
    }
}

void restore_signal_context(arch_regs_t *regs) {
    if (current_task == -1) return;
    process_t *curr = &tasks[current_task];
    if (curr->in_signal_handler) {
        *regs = curr->signal_saved_regs;
        curr->in_signal_handler = 0;
    }
}

int check_free_task_slot(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) return 1;
    }
    return 0;
}

void start_first_task(void) {
    int first_task_idx = -1;

    if (foreground_task > 0 && tasks[foreground_task].state == TASK_RUNNING) {
        first_task_idx = foreground_task;
    } 
    else {
        for (int i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_RUNNING) {
                first_task_idx = i;
                break;
            }
        }
    }

    if (first_task_idx == -1) {
        printk("[SCHEDULER UYARISI] Calisacak hicbir kullanici gorevi yok.\n");
        first_task_idx = 0; 
    }

    current_task = first_task_idx; 
    
    uint32_t k_stack_top = (((uint32_t)tasks[current_task].kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 

    asm volatile ("mov %0, %%cr3" : : "r"(tasks[current_task].page_directory));
    
    multitasking_enabled = 1;

    uint32_t eip = tasks[current_task].regs.eip;
    uint32_t esp = tasks[current_task].regs.useresp;

    asm volatile(
        "mov $0x2B, %%ax \n"
        "mov %%ax, %%ds \n"
        "mov %%ax, %%es \n"
        "mov %%ax, %%fs \n"
        "mov %%ax, %%gs \n"
        "pushl $0x2B \n"     // SS (Stack Segment)
        "pushl %0 \n"        // Kullanıcı ESP'si
        "pushl $0x202 \n"    // EFLAGS (0x202 = Interruptlar AÇIK!)
        "pushl $0x23 \n"     // CS (gdt.h içindeki GDT_USER_CS değeri)
        "pushl %1 \n"        // EIP (Kullanıcı kodu başlama adresi)
        "iret \n"            // Ring 3'e güvenli geçiş!
        : : "r"(esp), "r"(eip)
    );
}