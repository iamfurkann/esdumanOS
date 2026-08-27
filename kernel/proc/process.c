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
#include "libft.h"
#include "pmm.h"
#include "paging.h"
#include "process.h"
#include "security.h"
/* For WSTATUS_STOPPED: the value a stopped child is reported to wait() with,
 * which stop_task() writes into a blocked exec()'s saved frame. And for
 * SYSCALL_YIELD, which init_multitasking() assembles into the idle task's
 * instruction stream by hand. */
#include "syscall.h"

process_t *task_list_head = 0;
process_t *task_list_tail = 0;
cpu_state_t cpus[MAX_CPUS];
int multitasking_enabled = 0;
int next_pid = 0;
uint32_t foreground_pgid = 0;
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

    /*
     * Asm: mov eax, SYSCALL_YIELD (B8 imm32) | int 0x80 (CD 80) | jmp short -9 (EB F7)
     *
     * The immediate is built from the constant rather than written out as a
     * byte. It used to be a literal 0x63, with "mov eax, 99" in the comment
     * beside it, and that is the whole of what tied the idle task to the syscall
     * it makes: nothing the compiler could check, in a page of machine code, for
     * the one task the scheduler falls back on when nothing else can run. Moving
     * SYSCALL_YIELD would have left the byte behind and the machine would not
     * have booted - not failed a test, not printed anything, not booted.
     *
     * B8 takes a full 4-byte immediate, so every syscall number encodes the same
     * way and there is no range this loop has to stay inside.
     */
    uint8_t idle_code[] = {
        0xB8,
        (uint8_t)(SYSCALL_YIELD & 0xFF),
        (uint8_t)((SYSCALL_YIELD >> 8) & 0xFF),
        (uint8_t)((SYSCALL_YIELD >> 16) & 0xFF),
        (uint8_t)((SYSCALL_YIELD >> 24) & 0xFF),
        0xCD, 0x80,
        0xEB, 0xF7
    };
    uint8_t *idle_page = (uint8_t *)0x80000000;
    for(int i = 0; i < (int)sizeof(idle_code); i++) {
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
 * @brief Allocate a process id no other task is currently holding.
 *
 * next_pid only ever moved forward and was reset to 2 on overflow, with nothing
 * checking whether the value it landed on was still in use. At one pid per
 * exec() the wrap is far away, but a duplicate pid is not a cosmetic problem:
 * kill(), the foreground bookkeeping and the parent search in reap_task() all
 * identify a task by pid alone, and each stops at the first match it finds - so
 * a signal or an exit status would be delivered to whichever namesake happened
 * to sit earlier in the list.
 *
 * Zombies are scanned alongside live tasks. A zombie still carries the pid its
 * parent has yet to be told about, so reusing that number before the reaper has
 * run would let the parent collect a status from the wrong task.
 *
 * The scan is bounded: at most MAX_TASKS live tasks and the zombies not yet
 * reaped can be holding pids, so a free one is always found within that many
 * steps.
 *
 * @return A pid held by no live or unreaped task.
 */
static int allocate_pid(void) {
    for (;;) {
        if (next_pid < 0 || next_pid == 0x7FFFFFFF) {
            next_pid = 2;
        }

        int candidate = next_pid++;
        int taken = 0;

        for (process_t *p = task_list_head; p != 0 && !taken; p = p->next) {
            if (p->pid == candidate) taken = 1;
        }
        for (process_t *z = zombie_tasks_head; z != 0 && !taken; z = z->next) {
            if (z->pid == candidate) taken = 1;
        }

        if (!taken) return candidate;
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

    /*
     * A fresh PCB starts as all zeroes.
     *
     * The explicit initialisers below cover every field this function has a
     * meaningful value for, and they used to be the only initialisation there
     * was - so cmd_args, fpu_state, signal_saved_regs and the mailbox entered
     * each new process carrying whatever the kernel heap had last left there.
     * Nothing read those fields before writing them, which is why it never
     * showed, but fork() copies a PCB as a whole and would hand the child that
     * garbage rather than the parent's state.
     *
     * This also subsumes the two hand-written loops that used to clear the
     * kernel stack and the register frame further down.
     */
    ft_memset(new_task, 0, sizeof(process_t));

    new_task->pid = allocate_pid();

    /*
     * cwd_id is inherited alongside uid. The very first task has no creator to
     * inherit from and starts at root; every later task starts where its creator
     * was standing, which is what makes "cd somewhere && run something" behave the
     * way a shell user expects.
     */
    if (current_task == 0) {
        new_task->parent_pid = -1;
        new_task->uid = 0;
        new_task->gid = 0;
        new_task->cwd_id = 0;
        /* Nothing to inherit from: the first task founds its own group. */
        new_task->pgid = (uint32_t)new_task->pid;
    } else {
        new_task->parent_pid = current_task->pid;
        new_task->uid = current_task->uid;
        new_task->gid = current_task->gid;
        new_task->cwd_id = current_task->cwd_id;
        /*
         * Same rule as uid and cwd_id. A forked child and an exec'd stage both
         * land in the group their creator was standing in, which is what keeps a
         * pipeline together without the shell having to place every stage.
         *
         * Unless the creator has no group, in which case this task founds one.
         * The idle task is created first, takes pid 0 because that is where the
         * pid counter starts, and therefore founds group 0 - which is the value
         * meaning "no group" everywhere else. create_process() then makes it
         * current_task on the spot, so the shell was built while the idle task
         * was standing there and inherited group 0 from it, and so did every
         * program the shell ever started. send_signal_to_group() refuses group 0,
         * so Ctrl-C reached nobody at all: the whole system was in the group that
         * means there isn't one.
         */
        new_task->pgid = current_task->pgid ? current_task->pgid
                                            : (uint32_t)new_task->pid;
    }

    klog_int(LOG_LEVEL_DEBUG, "PROCESS", "New process created", new_task->pid);

    new_task->in_signal_handler = 0;
    new_task->state = TASK_RUNNING;
    new_task->wait_reason = WAIT_NONE;
    new_task->sleep_deadline = 0;
    new_task->sleep_active = 0;
    new_task->alarm_deadline = 0;
    new_task->alarm_active = 0;
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
    new_task->fd_table = (file_descriptor_t *)kmalloc(sizeof(file_descriptor_t) * MAX_FD_PER_TASK);
    if (!new_task->fd_table) {
        klog(LOG_LEVEL_ERROR, "PROCESS", "Could not create new process: no memory for the descriptor table.");
        kfree(new_task);
        return E_NOMEM;
    }

    new_task->fd_table_size = MAX_FD_PER_TASK;
    for (uint32_t fd_i = 0; fd_i < MAX_FD_PER_TASK; fd_i++) {
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

    new_task->page_directory = cr3;

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
 * @brief Gives a new task a copy of another's open descriptors.
 *
 * Both callers of this need the same thing and for the same reason: a task
 * created from another one keeps what the original had open. exec() has done it
 * since descriptors existed, and fork() cannot do otherwise - a child that lost
 * its parent's stdout would have nowhere to write.
 *
 * Sharing a descriptor means sharing the object behind it, so every copied entry
 * takes a reference: a pipe end bumps the count for its own direction, and a file
 * bumps ref_count. Without that the first of the two tasks to close or exit would
 * destroy the pipe, or commit and free the file, out from under the other.
 *
 * A file whose ref_count is already out of range is treated as a dangling pointer
 * and the descriptor is dropped rather than copied. That guard predates fork(); it
 * exists because a stale vfs_file_t reaching a second task turns one
 * use-after-free into two.
 *
 * Standard descriptors are deliberately not defaulted here. exec() opens them for
 * a program that arrives with none, which is right for a fresh image and wrong for
 * a fork: a child inherits the parent's descriptors exactly, closed ones included.
 *
 * @param child  Task receiving the copies. Its table must already be allocated.
 * @param parent Task to copy from.
 */
void inherit_fd_table(process_t *child, process_t *parent) {
    if (child == 0 || parent == 0) return;
    if (child->fd_table == 0 || parent->fd_table == 0) return;

    for (uint32_t k = 0; k < parent->fd_table_size; k++) {
        if (k >= child->fd_table_size) break;

        child->fd_table[k] = parent->fd_table[k];

        if (child->fd_table[k].type == 3 && child->fd_table[k].ptr != 0) {
            pipe_t *p = (pipe_t *)child->fd_table[k].ptr;
            if (child->fd_table[k].mode == 1) p->write_refs++;
            else p->read_refs++;
        }
        else if (child->fd_table[k].type == 2 && child->fd_table[k].ptr != 0) {
            vfs_file_t *f = (vfs_file_t *)child->fd_table[k].ptr;
            if (f->ref_count >= 0 && f->ref_count < 1000) {
                f->ref_count++;
            } else {
                child->fd_table[k].type = 0;
                child->fd_table[k].ptr = 0;
                klog_int(LOG_LEVEL_WARN, "PROCESS",
                         "Use-After-Free Protection: Invalid file pointer cleared FD", (int)k);
            }
        }
    }
}

/**
 * @brief Copies the parent state a forked child continues with.
 *
 * create_process() already inherits uid, cwd_id and pgid from the creating task,
 * so those are not repeated here. What is left is everything a process accumulates
 * while running, which exec() deliberately discards - a new program image starts
 * with default handlers and no arguments - and which fork() must carry over
 * because the child is the same program at the same instruction.
 *
 * The FPU state is copied rather than reset. The child resumes mid-expression as
 * far as it knows, and an fninit underneath it would change the value of a
 * computation already in flight.
 *
 * auth_fail_ticks is copied for a different reason: it is a rate limit, not
 * context. sys_auth() makes a task wait after a failed password attempt, and a
 * child starting with the counter clear would let a caller fork its way out of the
 * delay and retry at full speed.
 *
 * Deliberately not copied: the mailbox, which is addressed to a pid and would
 * deliver the same message twice; pending_signals and in_signal_handler, so the
 * child does not re-enter a handler its parent was already inside; and every
 * scheduler and lock field, since the child holds no lock and waits on nothing.
 *
 * @param child  Freshly created task.
 * @param parent Task being forked.
 */
void inherit_pcb_state(process_t *child, process_t *parent) {
    if (child == 0 || parent == 0) return;

    for (int i = 0; i < MAX_USER_SIGNALS; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }

    child->base_priority = parent->base_priority;
    child->current_priority = parent->current_priority;
    child->auth_fail_ticks = parent->auth_fail_ticks;

    /*
     * The break comes over as it stands. create_process() cannot derive it - it
     * never sees an ELF image - and copy_user_space() has already given the
     * child every heap page the parent had mapped. A child that started at
     * brk_start instead would hand the same addresses out twice, over memory it
     * is already using.
     */
    child->brk_start = parent->brk_start;
    child->brk_current = parent->brk_current;

    /*
     * The log cursor comes over too, so a child continues reading where its
     * parent had got to rather than being handed the whole ring again. A shell
     * that forks to run something is not asking for the boot log a second time.
     */
    child->kmsg_seq = parent->kmsg_seq;

    ft_memcpy(child->cmd_args, parent->cmd_args, sizeof(child->cmd_args));
    ft_memcpy(child->fpu_state, parent->fpu_state, sizeof(child->fpu_state));
    child->fpu_initialized = parent->fpu_initialized;

    /*
     * alarm_deadline and alarm_active are deliberately absent, and this note is
     * here because their absence is a decision rather than an omission.
     *
     * POSIX clears a pending alarm in the child, and the reason holds here: the
     * parent asked to be told something at a particular moment, and the child is
     * not the one that asked. A child that inherited it would be signalled - and
     * by default killed - at a time nothing in it had any part in choosing.
     * create_process() zeroes the whole PCB before this runs, so the child
     * starts with no alarm by construction.
     *
     * The sleep pair is absent for a different reason: a child is created
     * runnable, so there is no sleep in progress to carry.
     */
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
 * @brief Something a child has to report, held until its parent asks for it.
 *
 * Two kinds of news share these slots. An exit status is the historical one: the
 * child is gone and the number is all that is left of it. A stop notification is
 * the other, and it is not history at all - the child is alive, parked, and
 * waiting to be continued - so it is taken only by a caller that asked for
 * WUNTRACED, and continue_task() withdraws it if nobody has.
 */
typedef struct {
    int parent_pid;
    int child_pid;
    int exit_code;     /**< Exit status, or the signal number for a stop. */
    uint32_t seq;      /**< Arrival order, so "oldest" means something when full. */
    uint8_t used;
    uint8_t stopped;   /**< 1 when this is a stop notification, not an exit. */
} exit_slot_t;

/*
 * Parked exit statuses.
 *
 * Until fork() there was only one way to learn how a child had finished:
 * exec() blocked the caller, and reap_task() wrote the status straight into its
 * saved frame. That works because the parent is *always* already waiting - it
 * blocked before the child could run.
 *
 * fork() breaks that. A child can exit while its parent is doing something else
 * entirely, and the status has to survive until wait() is called - which may be
 * never. These slots hold it in the meantime.
 *
 * MAX_TASKS entries is the ceiling by construction: a status can only be parked
 * by a task that existed, and no more than MAX_TASKS exist at once.
 */
static exit_slot_t exit_slots[MAX_TASKS];
static uint32_t exit_slot_seq = 1;

/**
 * @brief Stores something a child has to report for a parent not waiting yet.
 *
 * @param parent_pid Parent to deliver to.
 * @param child_pid  Child the news is about.
 * @param exit_code  Exit status, or the signal number when stopped is set.
 * @param stopped    1 for a stop notification, 0 for an exit status.
 */
static void park_child_news(int parent_pid, int child_pid, int exit_code, int stopped) {
    int slot = -1;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (!exit_slots[i].used) { slot = i; break; }
    }

    if (slot == -1) {
        /*
         * Full. Every entry belongs to a parent that has not called wait(), so
         * something is already leaking statuses; drop the oldest rather than the
         * newest, and say so. Silently discarding the arriving one would lose the
         * status of the child that just exited, which is the one a caller is most
         * likely to be about to ask for.
         */
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (exit_slots[i].seq < oldest) { oldest = exit_slots[i].seq; slot = i; }
        }
        klog_int(LOG_LEVEL_WARN, "PROCESS",
                 "wait: status table full, discarded the status of PID", exit_slots[slot].child_pid);
    }

    exit_slots[slot].parent_pid = parent_pid;
    exit_slots[slot].child_pid = child_pid;
    exit_slots[slot].exit_code = exit_code;
    exit_slots[slot].seq = exit_slot_seq++;
    exit_slots[slot].used = 1;
    exit_slots[slot].stopped = (uint8_t)(stopped ? 1 : 0);
}

/**
 * @brief Withdraws any stop notification parked for a child, uncollected.
 *
 * A stop describes what a task is, not what it did, so a second stop must not
 * queue behind the first and a stop that has been continued must not still be
 * sitting there to be reported later. Exit statuses are left alone: those are
 * history and every one of them is owed to somebody.
 *
 * @param child_pid Child whose stop notification is no longer true.
 */
static void drop_parked_stop(int child_pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (exit_slots[i].used && exit_slots[i].stopped &&
            exit_slots[i].child_pid == child_pid) {
            exit_slots[i].used = 0;
        }
    }
}

/**
 * @brief Records that a child has stopped, for a parent to collect with wait().
 *
 * @param parent_pid Parent to deliver to.
 * @param child_pid  Child that stopped.
 * @param sig_num    Signal that stopped it.
 */
static void park_stop_notice(int parent_pid, int child_pid, int sig_num) {
    drop_parked_stop(child_pid);
    park_child_news(parent_pid, child_pid, sig_num, 1);
}

/**
 * @brief Stores a child's exit status for a parent that is not waiting yet.
 *
 * @param parent_pid Parent to deliver to.
 * @param child_pid  Child that exited.
 * @param exit_code  Status to hold.
 */
static void park_exit_status(int parent_pid, int child_pid, int exit_code) {
    /*
     * A child that exits has nothing left to be stopped about. Leaving the stop
     * behind would let a shell collect "it stopped" for a process that is gone
     * and go looking for it with fg.
     */
    drop_parked_stop(child_pid);
    park_child_news(parent_pid, child_pid, exit_code, 0);
}

/**
 * @brief Takes one parked status belonging to a parent, oldest first.
 *
 * @param parent_pid    Parent asking.
 * @param child_pid_out Receives the pid of the child it belonged to.
 * @param exit_code_out Receives the status when one is found.
 * @return 1 when a status was taken, 0 when the parent has none parked.
 */
int take_parked_status(int parent_pid, int *child_pid_out, int *exit_code_out) {
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFF;

    for (int i = 0; i < MAX_TASKS; i++) {
        /* Stop notifications are not exits and are invisible here. A caller that
         * wants them asks take_parked_stop(), which is what WUNTRACED reaches. */
        if (exit_slots[i].used && !exit_slots[i].stopped &&
            exit_slots[i].parent_pid == parent_pid) {
            if (exit_slots[i].seq < oldest) { oldest = exit_slots[i].seq; slot = i; }
        }
    }

    if (slot == -1) return 0;

    if (child_pid_out) *child_pid_out = exit_slots[slot].child_pid;
    if (exit_code_out) *exit_code_out = exit_slots[slot].exit_code;
    exit_slots[slot].used = 0;
    return 1;
}

/**
 * @brief Takes one stop notification parked for a parent, oldest first.
 *
 * @param parent_pid    Parent asking.
 * @param child_pid_out Receives the pid that stopped.
 * @param sig_out       Receives the signal that stopped it.
 * @return 1 when a notification was taken, 0 when none is parked.
 */
int take_parked_stop(int parent_pid, int *child_pid_out, int *sig_out) {
    int slot = -1;
    uint32_t oldest = 0xFFFFFFFF;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (exit_slots[i].used && exit_slots[i].stopped &&
            exit_slots[i].parent_pid == parent_pid) {
            if (exit_slots[i].seq < oldest) { oldest = exit_slots[i].seq; slot = i; }
        }
    }

    if (slot == -1) return 0;

    if (child_pid_out) *child_pid_out = exit_slots[slot].child_pid;
    if (sig_out) *sig_out = exit_slots[slot].exit_code;
    exit_slots[slot].used = 0;
    return 1;
}

/**
 * @brief Discards every status parked for a parent that is itself gone.
 *
 * Nobody will ever collect them, and the slots are a fixed resource shared by
 * every process.
 *
 * @param parent_pid Parent being reaped.
 */
static void drop_parked_statuses(int parent_pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (exit_slots[i].used && exit_slots[i].parent_pid == parent_pid) {
            exit_slots[i].used = 0;
        }
    }
}

/**
 * @brief Whether a task still has children that could report a status.
 *
 * @param pid Parent to check.
 * @return 1 when at least one live child or parked status exists, 0 otherwise.
 */
int has_pending_children(int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (exit_slots[i].used && exit_slots[i].parent_pid == pid) return 1;
    }
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        /* A stopped child is still a child. It cannot report anything until it
         * is continued, but wait() must not answer E_CHILD while it exists -
         * that would tell a shell it has no jobs left to bring back. */
        if (p->parent_pid == pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) return 1;
    }
    return 0;
}

/**
 * @brief Whether a process group still holds anybody able to use the terminal.
 *
 * Stopped members do not count, and that is the whole reason this is asked. A
 * group that has been stopped is a group nothing can be typed at: it will not
 * read the keyboard and it will not act on an interrupt, so the terminal has to
 * move on to somebody who will.
 *
 * @param pgid      Group to examine; 0 is not a group and holds nobody.
 * @param excluding Task to ignore - the one being reaped or stopped, whose state
 *                  may not have been written yet.
 * @return 1 when a running or blocked member other than excluding exists.
 */
static int group_has_a_live_member(uint32_t pgid, const process_t *excluding) {
    if (pgid == 0) return 0;

    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p == excluding) continue;
        if (p->pgid != pgid) continue;
        if (p->state == TASK_RUNNING || p->state == TASK_WAITING) return 1;
    }
    return 0;
}

/**
 * @brief Gives the terminal to whichever live task can be found for it.
 *
 * The fallback, used when the foreground group has emptied and nobody was woken
 * who could take it. Split out of reap_task() because stopping a group has the
 * same problem as reaping one and had been about to grow a second copy of it.
 *
 * A group of zero is skipped. Every task built through create_process() has a
 * real one, but a task assembled by hand - the synthetic task the kernel-mode
 * tests run against - can carry a zeroed field, and handing the terminal to that
 * group is handing it to nobody: Ctrl-C would then reach no one at all. The idle
 * task is skipped for the same reason, more obviously.
 *
 * @param excluding Task not to consider, or 0.
 */
static void hand_terminal_to_any_live_task(const process_t *excluding) {
    foreground_pgid = 0;

    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p == excluding || p == idle_task) continue;
        if (p->state != TASK_RUNNING && p->state != TASK_WAITING) continue;
        if (p->pgid == 0) continue;

        foreground_pgid = p->pgid;
        return;
    }
}

/**
 * @brief Release a task's resources and hand it to the zombie reaper.
 *
 * Everything the death of a task entails except leaving it: the resources go
 * back, a parent blocked in wait() is woken with the status, the task leaves the
 * run list and joins the zombie list. What it deliberately does not do is switch
 * away - that is the caller's business, and only exit_current_process() has it.
 *
 * The split exists because a task can now die without being the one running.
 * kill() reaps its target from inside the killer's syscall, and fork()/wait()
 * will need the same "settle this task's affairs" step without a context switch
 * attached to it. When all of this lived in exit_current_process() there was no
 * way to end a task other than by being it.
 *
 * @param victim Task to reap. May be current_task or any other live task.
 */
void reap_task(process_t *victim) {
    if (victim == 0 || victim->state == TASK_DEAD) return;

    /*
     * The idle task is not reapable.
     *
     * It is the scheduler's guarantee that there is always something to pick,
     * and schedule() reaches it through a named pointer rather than through the
     * task list - so reaping it would leave that pointer aimed at memory the
     * zombie reaper has already freed, and the next schedule() would read a dead
     * process_t before panicking about having nowhere to go.
     *
     * This never came up while dying meant calling exit(): the idle task is a
     * Ring 3 loop that only ever yields. A working kill() makes it reachable.
     */
    if (victim == idle_task) {
        klog(LOG_LEVEL_WARN, "PROCESS", "Refused to reap the idle task.");
        return;
    }

    /*
     * Released against the victim, not against whoever is running.
     *
     * mutex_unlock() identifies the owner as current_task, which was the same
     * thing while only a task's own exit could release its locks. Reached from
     * kill() it is not: the ownership test would compare the mutex against the
     * killer's pid, fail, and leave a lock held by a task that no longer exists
     * - permanently, since nothing else ever revisits it. Anything waiting on
     * that mutex would block for the rest of the boot.
     */
    if (victim->held_mutex != 0) {
        mutex_release_owned_by(victim->held_mutex, victim);
    }

    for (uint32_t i = 0; i < victim->fd_table_size; i++) {
        if (victim->fd_table && victim->fd_table[i].type != 0) {
            if (victim->fd_table[i].type == 3 && victim->fd_table[i].ptr != 0) {
                pipe_t *p = (pipe_t *)victim->fd_table[i].ptr;
                if (victim->fd_table[i].mode == 1) p->write_refs--;
                else p->read_refs--;

                if (p->read_refs <= 0 && p->write_refs <= 0) {
                    destroy_pipe(p);
                }
                /* A dying task dropping its end is the same event a close is;
                 * without this a peer blocked in pipe_read()/pipe_write() waited
                 * for the rest of the boot. See sys_close(). */
                wakeup_tasks(WAIT_IPC);
            }
            else if (victim->fd_table[i].type == 2 && victim->fd_table[i].ptr != 0) {
                /* Same commit-on-last-reference rule as sys_close(): a program
                 * that writes and then dies without closing would otherwise lose
                 * everything it wrote. Nothing can be reported from here, so a
                 * failure is logged by fs_commit_writes() itself. */
                vfs_file_t *f = (vfs_file_t *)victim->fd_table[i].ptr;
                f->ref_count--;
                if (f->ref_count <= 0) {
                    fs_commit_writes(f);
                    kfree((void *)victim->fd_table[i].ptr);
                }
            }
            victim->fd_table[i].type = 0;
            victim->fd_table[i].ptr = 0;
            victim->fd_table[i].mode = 0;
        }
    }

    if (victim->fd_table) {
        kfree(victim->fd_table);
        victim->fd_table = 0;
        victim->fd_table_size = 0;
    }

    /*
     * The address space is NOT reclaimed here.
     *
     * When the victim is the running task this code is executing on its kernel
     * stack with its directory in CR3, so freeing its page tables would unmap
     * the ground we are standing on. The task is handed to the zombie list below
     * and the reaper in schedule() releases both the process_t and the address
     * space once another task's directory is live - which for a victim that was
     * never the running task is already true, so that reaper pass frees it.
     */
    int parent_pid = victim->parent_pid;
    victim->state = TASK_DEAD;
    victim->wait_mutex = 0;
    victim->pending_signals = 0;

    process_t *woken_parent = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid != parent_pid || p->state != TASK_WAITING) continue;

        /*
         * A parent blocked in wait() gets its status parked and is simply woken.
         *
         * It cannot be handed the value the way exec() is: wait() reports the
         * status by writing into the caller's own memory, and this code runs
         * with somebody else's directory in CR3. The syscall is restarted
         * instead - see WAIT_PID - and collects from the table below with the
         * right address space live.
         */
        if (p->wait_reason == WAIT_PID) {
            park_exit_status(parent_pid, victim->pid, victim->exit_code);
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
            woken_parent = p;
            break;
        }

        if (p->wait_reason == WAIT_CHILD) {
            /*
             * Only for the child this parent is actually inside exec() waiting
             * for.
             *
             * It used to be any child at all, which was the same thing only
             * while exec() was the only way to have one: the caller blocked
             * before the child could run, so there was never a second. fork()
             * and background jobs broke that, and the parent was then woken by
             * whichever child died first - a "sleep 30 &" finishing while the
             * shell sat in exec() returned the job's status as the foreground
             * command's, so the shell printed a prompt with that command still
             * running and both then read the keyboard.
             *
             * A non-match falls through to the parking below, which is where an
             * unrelated child's status belongs.
             */
            if (p->exec_child_pid != victim->pid) continue;

            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
            p->exec_child_pid = 0;

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
            p->regs.eax = (uint32_t)victim->exit_code;

            woken_parent = p;
            break;
        }
    }


    /*
     * Nobody was waiting, so hold the status until somebody asks.
     *
     * Before fork() this case could not arise: exec() was the only way to have a
     * child, and it blocked the caller before the child could run, so a parent was
     * always already sitting in WAIT_CHILD by the time this ran. A forked child
     * exits whenever it likes, and a status dropped here is one wait() can never
     * return.
     *
     * Statuses parked for a parent that never collects them are released when that
     * parent is itself reaped, below.
     */
    if (woken_parent == 0 && parent_pid > 0) {
        park_exit_status(parent_pid, victim->pid, victim->exit_code);
    }

    drop_parked_statuses(victim->pid);

    /*
     * The terminal only changes hands when the task holding it dies, or when a
     * shell blocked on this task wakes up to take it back.
     *
     * This used to reassign foreground_task on every exit, which was
     * indistinguishable from the rule below while the only way to die was to be
     * the running - and therefore foreground - task. kill() breaks that: reaping
     * a task the user is not looking at must not take the terminal away from the
     * shell they are typing into.
     */
    /*
     * A group keeps the terminal until its last member goes. This used to hand
     * it on when the foreground *task* died, which was the same thing only while
     * a group could not have two members: the first stage of "ls | grep etc"
     * exiting would have taken the terminal away from the stage still running.
     *
     * And the woken parent is inside that test rather than ahead of it, which is
     * a correction. Waking a parent used to move the terminal to it whatever the
     * dying task was, so a background job finishing while a foreground job ran
     * took the terminal from a job the user was looking at - the shell's own
     * wait() is what the job it started wakes, and it is also what every other
     * child of it wakes. A task that did not hold the terminal cannot give it
     * away.
     */
    if (victim->pgid == foreground_pgid &&
        !group_has_a_live_member(foreground_pgid, victim)) {
        if (woken_parent != 0) foreground_pgid = woken_parent->pgid;
        else hand_terminal_to_any_live_task(victim);
    }

    // Remove from linked list
    if (victim->prev) victim->prev->next = victim->next;
    else task_list_head = victim->next;

    if (victim->next) victim->next->prev = victim->prev;
    else task_list_tail = victim->prev;

    victim->prev = 0;
    victim->next = zombie_tasks_head;
    zombie_tasks_head = victim;
}

/**
 * @brief Take a task out of the scheduler's reach without ending it.
 *
 * @param victim  Task to stop; may or may not be the running task.
 * @param sig_num Signal that stopped it, reported to the parent through wait().
 */
void stop_task(process_t *victim, int sig_num) {
    if (victim == 0) return;
    if (victim->state == TASK_EMPTY || victim->state == TASK_DEAD) return;
    if (victim->state == TASK_STOPPED) return;

    /*
     * The idle task is not stoppable, for the reason it is not reapable: the
     * scheduler reaches it through a named pointer as its guarantee that there
     * is always something to pick, and a stopped idle task would leave
     * schedule() panicking with nowhere to go the first time everything else
     * blocked.
     */
    if (victim == idle_task) {
        klog(LOG_LEVEL_WARN, "PROCESS", "Refused to stop the idle task.");
        return;
    }

    /*
     * Remember what it was doing, then take it out of that state. A task in
     * TASK_STOPPED is not waiting for anything as far as the rest of the kernel
     * is concerned - wakeup_tasks() must not find it and release it behind the
     * user's back - so the reason moves aside rather than staying live.
     */
    victim->stopped_wait_reason =
        (victim->state == TASK_WAITING) ? victim->wait_reason : WAIT_NONE;
    victim->state = TASK_STOPPED;
    victim->wait_reason = WAIT_NONE;

    /* The signal has done what it does; leaving the bit set would stop the task
     * a second time the moment it was continued. */
    if (sig_num >= 0 && sig_num < MAX_USER_SIGNALS) {
        victim->pending_signals &= ~(1u << sig_num);
    }

    /* Kept, not printed. Stopping is routine and the user did it on purpose; a
     * line of kernel log over the prompt on every Ctrl-Z says nothing they do not
     * already know, and over a full-screen program it lands in the middle of the
     * display. This was demoted to DEBUG for that in v0.8.1, which was the wrong
     * tool: one threshold gates the ring as well as the console, so the record
     * was not hidden but discarded - gone from dmesg too. */
    klog_record_int(LOG_LEVEL_INFO, "SIGNAL", "Stopped by signal: PID", victim->pid);

    /*
     * Tell the parent, and tell it the same way reap_task() does.
     *
     * This is not politeness. A shell waiting for the job it just started is
     * blocked in wait() or inside exec(), and a stopped child will never finish -
     * so without this the shell waits for the rest of the boot for something that
     * is only ever going to be resumed by the shell itself.
     */
    process_t *woken_parent = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid != victim->parent_pid || p->state != TASK_WAITING) continue;

        if (p->wait_reason == WAIT_PID) {
            /* Parked and woken, not handed over: wait() writes the status into
             * the caller's own memory and this code runs with somebody else's
             * directory in CR3. The syscall restarts and collects it. */
            park_stop_notice(p->pid, victim->pid, sig_num);
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
            woken_parent = p;
            break;
        }

        if (p->wait_reason == WAIT_CHILD && p->exec_child_pid == victim->pid) {
            /*
             * A blocking exec() cannot restart - it would launch the program a
             * second time - so it is handed a value in its saved frame, the same
             * way an exit status is. The notification is parked as well, because
             * the value says only that something stopped: exec() never told the
             * caller which pid it started, so wait() is the only way to find out
             * who to bring back.
             */
            park_stop_notice(p->pid, victim->pid, sig_num);
            p->regs.eax = (uint32_t)(WSTATUS_STOPPED | sig_num);
            p->state = TASK_RUNNING;
            p->wait_reason = WAIT_NONE;
            p->exec_child_pid = 0;
            woken_parent = p;
            break;
        }
    }

    if (woken_parent == 0 && victim->parent_pid > 0) {
        park_stop_notice(victim->parent_pid, victim->pid, sig_num);
    }

    /*
     * And the terminal, by the same rule death follows: it moves only once the
     * group holding it has nobody left who could use it, and a stopped member is
     * not one of those - it will not read a key and will not act on an
     * interrupt. Then the woken parent takes it back, or the fallback finds
     * somebody who can hold it.
     */
    if (victim->pgid == foreground_pgid &&
        !group_has_a_live_member(foreground_pgid, victim)) {
        if (woken_parent != 0) foreground_pgid = woken_parent->pgid;
        else hand_terminal_to_any_live_task(victim);
    }
}

/**
 * @brief Put a stopped task back where it was.
 *
 * @param task Task to continue; anything not stopped is left alone.
 */
void continue_task(process_t *task) {
    if (task == 0 || task->state != TASK_STOPPED) return;

    /* A stop that has not happened yet is cancelled by being continued, which is
     * the POSIX rule and also the only sane reading of the two arriving
     * together. Both of them: a job stopped for reading the terminal may have a
     * Ctrl-Z sitting behind it, and vice versa. */
    task->pending_signals &= ~((1u << SIG_TSTP) | (1u << SIG_TTIN));

    /*
     * A process that asked to hear about this is told, once it is running again.
     *
     * The signal is acted on where it is sent - a stopped task never reaches a
     * delivery point of its own - so nothing was ever recorded for it and a
     * program had no way to learn it had been continued. That is fine for a
     * program that only computes, and useless for one that draws: everything it
     * had on screen was written over by the shell while it was stopped, and the
     * moment it is running again is the only moment it could redraw.
     *
     * Only when a handler is registered. A bit left pending for a task with no
     * handler would be cleared by check_and_deliver_signals() and nothing else,
     * which is a pending bit that means nothing.
     */
    if (task->signal_handlers[SIG_CONT] != 0 &&
        task->signal_handlers[SIG_CONT] != SIG_IGN) {
        task->pending_signals |= (1u << SIG_CONT);
    }

    /* The notification described the task as it was. It is not true any more,
     * and a parent that had not got round to collecting it would otherwise be
     * told about a stop that has already been undone. */
    drop_parked_stop(task->pid);

    if (task->stopped_wait_reason == WAIT_CHILD) {
        /* Back into the wait. See stopped_wait_reason: this is the one block
         * that returns through eax rather than re-running its syscall, so
         * releasing it as runnable would return from exec() with a register
         * nobody wrote. */
        task->state = TASK_WAITING;
        task->wait_reason = WAIT_CHILD;
    } else {
        /*
         * Released as runnable even if it was blocked. Every other blocking
         * syscall resumes on the trap instruction and re-evaluates what it was
         * waiting for, so this both restores the wait and repairs the wakeup
         * that arrived while the task was stopped - wakeup_tasks() only touches
         * TASK_WAITING, so those were missed.
         */
        task->state = TASK_RUNNING;
        task->wait_reason = WAIT_NONE;
    }

    task->stopped_wait_reason = WAIT_NONE;
    klog_record_int(LOG_LEVEL_INFO, "SIGNAL", "Continued by signal: PID", task->pid);
}

/**
 * @brief Whether any task is still able to run.
 *
 * Asked separately from the foreground bookkeeping above. Folding the two
 * together - treating "no foreground task" as "nothing left to run" - would
 * announce a shutdown whenever a task died while foreground_task happened to be
 * unset, which is its value for the whole of early boot.
 *
 * A stopped task counts. It cannot run, but it exists, holds its memory and can
 * be continued - announcing that the machine has shut down while one is sitting
 * there would be announcing the loss of it.
 *
 * @return 1 while at least one task is runnable, blocked or stopped, 0 otherwise.
 */
static int any_task_alive(void) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state == TASK_RUNNING || p->state == TASK_WAITING ||
            p->state == TASK_STOPPED) return 1;
    }
    return 0;
}

/**
 * @brief Exit the current process and clean up its resources.
 *
 * @param regs Pointer to the current CPU registers.
 */
void exit_current_process(arch_regs_t *regs) {
    if (current_task == 0) return;

    reap_task(current_task);

    if (!any_task_alive()) {
        klog(LOG_LEVEL_INFO, "KERNEL", "Last running process terminated. Halting system.");

        printk("\n\n=======================================================\n");
        printk("      esdumanOS SAFELY SHUT DOWN        \n");
        printk("      You may now safely turn off your computer.        \n");
        printk("=======================================================\n\n");

        asm volatile("cli; hlt");
    }

    // Switch to next task before freeing curr memory!
    // But schedule uses current_task. We must set current_task to 0 so schedule picks another.
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
 * @brief Send SIG_ALRM to every task whose alarm() deadline has passed.
 *
 * The deadline is absolute and the comparison signed, for the reason spelled out
 * above wake_expired_sleepers(): the tick counter wraps.
 *
 * Collected first and signalled second, which is not tidiness. SIG_ALRM
 * terminates by default, and send_user_signal() reaps a target that is not the
 * running task on the spot - unlinking it from the very list a single pass would
 * be walking, so the step after the first casualty would follow a next pointer
 * out of a freed node. send_signal_to_group() collects into an array for exactly
 * this reason and this is the same hazard; MAX_TASKS is the ceiling by
 * construction, since no more tasks can hold an alarm than exist.
 *
 * The flag is cleared before the signal goes out rather than after. An alarm is
 * one-shot - a caller that wants another says so with another alarm() - and
 * clearing afterwards would mean a handler that calls alarm() from inside the
 * delivery had its new deadline wiped by the sweep that delivered the old one.
 *
 * The idle task is skipped. It cannot call alarm(), so it can never hold one,
 * and the check costs a comparison against the case where something else has
 * gone wrong enough to give it one.
 */
void expire_alarms(void) {
    uint32_t now = timer_get_ticks();
    int targets[MAX_TASKS];
    int count = 0;

    for (process_t *p = task_list_head; p != 0 && count < MAX_TASKS; p = p->next) {
        if (p == idle_task) continue;
        if (p->state == TASK_EMPTY || p->state == TASK_DEAD) continue;
        if (!p->alarm_active) continue;

        if ((int32_t)(now - p->alarm_deadline) >= 0) {
            p->alarm_active = 0;
            p->alarm_deadline = 0;
            targets[count++] = p->pid;
        }
    }

    for (int i = 0; i < count; i++) {
        send_user_signal(targets[i], SIG_ALRM);
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
     * All three now run on every entry, on the caller's own stack and directory,
     * and before the selection passes - so a task any of them makes runnable can
     * be chosen in this same pass instead of waiting for the next one. That
     * ordering is load-bearing for wake_expired_sleepers(): a sweep after the
     * selection would leave a due sleeper waiting one more scheduling round.
     *
     * expire_alarms() joins them for the same reason and answers to the same
     * constraint: it walks the task list, so it cannot run from the interrupt
     * either. It is last of the three because an alarm that comes due for a
     * sleeping task should find that task already woken by the sweep above -
     * the signal is delivered when the task next runs, and a task the scheduler
     * is about to pick runs sooner than one it has passed over.
     *
     * Running any of them from IRQ0 instead would mean walking the task list
     * from an interrupt, and that list is edited without masking interrupts -
     * see the note above. This is the same reasoning that made kernel timers a
     * bottom half in the first place.
     *
     * Progress does not depend on another task being awake: the idle task is a
     * Ring 3 loop of "mov eax, SYSCALL_YIELD; int 0x80" (init_multitasking()),
     * so schedule() keeps being entered even when every other task is sleeping.
     */
    process_pending_kernel_timers();
    wake_expired_sleepers();
    expire_alarms();

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
void mutex_release_owned_by(mutex_t *m, process_t *owner) {
    if (m == 0 || owner == 0) return;
    if (m->locked != 1 || m->owner_pid != owner->pid) return;

    m->locked = 0;
    m->owner_pid = -1;
    owner->held_mutex = 0;

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

void mutex_unlock(mutex_t *m) {
    if (!multitasking_enabled || current_task == 0) return;

    uint32_t eflags;
    asm volatile ("pushf; pop %0" : "=r"(eflags));
    if (!(eflags & 0x200)) return;

    asm volatile("cli");

    mutex_release_owned_by(m, current_task);

    asm volatile("sti");
}

/**
 * @brief Register a signal handler for the current user process.
 * 
 * @param sig_num The signal number.
 * @param handler_addr Address of the signal handler function.
 */
int register_user_signal(int sig_num, uint32_t handler_addr) {
    if (current_task == 0 || sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return E_INVAL;

    /*
     * SIG_IGN is exempt from the address check. It is a disposition rather than
     * an address, and 1 is not a mappable user page - without the exemption the
     * only way to decline a signal would be rejected as a bad handler.
     *
     * This is the only copy of the test. sys_signal_reg() had a second one and
     * kept refusing SIG_IGN after this one was relaxed, which is how the shell
     * came to be unable to decline SIGPIPE while the kernel-mode tests of the
     * same function passed.
     */
    if (handler_addr != 0 && handler_addr != SIG_IGN
        && !validate_user_pointer((const void *)handler_addr, 1)) {
        klog_int(LOG_LEVEL_WARN, "SIGNAL", "Invalid signal handler address rejected! PID", current_task->pid);
        return E_FAULT;
    }

    current_task->signal_handlers[sig_num] = handler_addr;
    return E_OK;
}

/**
 * @brief Whether a signal terminates a process that has not handled it.
 *
 * Only the signals that mean "stop running" have a default action. Every other
 * signal keeps the behaviour it has always had: the pending bit is set, and if
 * nothing is registered to receive it, nothing happens.
 *
 * SIG_PIPE joins the list for the same reason the other two are on it - the
 * whole point is to stop a process that would otherwise keep going. A pipeline
 * stage whose reader has gone has nothing left to do, and until this it kept
 * reading its input to the end while every write failed.
 *
 * SIG_INT joined them in v0.8.0 and this line did not, which is the kind of drift
 * that makes a comment worth less than no comment.
 *
 * SIG_ALRM joins in v1.0.0, when alarm() became a call that actually signals the
 * process that made it. Terminating is POSIX's default and it is the one that
 * makes the call mean anything: alarm() exists to put a bound on something, and
 * a bound whose default is to be ignored is not a bound. A program that means to
 * survive its own alarm registers a handler and says so.
 *
 * @param sig_num The signal number.
 * @return 1 for SIG_KILL, SIG_TERM, SIG_PIPE, SIG_INT and SIG_ALRM, 0 otherwise.
 */
static int signal_terminates_by_default(int sig_num) {
    return sig_num == SIG_KILL || sig_num == SIG_TERM || sig_num == SIG_PIPE ||
           sig_num == SIG_INT || sig_num == SIG_ALRM;
}

/**
 * @brief Whether a signal stops a process that has not handled it.
 *
 * The second kind of default action, and the reason it has to be asked
 * separately from the first: the four signals above end a process, and these
 * park it with everything intact. A single "has a default action" test would
 * have to decide which of those to do from the signal number anyway.
 *
 * SIG_TTIN joins SIG_TSTP because the answer to a background job reading the
 * terminal is the same answer: park it where the user can find it and give it
 * back with fg. Killing it would lose whatever it had done, and letting it read
 * is the behaviour the signal exists to prevent.
 *
 * @param sig_num The signal number.
 * @return 1 for SIG_TSTP and SIG_TTIN, 0 otherwise.
 */
static int signal_stops_by_default(int sig_num) {
    return sig_num == SIG_TSTP || sig_num == SIG_TTIN;
}

/**
 * @brief Send a signal to a user process.
 *
 * A signal with no handler registered used to be recorded and then discarded by
 * check_and_deliver_signals(), which meant kill() could not actually kill
 * anything: a process that had never called signal() ignored every signal sent
 * to it, and a runaway task could not be stopped by any means the system
 * offered. SIG_KILL and SIG_TERM now fall back to terminating the target.
 *
 * A target that is not the running task is reaped here and now. The victim is
 * not executing - the kernel is not preemptible, so any task other than this one
 * is parked at a syscall or interrupt boundary with its frame saved in p->regs
 * and nothing live on its kernel stack - so there is no context to unwind and
 * reap_task() can settle its affairs directly.
 *
 * A target that IS the running task cannot be reaped from here: the caller is
 * mid-syscall on that task's stack. The pending bit is left set instead, and
 * apply_default_signal_action() terminates it on the way back out to user mode.
 *
 * A signal whose default action is to stop rather than to end follows exactly
 * that shape - in place for another task, deferred for this one - and SIG_CONT
 * is the one signal acted on here in every case, because its target is by
 * definition not running.
 *
 * @param target_pid The target process ID.
 * @param sig_num The signal number.
 */
void send_user_signal(int target_pid, int sig_num) {
    if (sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == target_pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) {
            /*
             * SIG_CONT is acted on here and nowhere else, and it is the one
             * signal that has to be.
             *
             * Everything else is recorded and then delivered to the target when
             * the target next runs. A stopped process does not next run - that
             * is what being stopped is - so a continue that waited for delivery
             * would wait forever. It is not recorded either: there is nothing
             * for the process to be told, and a pending bit nobody clears would
             * sit in the mask for the rest of its life.
             */
            if (sig_num == SIG_CONT) {
                continue_task(p);
                return;
            }

            p->pending_signals |= (1 << sig_num);

            /*
             * Stopped like the killed case below: in place when the target is
             * not the running task, and left pending when it is. The reasoning
             * is identical - this code may be running inside the target's own
             * syscall, on its kernel stack, and taking it out of the scheduler
             * from there is not something that can be done halfway through.
             */
            if (p->signal_handlers[sig_num] == 0 && signal_stops_by_default(sig_num)
                && p != idle_task && p != current_task) {
                stop_task(p, sig_num);
                return;
            }

            if (p->signal_handlers[sig_num] == 0 && signal_terminates_by_default(sig_num)
                && p != idle_task) {
                if (p != current_task) {
                    /* 128 + signal number, the same encoding a shell reports a
                     * signalled child with. exit_code is masked to 8 bits on the
                     * exit() path, and 128 + 15 still fits. */
                    p->exit_code = 128 + sig_num;
                    /* Kept at INFO and not printed. A process ending because the
                     * user pressed Ctrl-C is the user's own doing and the "^C" is
                     * already on screen; the log line adds nothing and lands in
                     * the middle of a full-screen program's display. */
                    klog_record_int(LOG_LEVEL_INFO, "SIGNAL", "Terminated by signal: PID", p->pid);
                    reap_task(p);
                    return;
                }
                /* Self-signalled: handled on the syscall return path. */
            }

            /*
             * Wake a blocked *read* so it can report the interruption, and leave
             * every other wait alone.
             *
             * This used to wake any waiting task, which is wrong here for a
             * reason that only appears once signals reach a shell: a blocked
             * syscall resumes by re-running from the trap instruction, so waking
             * a shell that is parked inside exec() would make it run the program
             * again. Ctrl-C would have killed the job and immediately started a
             * second copy of it.
             *
             * A parent waiting on a child does not need waking in any case - the
             * child's death wakes it, with the status - so the only wait that
             * gains anything from being cut short is one for input.
             */
            if (p->state == TASK_WAITING && p->wait_reason == WAIT_KBD) {
                p->state = TASK_RUNNING;
                p->wait_reason = WAIT_NONE;
                p->signal_interrupted = 1;
            }
            return;
        }
    }
}

/**
 * @brief Sends a signal to every task in a process group.
 *
 * The pids are collected before any of them is signalled, and that is not
 * tidiness. send_user_signal() reaps its target outright when the signal
 * terminates by default and the target is not the running task, and reaping
 * unlinks that task from the very list this would otherwise be walking - so the
 * step after the first casualty would follow a pointer out of a node that no
 * longer exists.
 *
 * MAX_TASKS is the ceiling by construction: a group cannot hold more members
 * than there are tasks.
 *
 * @param pgid Group to signal; 0 signals nothing.
 * @param sig_num Signal number.
 */
void send_signal_to_group(uint32_t pgid, int sig_num) {
    int targets[MAX_TASKS];
    int count = 0;

    if (pgid == 0) return;
    if (sig_num < 0 || sig_num >= MAX_USER_SIGNALS) return;

    for (process_t *p = task_list_head; p != 0 && count < MAX_TASKS; p = p->next) {
        if (p == idle_task) continue;
        if (p->pgid != pgid) continue;
        if (p->state == TASK_EMPTY || p->state == TASK_DEAD) continue;
        targets[count++] = p->pid;
    }

    for (int i = 0; i < count; i++) {
        send_user_signal(targets[i], sig_num);
    }
}

/**
 * @brief Check for and deliver pending signals to the current process.
 *
 * Handler delivery only. A pending signal that terminates by default and has no
 * handler is left pending for apply_default_signal_action() to act on, because
 * this function is also called from the tail of schedule() - against the task
 * that has just been picked to run - and terminating a task from there would
 * re-enter schedule() through exit_current_process().
 *
 * SIG_IGN is checked explicitly rather than falling through to the delivery
 * branch below. It lives in signal_handlers[] alongside real addresses, so
 * "handler is non-zero" is no longer the same question as "there is somewhere to
 * jump" - without the check the sentinel would be written straight into
 * regs->eip and the process would resume at address 1.
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

                uint32_t handler = curr->signal_handlers[i];

                /*
                 * A default action is not delivery, and it is not this
                 * function's to perform - it is left pending for
                 * apply_default_signal_action(), which runs somewhere it is safe
                 * to end or park the task. Both kinds have to be named here:
                 * clearing the bit for a stop would discard a Ctrl-Z on the way
                 * out of the very syscall that was about to act on it.
                 */
                if (handler == 0 &&
                    (signal_terminates_by_default(i) || signal_stops_by_default(i))) {
                    continue;
                }

                curr->pending_signals &= ~(1 << i);

                /* Discarded, not delivered: the bit is cleared above and the
                 * process resumes where it was, none the wiser. */
                if (handler == SIG_IGN) {
                    continue;
                }

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
 * @brief Terminate the running task if it holds an unhandled fatal signal.
 *
 * The self-signalled half of the default action. send_user_signal() reaps any
 * other task on the spot, but it cannot reap the task whose syscall it is
 * running inside; that one is left with the pending bit set and terminated here,
 * on the way back out to user mode, which is where exit_current_process() is
 * safe to call.
 *
 * Called from exactly one place - the end of syscall_handler(), after the
 * syscall bookkeeping is closed out. It must not be called from schedule(): this
 * function calls exit_current_process(), which calls schedule().
 *
 * @param regs Live interrupt frame of the returning task.
 */
void apply_default_signal_action(arch_regs_t *regs) {
    if (current_task == 0 || regs == 0) return;

    process_t *curr = current_task;
    if (curr->pending_signals == 0) return;

    /*
     * The idle task cannot be terminated - see reap_task(). send_user_signal()
     * records the signal before it decides what to do with it, so the bit can be
     * sitting here even though the send declined to act on it; drop it rather
     * than walking into an exit that reap_task() will refuse anyway.
     *
     * Every bit, not a list of them. This cleared six named signals until
     * v1.0.0, which was correct only while those were all the signals with a
     * default action - adding SIG_ALRM to signal_terminates_by_default() would
     * have left its bit set here forever, on the one task that can never act on
     * it, with nothing to fail and nothing to notice. The condition being
     * answered is "this task cannot act on any signal", and that is what the
     * code should say.
     */
    if (curr == idle_task) {
        curr->pending_signals = 0;
        return;
    }

    for (int i = 0; i < MAX_USER_SIGNALS; i++) {
        if ((curr->pending_signals & (1 << i)) == 0) continue;
        if (curr->signal_handlers[i] != 0) continue;

        if (signal_terminates_by_default(i)) {
            curr->pending_signals &= ~(1 << i);
            curr->exit_code = 128 + i;

            klog_record_int(LOG_LEVEL_INFO, "SIGNAL", "Terminated by its own signal: PID", curr->pid);
            exit_current_process(regs);
            return;
        }

        /*
         * The other default action, and the same reason it happens here: this is
         * the running task, and send_user_signal() could not take it out of the
         * scheduler from inside its own syscall.
         *
         * The frame is snapshotted before the switch, exactly as
         * sleep_current_task() does - schedule() overwrites *regs with the
         * incoming task's context, so anything not saved by this point is gone.
         * The task resumes on the instruction after the trap when it is
         * continued, with the syscall it was making already finished.
         */
        if (signal_stops_by_default(i)) {
            curr->regs = *regs;
            stop_task(curr, i);
            schedule(regs);
            return;
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
 * process_t on the kernel heap (~9 KB, including its 8 KB kernel stack and
 * 512-byte FPU save area) plus a page directory, its page tables and 32 pages of
 * user stack. Without a cap an unprivileged loop of exec() exhausts the kernel
 * heap, and a kmalloc() failure there is far harder to survive than a rejected
 * exec.
 *
 * Only live tasks count: reap_task() unlinks a task before handing it to the
 * zombie reaper, so slots come back as processes terminate.
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

    if (foreground_pgid > 0) {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pgid == foreground_pgid && p->state == TASK_RUNNING) {
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
