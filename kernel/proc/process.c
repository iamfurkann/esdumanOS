/**
 * @file process.c
 * @brief Process management, context switching, and multitasking implementation.
 */
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

/*
 * The idle task. Created by init_multitasking(), never exits, and always
 * runnable - it is the guarantee that the scheduler always has something to
 * pick. Kept as a named pointer rather than assumed to be task_list_head.
 */
process_t *idle_task = 0;

/**
 * @brief Initialize the multitasking system.
 */
void init_multitasking(void) {
    task_list_head = 0;
    task_list_tail = 0;

    cpus[0].cpu_id = 0;
    cpus[0].is_bsp = 1;
    cpus[0].active_task = 0;

    uint32_t idle_phys = pmm_alloc_frame();
    if (idle_phys == 0xFFFFFFFF) {
        kernel_panic("init_multitasking: out of memory allocating the idle page");
    }

    /*
     * Fill the idle loop in while the page is still kernel-only, and only then
     * hand it to Ring 3.
     *
     * Mapping it user-accessible first and writing the code afterwards is a
     * supervisor write to a user page. That works with SMAP off, which is why
     * it went unnoticed, but faults the instant SMAP is enabled - a page fault
     * at 0x80000000 with error code 3 (present, write, supervisor) during boot.
     */
    map_page(0x80000000, idle_phys, 3); // Present | Read/Write, kernel only

    // Asm: mov eax, 99 (B8 63 00 00 00) | int 0x80 (CD 80) | jmp short -9 (EB F7)
    uint8_t idle_code[] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xCD, 0x80, 0xEB, 0xF7 };
    uint8_t *idle_page = (uint8_t *)0x80000000;
    for(int i = 0; i < 9; i++) {
        idle_page[i] = idle_code[i];
    }

    map_page(0x80000000, idle_phys, 7); // ...then Present | Read/Write | User

    uint32_t kernel_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(kernel_cr3));

    create_process(0x80000000, 0x80000000 + 4096 - 4, kernel_cr3);
    if (task_list_head) {
        task_list_head->base_priority = 0;
        task_list_head->current_priority = 0;
        idle_task = task_list_head;
    }
}

/**
 * @brief Create a new process.
 * 
 * @param eip Instruction pointer (entry point).
 * @param esp Stack pointer.
 * @param cr3 Page directory physical address.
 * @return The PID of the newly created process, or a negative error code.
 */
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

    /*
     * cwd_id is inherited alongside uid. The very first task has no creator to
     * inherit from and starts at root; every later task starts where its creator
     * was standing, which is what makes "cd somewhere && run something" behave the
     * way a shell user expects.
     */
    if (current_task == 0) {
        new_task->parent_pid = -1;
        new_task->uid = 0;
        new_task->cwd_id = 0;
    } else {
        new_task->parent_pid = current_task->pid;
        new_task->uid = current_task->uid;
        new_task->cwd_id = current_task->cwd_id;
    }

    klog_int(LOG_LEVEL_DEBUG, "PROCESS", "New process created", new_task->pid);

    new_task->in_signal_handler = 0;
    new_task->state = TASK_RUNNING;
    new_task->wait_reason = WAIT_NONE;
    new_task->sleep_deadline = 0;
    new_task->sleep_active = 0;
    new_task->exit_code = 0;
    new_task->wait_mutex = 0;
    new_task->held_mutex = 0;
    new_task->pending_signals = 0;
    
    for (int k = 0; k < MAX_USER_SIGNALS; k++) {
        new_task->signal_handlers[k] = 0;
    }
    
    /*
     * A task with no descriptor table must not be published.
     *
     * fd_table_size was set to 16 unconditionally and the allocation failure
     * only skipped the initialisation loop - no error return, no cleanup. The
     * task was then linked into the run list and scheduled, and its first
     * read/write/open/close indexed a NULL table from Ring 0: a kernel page
     * fault with no fixup, which parks the CPU. The process_t allocation a few
     * lines above is checked properly; this one was not.
     */
    new_task->fd_table_size = 0;
    new_task->fd_table = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t) * 16);
    if (!new_task->fd_table) {
        klog(LOG_LEVEL_ERROR, "PROCESS", "Could not create new process: no memory for the descriptor table.");
        kfree(new_task);
        return E_NOMEM;
    }

    new_task->fd_table_size = 16;
    for (uint32_t fd_i = 0; fd_i < 16; fd_i++) {
        new_task->fd_table[fd_i].type = 0;
        new_task->fd_table[fd_i].ptr = 0;
        new_task->fd_table[fd_i].mode = 0;
        new_task->fd_table[fd_i].offset = 0;
    }
    
    new_task->base_priority = 10;
    new_task->current_priority = 10;
    
    new_task->msg_head = 0;
    new_task->msg_tail = 0;
    new_task->msg_count = 0;

    uint8_t *kstack_ptr = (uint8_t *)new_task->kstack;
    for (int j = 0; j < KERNEL_STACK_SIZE; j++) {
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
    new_task->in_syscall = 0;
    new_task->syscall_entry_eip = 0;
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

/**
 * @brief Set the priority of a task.
 * 
 * @param pid Process ID.
 * @param new_priority The new priority value.
 */
void set_task_priority(int pid, uint8_t new_priority) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            p->base_priority = new_priority;
            p->current_priority = new_priority;
            return;
        }
    }
}

/**
 * @brief Send a message to another process.
 * 
 * @param target_pid The target process ID.
 * @param payload The message payload.
 * @return E_OK on success, or a negative error code.
 */
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

/**
 * @brief Receive a message for the current process.
 * 
 * @param sender_out Pointer to store the sender's PID.
 * @param payload_out Pointer to store the received payload.
 * @return E_OK on success, or a negative error code.
 */
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

/**
 * @brief Clean up the memory of a process.
 * 
 * @param page_directory_phys Physical address of the page directory to clean up.
 */
void cleanup_process_memory(uint32_t page_directory_phys) {
    uint32_t curr_pd = 0;
    asm volatile("mov %%cr3, %0" : "=r"(curr_pd));
    curr_pd &= 0xFFFFF000;
    page_directory_phys &= 0xFFFFF000;

    if (page_directory_phys == 0) {
        return;
    }

    /* Tearing down the live address space would pull the page tables out from
     * under the running code. Callers must switch CR3 away first. */
    if (page_directory_phys == curr_pd) {
        klog(LOG_LEVEL_ERROR, "PMM", "Refused to reclaim the address space currently in CR3.");
        return;
    }

    /* The kernel directory is shared by the idle task and by every early boot
     * path; freeing it would take the whole system with it. */
    if (page_directory_phys == ((uint32_t)page_directory & 0xFFFFF000)) {
        klog(LOG_LEVEL_ERROR, "PMM", "Refused to reclaim the kernel page directory.");
        return;
    }

    asm volatile("mov %0, %%cr3" :: "r"(page_directory_phys));

    /*
     * Entries 0..767 are this process's own; entries 768..1022 are the kernel
     * half, shared with every other address space, and entry 1023 is the
     * recursive slot pointing at the directory itself. Only the first range may
     * be freed. The scan starts at 0, not 4: user space begins at 4 MB, which
     * falls inside directory entry 1, so entries 1..3 hold real user page
     * tables and skipping them leaked one frame per table.
     */
    uint32_t *pd_virt = (uint32_t *)0xFFFFF000;
    for (int i = 0; i < 768; i++) {
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

    asm volatile("mov %0, %%cr3" :: "r"(curr_pd));

    pmm_free_frame(page_directory_phys);

    klog(LOG_LEVEL_DEBUG, "PMM", "User process memory (PD, PT, PTE) fully reclaimed.");
}

/**
 * @brief Exit the current process and clean up its resources.
 * 
 * @param regs Pointer to the current CPU registers.
 */
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
                /* An exiting task dropping its end is the same event a close is;
                 * without this a peer blocked in pipe_read()/pipe_write() waited
                 * for the rest of the boot. See sys_close(). */
                wakeup_tasks(WAIT_IPC);
            }
            else if (curr->fd_table[i].type == 2 && curr->fd_table[i].ptr != 0) {
                /* Same commit-on-last-reference rule as sys_close(): a program
                 * that writes and then exits without closing would otherwise
                 * lose everything it wrote. Nothing can be reported from here,
                 * so a failure is logged by fs_commit_writes() itself. */
                vfs_file_t *f = (vfs_file_t *)curr->fd_table[i].ptr;
                f->ref_count--;
                if (f->ref_count <= 0) {
                    fs_commit_writes(f);
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

    /*
     * The address space is NOT reclaimed here. This code is still executing on
     * curr's kernel stack with curr's directory in CR3, so freeing its page
     * tables would unmap the ground we are standing on. The task is handed to
     * the zombie list below and the reaper in schedule() releases both the
     * process_t and the address space once another task's directory is live.
     */
    int parent_pid = curr->parent_pid;
    curr->state = TASK_DEAD; 
    curr->wait_mutex = 0;

    int next_fg = -1;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == parent_pid && p->state == TASK_WAITING && p->wait_reason == WAIT_CHILD) {
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;

            /*
             * Hand the child's status up.
             *
             * Written into the parent's *saved* frame, not a live one: the
             * parent is blocked, so schedule() will restore this copy into the
             * real frame when it next runs, and sys_exec()'s return value is
             * whatever sits in eax at that moment. Writing anywhere else - or
             * writing after the parent had already resumed - is the mistake
             * sys_exec() documents in its own comment about publishing before
             * sleeping.
             *
             * Until this landed the parent kept the E_OK sys_exec() had put
             * there before blocking, so every program appeared to succeed.
             */
            p->regs.eax = (uint32_t)curr->exit_code;

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

/**
 * @brief Put the current task to sleep.
 * 
 * @param regs Pointer to the current CPU registers.
 * @param reason The reason for sleeping.
 */
void sleep_current_task(arch_regs_t *regs, int reason) {
    if (current_task == 0) return;
    current_task->regs = *regs;
    current_task->state = TASK_WAITING;
    current_task->wait_reason = (wait_reason_t)reason;
    schedule(regs);
}

/**
 * @brief Blocks the current task so that its syscall re-runs when it wakes.
 *
 * @param regs   Live interrupt frame of the calling task.
 * @param reason Why the task is blocking.
 * @return 1 when the task was blocked, 0 when it could not be.
 */
int syscall_block_and_restart(arch_regs_t *regs, wait_reason_t reason) {
    /*
     * All three conditions are necessary:
     *
     *  - in_syscall: the frame has to belong to a syscall. An interrupt frame
     *    from Ring 3 looks identical here, and rewinding one drops the user's
     *    EIP into the middle of whatever instruction was running.
     *  - trap_frame_is_live: it has to be the frame on this task's kernel
     *    stack, not a saved copy of one (see trap_frame_is_live).
     *  - CPL 3: resuming it has to re-enter the kernel through the trap.
     */
    if (current_task == 0 || !current_task->in_syscall) return 0;
    if (!trap_frame_is_live(regs) || (regs->cs & 0x03) == 0) return 0;

    /*
     * Resume on the trap instruction rather than after it, using the address
     * recorded at syscall entry. The syscall simply runs again and this time
     * finds whatever it was waiting for.
     */
    regs->eip = current_task->syscall_entry_eip;

    sleep_current_task(regs, (int)reason);
    return 1;
}

/**
 * @brief Wake up tasks that are sleeping for a specific reason.
 * 
 * @param reason The reason to wake up tasks for.
 */
void wakeup_tasks(int reason) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state == TASK_WAITING && p->wait_reason == (wait_reason_t)reason) {
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
        }
    }
}

/**
 * @brief Wake every task whose sleep() deadline has passed.
 *
 * Deliberately not wakeup_tasks(WAIT_TIMER): that wakes everyone waiting on a
 * reason at once, which is the right shape for "a key arrived" and the wrong one
 * for sleeping, where each task asked for a different moment. The deadline lives
 * in the PCB and is checked per task here.
 *
 * The subtraction is signed on purpose. timer_ticks is a 32-bit counter that
 * wraps after roughly 497 days at TIMER_HZ; comparing the raw values with >=
 * would leave a task that armed its deadline just before the wrap asleep until
 * the counter came all the way back round.
 */
void wake_expired_sleepers(void) {
    uint32_t now = timer_get_ticks();

    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state != TASK_WAITING || p->wait_reason != WAIT_TIMER) continue;
        if (!p->sleep_active) continue;

        if ((int32_t)(now - p->sleep_deadline) >= 0) {
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
        }
    }
}

/**
 * @brief Schedule the next task to run.
 * 
 * @param regs Pointer to the current CPU registers.
 */
void schedule(arch_regs_t *regs) {
    asm volatile("cli");

    uint32_t current_esp;
    asm volatile("mov %%esp, %0" : "=r"(current_esp));

    /*
     * Reap exited tasks. A zombie is skipped while we are still running on its
     * kernel stack (exit_current_process() calls schedule() from exactly that
     * position); it is picked up on the next switch, when some other task's
     * stack and directory are live.
     *
     * CR3 still holds the directory of whichever task entered schedule(), and a
     * zombie that is not the one we are standing on is never that task, so
     * cleanup_process_memory() can safely tear its address space down here.
     */
    process_t *prev_zombie = 0;
    process_t *zombie = zombie_tasks_head;

    while (zombie != 0) {
        uint32_t stack_start = (uint32_t)zombie->kstack;
        uint32_t stack_end = stack_start + KERNEL_STACK_SIZE;

        if (current_esp >= stack_start && current_esp < stack_end) {
            prev_zombie = zombie;
            zombie = zombie->next;
        } else {
            process_t *to_free = zombie;
            zombie = zombie->next;

            if (prev_zombie) prev_zombie->next = zombie;
            else zombie_tasks_head = zombie;

            cleanup_process_memory(to_free->page_directory);
            kfree(to_free);
        }
    }

    if (!multitasking_enabled || task_list_head == 0) return;

    /*
     * The kernel is deliberately not preemptible: a task that entered the
     * kernel runs until it returns to user mode or blocks of its own accord.
     * A frame from Ring 0 is therefore never rescheduled.
     *
     * This is what makes kernel code implicitly atomic with respect to other
     * tasks, and most of the kernel depends on it - bcache, the pipe pool and
     * the ATA driver have no locks at all, the task list is edited without
     * masking interrupts, pmm_lock spins without disabling them, and the
     * uaccess fault state is a single global. Removing this guard alone would
     * not add preemption, it would remove that guarantee.
     *
     * arch_regs_t is also unable to describe a Ring 0 frame: the processor does
     * not push SS:ESP on a same-privilege interrupt, so the last two fields
     * alias the interrupted kernel stack. Preemption needs the frame layout
     * fixed before anything else.
     */
    if (regs && (regs->cs & 0x03) == 0) {
        return;
    }

    /*
     * Bottom halves, before a task is picked rather than after one has been.
     *
     * process_pending_kernel_timers() used to be called at the very end of this
     * function, which put it behind the "current_task == next_task" early return
     * below: whenever the same task was reselected - the normal case while only
     * the idle task is runnable, with the shell blocked on WAIT_KBD - armed
     * timers were counted down by the tick handler and then never run. It also
     * sat after CR3 had been loaded with the incoming task's directory while
     * still executing on the outgoing task's kernel stack, which happens to be
     * harmless for callbacks that only touch kernel mappings and is not a
     * property worth depending on.
     *
     * Both now run on every entry, on the caller's own stack and directory, and
     * before the selection passes - so a task either of them makes runnable can
     * be chosen in this same pass instead of waiting for the next one. That
     * ordering is load-bearing for wake_expired_sleepers(): a sweep after the
     * selection would leave a due sleeper waiting one more scheduling round.
     *
     * Running either from IRQ0 instead would mean walking the task list from an
     * interrupt, and that list is edited without masking interrupts - see the
     * note above. This is the same reasoning that made kernel timers a bottom
     * half in the first place.
     *
     * Progress does not depend on another task being awake: the idle task is a
     * Ring 3 loop of "mov eax, SYSCALL_YIELD; int 0x80" (init_multitasking()),
     * so schedule() keeps being entered even when every other task is sleeping.
     */
    process_pending_kernel_timers();
    wake_expired_sleepers();

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
    } else if (current_task && current_task->state == TASK_RUNNING) {
        next_task = current_task;
    } else if (idle_task && idle_task->state == TASK_RUNNING) {
        /*
         * Nothing else is runnable, so fall back to the idle task.
         *
         * This used to be "sti; hlt; return". Returning leaves *regs untouched,
         * so the iret at the end of the interrupt stub resumed the very task
         * that had just been found unrunnable: a task that had blocked would
         * carry on executing as though it never had. The idle task exists
         * precisely so there is always somewhere to go instead.
         */
        next_task = idle_task;
    } else {
        /* No runnable task and no idle task: the invariant init_multitasking()
         * establishes has been broken, and there is nothing safe to resume. */
        kernel_panic("schedule: no runnable task and no idle task");
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

    uint32_t k_stack_top = (((uint32_t)current_task->kstack + KERNEL_STACK_SIZE) & 0xFFFFFFF0) - 4;
    set_kernel_stack(k_stack_top); 
    asm volatile ("mov %0, %%cr3" : : "r"(current_task->page_directory));

    if (regs) *regs = current_task->regs;
    if (regs) regs->eflags |= 0x200;

    check_and_deliver_signals(regs);
}

/**
 * @brief Initialize a mutex.
 * 
 * @param m Pointer to the mutex to initialize.
 */
void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner_pid = -1;
}

/**
 * @brief Tells a live interrupt frame from a saved copy of one.
 *
 * Only the frame the ISR stub pushed on the current task's kernel stack can be
 * used for sleeping: going to sleep rewinds its EIP and hands it to schedule(),
 * which overwrites it in place with the incoming task's context.
 *
 * A saved copy looks identical field for field. &current_task->regs in
 * particular used to be passed here by the VFS, and the result was silent
 * corruption: sleep_current_task() copied the frame onto itself, schedule()
 * wrote the next task's context into the *previous* task's PCB, and CR3 and
 * current_task ended up describing different tasks while the real frame on the
 * kernel stack went untouched. Callers without the live frame must pass 0.
 *
 * @param regs Candidate frame.
 * @return 1 when regs lies wholly inside the current task's kernel stack.
 */
int trap_frame_is_live(const arch_regs_t *regs) {
    if (regs == 0 || current_task == 0) return 0;

    uint32_t addr = (uint32_t)regs;
    uint32_t base = (uint32_t)current_task->kstack;

    return addr >= base && (addr + sizeof(arch_regs_t)) <= (base + KERNEL_STACK_SIZE);
}

/**
 * @brief Acquire a lock on a mutex.
 *
 * @param m Pointer to the mutex to lock.
 * @param regs Live interrupt frame of the calling task, or 0 when the caller
 *             does not have one. A frame that is not on the current kernel
 *             stack is treated as absent rather than trusted.
 */
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

        asm volatile("sti");

        /*
         * Prefer to sleep. syscall_block_and_restart() refuses unless this is
         * genuinely a Ring 3 syscall frame belonging to this task, so a frame
         * that arrived any other way can never have its EIP rewound.
         */
        current_task->wait_mutex = m;
        if (syscall_block_and_restart(regs, WAIT_MUTEX)) {
            return;
        }
        current_task->wait_mutex = 0;

        /* Cannot block here: spin, and report if it is clearly a deadlock. */
        asm volatile("nop");
        spin_count++;
        if (spin_count > 100000000) {
            klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Spinlock Deadlock! Mutex owner PID", m->owner_pid);
            klog_int(LOG_LEVEL_CRITICAL, "MUTEX", "Locked Kernel PID", current_task->pid);
            kernel_panic("Kernel Mutex Deadlock");
        }
    }
}

/**
 * @brief Release a lock on a mutex.
 * 
 * @param m Pointer to the mutex to unlock.
 */
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

/**
 * @brief Register a signal handler for the current user process.
 * 
 * @param sig_num The signal number.
 * @param handler_addr Address of the signal handler function.
 */
void register_user_signal(int sig_num, uint32_t handler_addr) {
    if (current_task == 0 || sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    
    if (handler_addr != 0 && !validate_user_pointer((const void *)handler_addr, 1)) {
        klog_int(LOG_LEVEL_WARN, "SIGNAL", "Invalid signal handler address rejected! PID", current_task->pid);
        return;
    }
    
    current_task->signal_handlers[sig_num] = handler_addr;
}

/**
 * @brief Send a signal to a user process.
 * 
 * @param target_pid The target process ID.
 * @param sig_num The signal number.
 */
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

/**
 * @brief Check for and deliver pending signals to the current process.
 * 
 * @param regs Pointer to the current CPU registers.
 */
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

/**
 * @brief Restore the context after returning from a signal handler.
 * 
 * @param regs Pointer to the current CPU registers.
 */
void restore_signal_context(arch_regs_t *regs) {
    if (current_task == 0) return;
    process_t *curr = current_task;
    if (curr->in_signal_handler && regs != 0) {
        *regs = curr->signal_saved_regs;
        curr->in_signal_handler = 0;
    }
}

/**
 * @brief Check whether another process may be created.
 *
 * The task list is a dynamically allocated linked list, so there is no fixed
 * table to run out of — but a ceiling is still enforced. Every process costs a
 * process_t on the kernel heap (~5 KB, including its 4 KB kernel stack and FPU
 * save area) plus a page directory, its page tables and 32 pages of user stack.
 * Without a cap an unprivileged loop of exec() exhausts the kernel heap, and a
 * kmalloc() failure there is far harder to survive than a rejected exec.
 *
 * Only live tasks count: exit_current_process() unlinks a task before handing it
 * to the zombie reaper, so slots come back as processes terminate.
 *
 * @return 1 when another process fits, 0 once MAX_TASKS live tasks exist.
 */
int check_free_task_slot(void) {
    int live = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            live++;
        }
    }
    return live < MAX_TASKS;
}

/**
 * @brief Start executing the first task.
 */
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
    
    uint32_t k_stack_top = (((uint32_t)current_task->kstack + KERNEL_STACK_SIZE) & 0xFFFFFFF0) - 4;
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

/**
 * @brief Initialize a read-write lock.
 * 
 * @param lock Pointer to the read-write lock.
 */
void rwlock_init(rwlock_t *lock) {
    lock->readers = 0;
    lock->writer_active = 0;
    mutex_init(&lock->mutex);
}

/** Spins tolerated on a contended rwlock before declaring a deadlock. */
#define RWLOCK_MAX_SPINS 1000000u

/**
 * @brief Waits for a contended read/write lock.
 *
 * Contention cannot happen while the kernel is non-preemptible: whoever holds
 * one of these locks runs to completion before another task can look at it. The
 * only way to arrive here is a path re-entering a lock it already holds, which
 * is a deadlock however it is spelled - so it is reported instead of spinning
 * forever and looking like a hang.
 *
 * Sleeping is deliberately not attempted. It needs the live interrupt frame,
 * and the VFS call sites sit several frames below the syscall entry without
 * one; the previous code manufactured a frame from the PCB, which corrupted
 * the caller's saved context. Once the kernel becomes preemptible this is the
 * place to grow a real wait queue.
 *
 * @param spins Caller's spin counter.
 */
static void rwlock_wait(uint32_t *spins) {
    if (++(*spins) > RWLOCK_MAX_SPINS) {
        kernel_panic("rwlock: contended in a non-preemptible kernel (recursive acquire?)");
    }
    asm volatile("pause");
}

/**
 * @brief Acquire a read lock on a read-write lock.
 * 
 * @param lock Pointer to the read-write lock.
 * @param regs Pointer to the current CPU registers.
 */
void rwlock_acquire_read(rwlock_t *lock, arch_regs_t *regs) {
    uint32_t spins = 0;

    while (1) {
        mutex_lock(&lock->mutex, regs);
        /* Readers used to walk straight past writer_active, so a reader and a
         * writer could hold the lock at the same time and the "exclusion" was
         * decorative. */
        if (!lock->writer_active) {
            lock->readers++;
            mutex_unlock(&lock->mutex);
            return;
        }
        mutex_unlock(&lock->mutex);
        rwlock_wait(&spins);
    }
}

/**
 * @brief Release a read lock on a read-write lock.
 *
 * @param lock Pointer to the read-write lock.
 */
void rwlock_release_read(rwlock_t *lock) {
    mutex_lock(&lock->mutex, 0);
    if (lock->readers > 0) {
        lock->readers--;
    }
    mutex_unlock(&lock->mutex);
}

/**
 * @brief Acquire a write lock on a read-write lock.
 *
 * @param lock Pointer to the read-write lock.
 * @param regs Live interrupt frame, or 0 when the caller does not have one.
 */
void rwlock_acquire_write(rwlock_t *lock, arch_regs_t *regs) {
    uint32_t spins = 0;

    while (1) {
        mutex_lock(&lock->mutex, regs);
        if (!lock->writer_active && lock->readers == 0) {
            lock->writer_active = 1;
            mutex_unlock(&lock->mutex);
            return;
        }
        mutex_unlock(&lock->mutex);
        /*
         * The old code slept here and then returned - without the lock, and
         * without setting writer_active. The caller went on to mutate the
         * resource believing it was protected, and its matching release
         * unlocked a mutex it did not own.
         */
        rwlock_wait(&spins);
    }
}

/**
 * @brief Release a write lock on a read-write lock.
 *
 * @param lock Pointer to the read-write lock.
 */
void rwlock_release_write(rwlock_t *lock) {
    mutex_lock(&lock->mutex, 0);
    lock->writer_active = 0;
    mutex_unlock(&lock->mutex);
}
