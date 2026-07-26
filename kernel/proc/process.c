/*
 * File: process.c
 * Purpose: Process management, context switching, and multitasking implementation (Dynamic Linked List).
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

process_t *task_list_head = 0;
process_t *task_list_tail = 0;
cpu_state_t cpus[MAX_CPUS];
int multitasking_enabled = 0;
int next_pid = 0;
int foreground_task = -1;
process_t *zombie_tasks_head = 0;

void init_multitasking(void) {
    task_list_head = 0;
    task_list_tail = 0;

    cpus[0].cpu_id = 0;
    cpus[0].is_bsp = 1;
    cpus[0].active_task = 0;

    uint32_t idle_phys = pmm_alloc_frame();
    map_page(0x80000000, idle_phys, 7); // 7 = User, RW, Present
    
    // Asm: mov eax, 99 (B8 63 00 00 00) | int 0x80 (CD 80) | jmp short -9 (EB F7)
    uint8_t idle_code[] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xEB, 0xF7 };
    uint8_t *idle_page = (uint8_t *)0x80000000;
    for(int i = 0; i < 9; i++) {
        idle_page[i] = idle_code[i];
    }

    uint32_t kernel_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(kernel_cr3));

    create_process(0x80000000, 0x80000000 + 4096 - 4, kernel_cr3);
    if (task_list_head) {
        task_list_head->base_priority = 0;
        task_list_head->current_priority = 0;
    }
}

int create_process(uint32_t eip, uint32_t esp, uint32_t cr3) {
    process_t *new_task = (process_t *)kmalloc(sizeof(process_t));
    if (!new_task) {
        klog(LOG_LEVEL_ERROR, "PROCESS", "Could not create new process: Out of memory.");
        return E_NOMEM; 
    }
    
    if (next_pid < 0 || next_pid == 0x7FFFFFFF) {
        next_pid = 2; 
    }
    new_task->pid = next_pid++;

    if (current_task == 0) {
        new_task->parent_pid = -1;
        new_task->uid = 0;
    } else {
        new_task->parent_pid = current_task->pid;
        new_task->uid = current_task->uid;
    }

    klog_int(LOG_LEVEL_DEBUG, "PROCESS", "New process created", new_task->pid);

    new_task->in_signal_handler = 0;
    new_task->state = TASK_RUNNING;
    new_task->wait_reason = WAIT_NONE;
    new_task->wait_mutex = 0;
    new_task->held_mutex = 0;
    new_task->pending_signals = 0;
    
    for (int k = 0; k < MAX_USER_SIGNALS; k++) {
        new_task->signal_handlers[k] = 0;
    }
    
    new_task->fd_table_size = 16;
    new_task->fd_table = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t) * 16);
    if (new_task->fd_table) {
        for (uint32_t fd_i = 0; fd_i < 16; fd_i++) {
            new_task->fd_table[fd_i].type = 0;
            new_task->fd_table[fd_i].ptr = 0;
            new_task->fd_table[fd_i].mode = 0;
            new_task->fd_table[fd_i].offset = 0;
        }
    }
    
    new_task->base_priority = 10;
    new_task->current_priority = 10;
    
    new_task->msg_head = 0;
    new_task->msg_tail = 0;
    new_task->msg_count = 0;

    uint8_t *kstack_ptr = (uint8_t *)new_task->kstack;
    for (int j = 0; j < 4096; j++) {
        kstack_ptr[j] = 0;
    }

    new_task->page_directory = cr3;

    uint8_t *ptr = (uint8_t *)&new_task->regs;
    for (uint32_t j = 0; j < sizeof(arch_regs_t); j++) ptr[j] = 0;

    new_task->regs.cs = GDT_USER_CS;
    new_task->regs.ds = GDT_USER_DS;
    new_task->regs.ss = GDT_USER_DS;
    
    new_task->regs.eip = eip;         
    new_task->regs.useresp = esp;     
    new_task->regs.eflags = EFLAGS_DEFAULT;
    new_task->regs.int_no = 32;
    new_task->regs.err_code = 0;

    new_task->fpu_initialized = 0;
    new_task->auth_fail_ticks = 0;
    new_task->next = 0;
    new_task->prev = 0;

    // Append to list
    if (!task_list_head) {
        task_list_head = new_task;
        task_list_tail = new_task;
    } else {
        task_list_tail->next = new_task;
        new_task->prev = task_list_tail;
        task_list_tail = new_task;
    }

    if (current_task == 0) current_task = new_task;

    return new_task->pid;
}

void set_task_priority(int pid, uint8_t new_priority) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            p->base_priority = new_priority;
            p->current_priority = new_priority;
            return;
        }
    }
}

int send_message(int target_pid, uint32_t payload) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == target_pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            if (p->msg_count >= MAX_MESSAGES) {
                klog_int(LOG_LEVEL_WARN, "IPC", "Message could not be sent: Target queue full (PID)", target_pid);
                return E_BUSY;
            }
            
            int head = p->msg_head;
            p->mailbox[head].sender_pid = current_task->pid;
            p->mailbox[head].payload = payload;

            p->msg_head = (head + 1) % MAX_MESSAGES;
            p->msg_count++;
            return E_OK;
        }
    }
    klog_int(LOG_LEVEL_WARN, "IPC", "Message could not be sent: Target process not found (PID)", target_pid);
    return E_NOENT;
}

int receive_message(uint32_t *sender_out, uint32_t *payload_out) {
    if (current_task == 0 || current_task->msg_count == 0)
        return E_NOENT;
    
    int tail = current_task->msg_tail;
    *sender_out = current_task->mailbox[tail].sender_pid;
    *payload_out = current_task->mailbox[tail].payload;

    current_task->msg_tail = (tail + 1) % MAX_MESSAGES;
    current_task->msg_count--;
    return E_OK;
}

void cleanup_process_memory(uint32_t page_directory_phys) {
    uint32_t curr_pd = 0;
    asm volatile("mov %%cr3, %0" : "=r"(curr_pd));
    if (page_directory_phys != 0 && page_directory_phys != curr_pd) {
        
        uint32_t old_cr3;
        asm volatile("mov %%cr3, %0" : "=r"(old_cr3));

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

        if ((old_cr3 & 0xFFFFF000) == (page_directory_phys & 0xFFFFF000)) {
            if (current_task) asm volatile("mov %0, %%cr3" :: "r"((uint32_t)current_task->page_directory));
        } else {
            asm volatile("mov %0, %%cr3" :: "r"(old_cr3));
        }

        pmm_free_frame(page_directory_phys);
        
        klog(LOG_LEVEL_INFO, "PMM", "User process memory (PD, PT, PTE) fully reclaimed.");
    }
}

void exit_current_process(arch_regs_t *regs) {
    if (current_task == 0) return;
    process_t *curr = current_task;

    if (curr->held_mutex != 0) {
        mutex_unlock(curr->held_mutex);
    }

    for (uint32_t i = 0; i < curr->fd_table_size; i++) {
        if (curr->fd_table && curr->fd_table[i].type != 0) {
            if (curr->fd_table[i].type == 3 && curr->fd_table[i].ptr != 0) {
                pipe_t *p = (pipe_t *)curr->fd_table[i].ptr;
                if (curr->fd_table[i].mode == 1) p->write_refs--; 
                else p->read_refs--;
                
                if (p->read_refs <= 0 && p->write_refs <= 0) {
                    destroy_pipe(p); 
                }
            }
            else if (curr->fd_table[i].type == 2 && curr->fd_table[i].ptr != 0) {
                vfs_file_t *f = (vfs_file_t *)curr->fd_table[i].ptr;
                f->ref_count--;
                if (f->ref_count <= 0) {
                    kfree((void *)curr->fd_table[i].ptr);
                }
            }
            curr->fd_table[i].type = 0;
            curr->fd_table[i].ptr = 0;
            curr->fd_table[i].mode = 0;
        }
    }
    
    if (curr->fd_table) {
        kfree(curr->fd_table);
        curr->fd_table = 0;
    }

    cleanup_process_memory(curr->page_directory);

    int parent_pid = curr->parent_pid;
    curr->state = TASK_DEAD; 
    curr->wait_mutex = 0;

    int next_fg = -1;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == parent_pid && p->state == TASK_WAITING && p->wait_reason == WAIT_CHILD) {
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
            next_fg = p->pid;
            break;
        }
    }

    if (next_fg == -1) {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p != curr && (p->state == TASK_RUNNING || p->state == TASK_WAITING)) {
                next_fg = p->pid;
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
    
    // Remove from linked list
    if (curr->prev) curr->prev->next = curr->next;
    else task_list_head = curr->next;
    
    if (curr->next) curr->next->prev = curr->prev;
    else task_list_tail = curr->prev;
    
    
    // Switch to next task before freeing curr memory!
    // But schedule uses current_task. We must set current_task to 0 so schedule picks another.
    
    curr->next = zombie_tasks_head;
    zombie_tasks_head = curr;

    current_task = 0; 
    
    schedule(regs);
}

void sleep_current_task(arch_regs_t *regs, int reason) {
    if (current_task == 0) return;
    current_task->regs = *regs;
    current_task->state = TASK_WAITING;
    current_task->wait_reason = (wait_reason_t)reason;
    schedule(regs);
}

void wakeup_tasks(int reason) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state == TASK_WAITING && p->wait_reason == (wait_reason_t)reason) {
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
        }
    }
}

void schedule(arch_regs_t *regs) {
    asm volatile("cli");

    uint32_t current_esp;
    asm volatile("mov %%esp, %0" : "=r"(current_esp));

    process_t *prev_zombie = 0;
    process_t *zombie = zombie_tasks_head;
    
    while (zombie != 0) {
        uint32_t stack_start = (uint32_t)zombie->kstack;
        uint32_t stack_end = stack_start + 4096;
        
        if (current_esp >= stack_start && current_esp < stack_end) {
            prev_zombie = zombie;
            zombie = zombie->next;
        } else {
            process_t *to_free = zombie;
            zombie = zombie->next;
            
            if (prev_zombie) prev_zombie->next = zombie;
            else zombie_tasks_head = zombie;
            
            kfree(to_free);
        }
    }

    if (!multitasking_enabled || task_list_head == 0) return;

    if (regs && (regs->cs & 0x03) == 0) {
        return; 
    }
    
    if (current_task != 0 && regs) {
        current_task->regs = *regs;
    }

    process_t *next_task = 0;
    int max_priority = -1;

    // Start searching from the task after current_task
    process_t *start_node = (current_task && current_task->next) ? current_task->next : task_list_head;
    process_t *p = start_node;
    
    // First pass
    while (p) {
        if (p->state == TASK_RUNNING) {
            if (p != current_task && p->current_priority < 250) {
                p->current_priority++; 
            }
            if ((int)p->current_priority > max_priority) {
                max_priority = p->current_priority;
                next_task = p;
            }
        }
        p = p->next;
    }
    // Second pass (wrap around)
    p = task_list_head;
    while (p != start_node && p != 0) {
        if (p->state == TASK_RUNNING) {
            if (p != current_task && p->current_priority < 250) {
                p->current_priority++; 
            }
            if ((int)p->current_priority > max_priority) {
                max_priority = p->current_priority;
                next_task = p;
            }
        }
        p = p->next;
    }

    if (next_task != 0) {
        next_task->current_priority = next_task->base_priority;
    } else {
        if (current_task && current_task->state == TASK_RUNNING) next_task = current_task;
        else next_task = task_list_head; // fallback to idle task
    }

    if (current_task == next_task) return; 

    if (current_task != 0 && current_task->state != TASK_DEAD) {
        asm volatile("fxsave %0" : "=m"(current_task->fpu_state));
    }

    current_task = next_task;
    if (!current_task->fpu_initialized) {
        asm volatile("fninit");
        asm volatile("fxsave %0" : "=m"(current_task->fpu_state));
        current_task->fpu_initialized = 1;
    } else {
        asm volatile("fxrstor %0" : : "m"(current_task->fpu_state));
    }

    uint32_t k_stack_top = (((uint32_t)current_task->kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 
    asm volatile ("mov %0, %%cr3" : : "r"(current_task->page_directory));

    if (regs) *regs = current_task->regs;
    if (regs) regs->eflags |= 0x200;
    process_pending_kernel_timers();
    
    check_and_deliver_signals(regs);
}

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner_pid = -1;
}

void mutex_lock(mutex_t *m, arch_regs_t *regs) {
    if (!multitasking_enabled || current_task == 0) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;
    uint32_t spin_count = 0; 

    while (1) {
        asm volatile("cli"); 

        if (m->locked == 0) {
            m->locked = 1;
            m->owner_pid = current_task->pid;
            current_task->held_mutex = m;
            current_task->wait_mutex = 0;
            asm volatile("sti"); 
            return;
        }

        if (regs != 0 && (regs->cs & 0x03) != 0) {
            regs->eip -= 2;
            asm volatile("sti");
            
            current_task->wait_mutex = m;
            sleep_current_task(regs, WAIT_MUTEX);
            return;
        } else {
            asm volatile("sti"); 
            asm volatile("nop");
            spin_count++;
            if (spin_count > 100000000) { 
                klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Spinlock Deadlock! Mutex owner PID", m->owner_pid);
                klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Locked Kernel PID", current_task->pid);
                kernel_panic("Kernel Mutex Deadlock");
            }
        }
    }
}

void mutex_unlock(mutex_t *m) {
    if (!multitasking_enabled || current_task == 0) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;

    asm volatile("cli"); 
    
    if (m->locked == 1 && m->owner_pid == current_task->pid) {
        m->locked = 0;
        m->owner_pid = -1;
        current_task->held_mutex = 0;

        process_t *next_to_wake = 0;
        int max_prio = -1;

        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->state == TASK_WAITING && p->wait_reason == WAIT_MUTEX && p->wait_mutex == m) {
                if ((int)p->current_priority > max_prio) {
                    max_prio = p->current_priority;
                    next_to_wake = p;
                }
            }
        }
        if (next_to_wake != 0) {
            next_to_wake->state = TASK_RUNNING;
            next_to_wake->wait_reason = WAIT_NONE;
            next_to_wake->wait_mutex = 0;
        }
    }
    asm volatile("sti"); 
}

void register_user_signal(int sig_num, uint32_t handler_addr) {
    if (current_task == 0 || sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    
    if (handler_addr != 0 && !validate_user_pointer((const void *)handler_addr, 1)) {
        klog_int(LOG_LEVEL_WARN, "SIGNAL", "Invalid signal handler address rejected! PID", current_task->pid);
        return;
    }
    
    current_task->signal_handlers[sig_num] = handler_addr;
}

void send_user_signal(int target_pid, int sig_num) {
    if (sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == target_pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            p->pending_signals |= (1 << sig_num);
            if (p->state == TASK_WAITING) {
                p->state = TASK_RUNNING;
                p->wait_reason = WAIT_NONE;
            }
            return;
        }
    }
}

void check_and_deliver_signals(arch_regs_t *regs) {
    if (current_task == 0 || regs == 0) return;
    process_t *curr = current_task;
    
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

void restore_signal_context(arch_regs_t *regs) {
    if (current_task == 0) return;
    process_t *curr = current_task;
    if (curr->in_signal_handler && regs != 0) {
        *regs = curr->signal_saved_regs;
        curr->in_signal_handler = 0;
    }
}

int check_free_task_slot(void) {
    // We are dynamic now! Always space (unless OOM).
    return 1;
}

void start_first_task(void) {
    asm volatile("cli"); 

    process_t *first_task = 0;

    if (foreground_task > 0) {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == foreground_task && p->state == TASK_RUNNING) {
                first_task = p;
                break;
            }
        }
    } 
    
    if (!first_task) {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid != 0 && p->state == TASK_RUNNING) {
                first_task = p;
                break;
            }
        }
    }

    if (!first_task) {
        printk("[SCHEDULER WARNING] No user tasks to run. Switching to idle task.\n");
        first_task = task_list_head; 
    }

    current_task = first_task; 
    if (!current_task) kernel_panic("No tasks found!");
    
    uint32_t k_stack_top = (((uint32_t)current_task->kstack + 4096) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 

    asm volatile ("mov %0, %%cr3" : : "r"(current_task->page_directory));
    
    multitasking_enabled = 1;

    uint32_t eip = current_task->regs.eip;
    uint32_t esp = current_task->regs.useresp;
        
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

void rwlock_init(rwlock_t *lock) {
    lock->readers = 0;
    lock->writer_active = 0;
    mutex_init(&lock->mutex);
}

void rwlock_acquire_read(rwlock_t *lock, arch_regs_t *regs) {
    mutex_lock(&lock->mutex, regs);
    lock->readers++;
    mutex_unlock(&lock->mutex);
}

void rwlock_release_read(rwlock_t *lock) {
    mutex_lock(&lock->mutex, 0);
    lock->readers--;
    mutex_unlock(&lock->mutex);
}

void rwlock_acquire_write(rwlock_t *lock, arch_regs_t *regs) {
    mutex_lock(&lock->mutex, regs);
    // Simple implementation: wait until readers are 0.
    while (lock->readers > 0) {
        mutex_unlock(&lock->mutex);
        if (regs != 0 && (regs->cs & 0x03) != 0) {
            regs->eip -= 2;
            sleep_current_task(regs, WAIT_MUTEX); 
            return;
        } else {
            // Kernel thread busy wait
            for (int i = 0; i < 1000; i++) asm volatile("pause");
        }
        mutex_lock(&lock->mutex, regs);
    }
    lock->writer_active = 1;
}

void rwlock_release_write(rwlock_t *lock) {
    lock->writer_active = 0;
    mutex_unlock(&lock->mutex);
}
