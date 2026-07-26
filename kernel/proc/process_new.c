/*
 * File: process.c
 * Purpose: Process management, context switching, and multitasking implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kernel.h"
#include "pipe.h"
#include "klog.h"
#include "errno.h"
#include "kheap.h"
#include "pmm.h"
#include "paging.h"
#include "process.h"
#include "security.h"
process_t tasks[MAX_TASKS] __attribute__((aligned(16)));
cpu_state_t cpus[MAX_CPUS];
int multitasking_enabled = 0;
int next_pid = 0;
int foreground_task = -1;
mutex_t *task_wait_mutex[MAX_TASKS] = {0};

/**
 * @brief init_multitasking
 */
void init_multitasking(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_EMPTY;
        tasks[i].wait_reason = WAIT_NONE;
        tasks[i].msg_head = 0;
        tasks[i].msg_tail = 0;
        tasks[i].msg_count = 0;
        task_wait_mutex[i] = 0;
    }

    cpus[0].cpu_id = 0;
    cpus[0].is_bsp = 1;
    cpus[0].active_task = -1;
uint32_t idle_phys = pmm_alloc_frame();
    map_page(0x80000000, idle_phys, 7); // 7 = User, RW, Present
    
    // Asm: mov eax, 99 (B8 63 00 00 00) | int 0x80 (CD 80) | jmp short -9 (EB F7)
    uint8_t idle_code[] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xEB, 0xF7 };
    uint8_t *idle_page = (uint8_t *)0x80000000;
    for(int i = 0; i < 9; i++) {
        idle_page[i] = idle_code[i];
    }

    // Retrieve the physical address of the current (kernel) page directory
    uint32_t kernel_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(kernel_cr3));

    // Start idle task at 0x80000000 (User space)
    create_process(0x80000000, 0x80000000 + 4096 - 4, kernel_cr3);
    tasks[0].base_priority = 0;
    tasks[0].current_priority = 0;
}

/**
 * @brief create_process
 * @param eip
 * @param esp
 * @param cr3
 * @return int
 */
int create_process(uint32_t eip, uint32_t esp, uint32_t cr3) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) break;
    }
    if (i == MAX_TASKS) {
        klog(LOG_LEVEL_ERROR, "PROCESS", "Could not create new process: MAX_TASKS limit reached.");
        return E_NOMEM; 
    }
    
    if (next_pid < 0 || next_pid == 0x7FFFFFFF) {
        next_pid = 2; 
    }
    tasks[i].pid = next_pid++;

    if (current_task == -1 || current_task == 0) {
        tasks[i].parent_pid = -1;
        tasks[i].uid = 0;
    } else {
        tasks[i].parent_pid = tasks[current_task].pid;
        tasks[i].uid = tasks[current_task].uid;
    }

    klog_int(LOG_LEVEL_DEBUG, "PROCESS", "New process created", tasks[i].pid);

    tasks[i].in_signal_handler = 0;
    tasks[i].state = TASK_RUNNING;
    tasks[i].wait_reason = WAIT_NONE;
    tasks[i].held_mutex = 0;
    tasks[i].pending_signals = 0;
    task_wait_mutex[i] = 0;
    for (int k = 0; k < MAX_USER_SIGNALS; k++) {
        tasks[i].signal_handlers[k] = 0;
    }
    
    tasks[i].base_priority = 10;
    tasks[i].current_priority = 10;
    
    tasks[i].msg_head = 0;
    tasks[i].msg_tail = 0;
    tasks[i].msg_count = 0;

    uint8_t *kstack_ptr = (uint8_t *)tasks[i].kstack;
    for (int j = 0; j < 4096; j++) {
        kstack_ptr[j] = 0;
    }

    tasks[i].page_directory = cr3;

    uint8_t *ptr = (uint8_t *)&tasks[i].regs;
    for (uint32_t j = 0; j < sizeof(arch_regs_t); j++) ptr[j] = 0;

    tasks[i].regs.cs = GDT_USER_CS; // USER_CS (Ring 3)
    tasks[i].regs.ds = GDT_USER_DS; // USER_DS (Ring 3)
    tasks[i].regs.ss = GDT_USER_DS;
    
    tasks[i].regs.eip = eip;         
    tasks[i].regs.useresp = esp;     
    tasks[i].regs.eflags = EFLAGS_DEFAULT;
    tasks[i].regs.int_no = 32;
    tasks[i].regs.err_code = 0;

    if (current_task == -1) current_task = i;

    return tasks[i].pid;
}

/**
 * @brief set_task_priority
 * @param pid
 * @param new_priority
 */
void set_task_priority(int pid, uint8_t new_priority) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_EMPTY) {
            tasks[i].base_priority = new_priority;
            tasks[i].current_priority = new_priority;
            return;
        }
    }
}

/**
 * @brief send_message
 * @param target_pid
 * @param payload
 * @return int
 */
int send_message(int target_pid, uint32_t payload) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == target_pid && tasks[i].state != TASK_EMPTY) {
            if (tasks[i].msg_count >= MAX_MESSAGES) {
                klog_int(LOG_LEVEL_WARN, "IPC", "Message could not be sent: Target queue full (PID)", target_pid);
                return E_BUSY;
            }
            
            int head = tasks[i].msg_head;
            tasks[i].mailbox[head].sender_pid = tasks[current_task].pid;
            tasks[i].mailbox[head].payload = payload;

            tasks[i].msg_head = (head + 1) % MAX_MESSAGES;
            tasks[i].msg_count++;
            return E_OK;
        }
    }
    klog_int(LOG_LEVEL_WARN, "IPC", "Message could not be sent: Target process not found (PID)", target_pid);
    return E_NOENT;
}
/**
 * @brief receive_message
 * @param sender_out
 * @param payload_out
 * @return int
 */
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
/**
 * @brief cleanup_process_memory
 * @param page_directory_phys
 */
void cleanup_process_memory(uint32_t page_directory_phys) {
if (page_directory_phys != 0 && page_directory_phys != (uint32_t)page_directory) {
        
        uint32_t old_cr3;
        asm volatile("mov %%cr3, %0" : "=r"(old_cr3));

        // Hedef page directory'yi gecici olarak yukleyip recursive mapping'i kullaniyoruz
        asm volatile("mov %0, %%cr3" :: "r"(page_directory_phys));

        uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
        for (int i = 4; i < 768; i++) {
            if (pd_virt[i] & 1) {
                uint32_t *pt_virt = (uint32_t *)(0xFFC00000 + (i * 0x1000));
                for (int j = 0; j < 1024; j++) {
                    if (pt_virt[j] & 1) {
                        pmm_free_frame(pt_virt[j] & 0xFFFFF000);
                    }
                }
                pmm_free_frame(pd_virt[i] & 0xFFFFF000);
                pd_virt[i] = 0;
            }
        }

        // Eski CR3 silinen CR3 ise kernel'a don. Degilse, eski CR3'e geri don (Orn: baska process'i oldururken)
        if ((old_cr3 & 0xFFFFF000) == (page_directory_phys & 0xFFFFF000)) {
            asm volatile("mov %0, %%cr3" :: "r"((uint32_t)page_directory));
        } else {
            asm volatile("mov %0, %%cr3" :: "r"(old_cr3));
        }

        pmm_free_frame(page_directory_phys);
        
        klog(LOG_LEVEL_INFO, "PMM", "User process memory (PD, PT, PTE) fully reclaimed.");
    }
}

/**
 * @brief exit_current_process
 * @param regs
 */
void exit_current_process(arch_regs_t *regs) {
    process_t *curr = &tasks[current_task];

    if (curr->held_mutex != 0) {
        mutex_unlock(curr->held_mutex);
    }

    for (int i = 0; i < MAX_FD_PER_TASK; i++) {
        if (curr->fd_table[i].type != 0) { // 0 = FD_TYPE_NONE
            if (curr->fd_table[i].type == 3 && curr->fd_table[i].ptr != 0) { // 3 = FD_TYPE_PIPE
                pipe_t *p = (pipe_t *)curr->fd_table[i].ptr;
                if (curr->fd_table[i].mode == 1) p->write_refs--; 
                else p->read_refs--;
                
                if (p->read_refs <= 0 && p->write_refs <= 0) {
destroy_pipe(p); 
                }
            }
            else if (curr->fd_table[i].type == 2 && curr->fd_table[i].ptr != 0) { // 2 = FD_TYPE_FILE
vfs_file_t *f = (vfs_file_t *)curr->fd_table[i].ptr;
                f->ref_count--;
                if (f->ref_count <= 0) {
                    kfree((void *)curr->fd_table[i].ptr);
                }
            }
            
            // Clear the FD cleanly
            curr->fd_table[i].type = 0;
            curr->fd_table[i].ptr = 0;
            curr->fd_table[i].mode = 0;
        }
    }

    cleanup_process_memory(curr->page_directory);

    int parent_pid = curr->parent_pid;
    curr->state = TASK_DEAD; 
    task_wait_mutex[current_task] = 0;

    int next_fg = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == parent_pid && tasks[i].state == TASK_WAITING && tasks[i].wait_reason == WAIT_CHILD) {
            tasks[i].state = TASK_RUNNING;
            tasks[i].wait_reason = WAIT_NONE;
            next_fg = i;
            break;
        }
    }

    if (next_fg == -1) {
        for (int i = 1; i < MAX_TASKS; i++) { 
            if (tasks[i].state == TASK_RUNNING || tasks[i].state == TASK_WAITING) {
                next_fg = i;
                break;
            }
        }
    }

    if (next_fg != -1) {
        foreground_task = next_fg;
    } else {
        klog(LOG_LEVEL_INFO, "KERNEL", "Last running process terminated. Halting system.");
        
        printk("\n\n=======================================================\n");
        printk("      esdumanOS SAFELY SHUT DOWN        \n");
        printk("      You may now safely turn off your computer.        \n");
        printk("=======================================================\n\n");
        
        asm volatile("cli; hlt");
    }
    
    curr->state = TASK_EMPTY;
    schedule(regs);
}

/**
 * @brief sleep_current_task
 * @param regs
 * @param reason
 */
void sleep_current_task(arch_regs_t *regs, int reason) {
    tasks[current_task].regs = *regs;
    tasks[current_task].state = TASK_WAITING;
    tasks[current_task].wait_reason = (wait_reason_t)reason;
    schedule(regs);
}

/**
 * @brief wakeup_tasks
 * @param reason
 */
void wakeup_tasks(int reason) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_WAITING && tasks[i].wait_reason == (wait_reason_t)reason) {
            tasks[i].state = TASK_RUNNING;
            tasks[i].wait_reason = WAIT_NONE;
        }
    }
}

/**
 * @brief schedule
 * @param regs
 */
void schedule(arch_regs_t *regs) {
    asm volatile("cli");
    if (!multitasking_enabled || current_task == -1) return;

    if ((regs->cs & 0x03) == 0) {
        return; 
    }
    
    tasks[current_task].regs = *regs;

    int next_task = 0;
    int max_priority = -1;

    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (current_task + i) % MAX_TASKS;
        if (idx == 0) continue; 

        if (tasks[idx].state == TASK_RUNNING) {
            if (idx != current_task && tasks[idx].current_priority < 250) {
                tasks[idx].current_priority++; 
            }

            if ((int)tasks[idx].current_priority > max_priority) {
                max_priority = tasks[idx].current_priority;
                next_task = idx;
            }
        }
    }

    if (next_task != 0) {
        tasks[next_task].current_priority = tasks[next_task].base_priority;
    }

    if (next_task == 0 && current_task == 0) return; 
    if (current_task == next_task) return; 

    if (current_task >= 0 && tasks[current_task].state != TASK_DEAD) {
        asm volatile("fxsave %0" : "=m"(tasks[current_task].fpu_state));
    }

    current_task = next_task;
    if (!tasks[current_task].fpu_initialized) {
        asm volatile("fninit");
        asm volatile("fxsave %0" : "=m"(tasks[current_task].fpu_state));
        tasks[current_task].fpu_initialized = 1;
    } else {
        asm volatile("fxrstor %0" : : "m"(tasks[current_task].fpu_state));
    }

    uint32_t k_stack_top = (((uint32_t)tasks[current_task].kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 
    asm volatile ("mov %0, %%cr3" : : "r"(tasks[current_task].page_directory));

    *regs = tasks[current_task].regs;
    regs->eflags |= 0x200;
    process_pending_kernel_timers();
    
    check_and_deliver_signals(regs);
}

/**
 * @brief mutex_init
 * @param m
 */
void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner_pid = -1;
}

/**
 * @brief mutex_lock
 * @param m
 * @param regs
 */
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
            task_wait_mutex[current_task] = 0;
            asm volatile("sti"); 
            return;
        }

        if (regs != 0 && (regs->cs & 0x03) != 0) {
            #define INT_0x80_SIZE 2
            regs->eip -= INT_0x80_SIZE;
            asm volatile("sti");
            
            task_wait_mutex[current_task] = m;
            sleep_current_task(regs, WAIT_MUTEX);
            return;
        } else {
            asm volatile("sti"); 
            asm volatile("nop");
            spin_count++;
            if (spin_count > 100000000) { 
                klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Spinlock Deadlock! Mutex owner PID", m->owner_pid);
                klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Locked Kernel PID", tasks[current_task].pid);
kernel_panic("Kernel Mutex Deadlock");
            }
        }
    }
}

/**
 * @brief mutex_unlock
 * @param m
 */
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

        int next_to_wake = -1;
        int max_prio = -1;

        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_WAITING && 
                tasks[i].wait_reason == WAIT_MUTEX && 
                task_wait_mutex[i] == m) {
                
                if ((int)tasks[i].current_priority > max_prio) {
                    max_prio = tasks[i].current_priority;
                    next_to_wake = i;
                }
            }
        }
        if (next_to_wake != -1) {
            tasks[next_to_wake].state = TASK_RUNNING;
            tasks[next_to_wake].wait_reason = WAIT_NONE;
            task_wait_mutex[next_to_wake] = 0;
        }
    }
    asm volatile("sti"); 
}
/**
 * @brief register_user_signal
 * @param sig_num
 * @param handler_addr
 */
void register_user_signal(int sig_num, uint32_t handler_addr) {
    if (current_task == -1 || sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    
    if (handler_addr != 0 && !validate_user_pointer((const void *)handler_addr, 1)) {
        klog_int(LOG_LEVEL_WARN, "SIGNAL", "Invalid signal handler address rejected! PID", tasks[current_task].pid);
        return;
    }
    
    tasks[current_task].signal_handlers[sig_num] = handler_addr;
}

/**
 * @brief send_user_signal
 * @param target_pid
 * @param sig_num
 */
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

/**
 * @brief check_and_deliver_signals
 * @param regs
 */
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
                    if (!validate_user_pointer((const void *)handler, 1)) {
                        klog_int(LOG_LEVEL_ERROR, "SIGNAL", "Invalid Handler Address detected during delivery! PID", curr->pid);
                        curr->signal_handlers[i] = 0;
                        return;
                    }
                    
                    curr->signal_saved_regs = *regs;
                    curr->in_signal_handler = 1;
                    regs->eip = handler;
                    
                    return; 
                }
            }
        }
    }
}
/**
 * @brief restore_signal_context
 * @param regs
 */
void restore_signal_context(arch_regs_t *regs) {
    if (current_task == -1) return;
    process_t *curr = &tasks[current_task];
    if (curr->in_signal_handler) {
        *regs = curr->signal_saved_regs;
        curr->in_signal_handler = 0;
    }
}

/**
 * @brief check_free_task_slot
 * @return int
 */
int check_free_task_slot(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_EMPTY || tasks[i].state == TASK_DEAD) return 1;
    }
    return 0;
}

/**
 * @brief start_first_task
 */
void start_first_task(void) {
    // [CRITICAL]: Freeze all interrupts until this operation completes! 
    // Otherwise Timer Interrupt might intervene and mess up the stack.
    asm volatile("cli"); 

    int first_task_idx = -1;

    if (foreground_task > 0 && tasks[foreground_task].state == TASK_RUNNING) {
        first_task_idx = foreground_task;
    } else {
        for (int i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].state == TASK_RUNNING) {
                first_task_idx = i;
                break;
            }
        }
    }

    if (first_task_idx == -1) {
        printk("[SCHEDULER WARNING] No user tasks to run. Switching to idle task.\n");
        first_task_idx = 0; 
    }

    current_task = first_task_idx; 
    
    uint32_t k_stack_top = (((uint32_t)tasks[current_task].kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 

    // Switch to the memory map of the new process
    asm volatile ("mov %0, %%cr3" : : "r"(tasks[current_task].page_directory));
    
    multitasking_enabled = 1;

    uint32_t eip = tasks[current_task].regs.eip;
    uint32_t esp = tasks[current_task].regs.useresp;
        
    // Since all processes (including Idle) are now in Ring 3, we use fixed values.
    uint32_t cs = 0x23; 
    uint32_t ds = 0x2B; 

    asm volatile(
        "movw %%ax, %%ds \n"
        "movw %%ax, %%es \n"
        "movw %%ax, %%fs \n"
        "movw %%ax, %%gs \n"
        "pushl %%eax \n"     
        "pushl %0 \n"        
        "pushl $0x202 \n"    
        "pushl %2 \n"        
        "pushl %1 \n"        
        "iret \n"            
        : : "r"(esp), "r"(eip), "r"(cs), "a"(ds)
    );
}