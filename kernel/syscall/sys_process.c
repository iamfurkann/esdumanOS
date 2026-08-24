/*
 * File: sys_process.c
 * Purpose: Contains system calls and related utilities.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscalls_internal.h"
#include "types.h"
#include "registers.h"
#include "stdio.h"
#include "fs.h"
#include "process.h"
#include "errno.h"
#include "klog.h"
#include "elf.h"
#include "isr.h"
#include "kheap.h"
#include "pmm.h"
#include "stack.h"
#include "uaccess.h"
#include "bcache.h"
#include "paging.h"
#include "rtc.h"
/* For the exec() modes and the wait() flags and status bits. */
#include "syscall.h"

static int copy_user_string(char *destination, const char *source, size_t max_len) {
    if (!validate_string_pointer(source, max_len)) return 0;
    return copy_string_from_user(destination, source, max_len) == E_OK;
}
/**
 * @brief Function sys_exit
 */
void sys_exit(arch_regs_t *regs) {
    /*
     * The status argument used to be read by nobody. Programs have always
     * passed one - /bin/stat exits 1 when it cannot stat its argument - and it
     * went straight into the floor, so a parent could see that its child had
     * finished but never how. exit_current_process() picks this up when it
     * wakes whoever was waiting.
     *
     * Masked to 8 bits: that is the width POSIX exposes, and it keeps an exit
     * status from ever being mistaken for the negative errno sys_exec() returns
     * when the program could not be started at all.
     */
    if (current_task != 0) {
        current_task->exit_code = (int)(regs->ebx & 0xFF);
    }
    exit_current_process(regs);
}

/**
 * @brief Duplicates the calling process.
 *
 * Every process in this system has so far come from an ELF image: exec() builds
 * an address space and fills it from a file, and the caller blocks until the
 * result exits. That is enough to run a program and not enough to run two at
 * once, which is why the shell executes `cmd1 | cmd2` one stage at a time and
 * deadlocks when the first stage outgrows the pipe buffer.
 *
 * fork() is the other way to make a process: not from a file, but from a process.
 * The child gets a private copy of the parent's memory, its open descriptors, its
 * signal handlers and its registers, and returns from this call as if it had made
 * it itself.
 *
 * The order matters. Everything that can fail happens before the child becomes
 * visible to the scheduler - the directory, the page copies, the PCB - because a
 * task that is already on the run list cannot be un-created, only reaped, and
 * reaping a task that has never run is a path with no other caller.
 *
 * @param regs Live syscall frame of the parent. The child's saved frame is taken
 *             from it, so both resume at the instruction after the trap.
 */
void sys_fork(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = E_SRCH;
        return;
    }

    /* Checked here rather than after the copying, so a fork that cannot succeed
     * costs a comparison instead of an address space. */
    if (!check_free_task_slot()) {
        klog(LOG_LEVEL_WARN, "PROCESS", "fork: refused, task limit reached.");
        regs->eax = E_AGAIN;
        return;
    }

    uint32_t child_pd = clone_page_directory();
    if (child_pd == 0) {
        regs->eax = E_NOMEM;
        return;
    }

    int copy_result = copy_user_space(child_pd);
    if (copy_result != E_OK) {
        cleanup_process_memory(child_pd);
        /* Reported as it came back: E_NOMEM for exhausted frames, E_FAULT for a
         * page the tables claimed was there and could not be read. The two say
         * different things about what went wrong. */
        regs->eax = (uint32_t)copy_result;
        return;
    }

    /* uid, cwd_id and parent_pid come from current_task inside create_process(),
     * which is exactly the inheritance fork() wants - see its comment on cwd_id,
     * written when this call was still hypothetical. */
    int child_pid = create_process(regs->eip, regs->useresp, child_pd);
    if (child_pid < 0) {
        cleanup_process_memory(child_pd);
        regs->eax = child_pid;
        return;
    }

    process_t *child = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == child_pid) { child = p; break; }
    }

    if (child == 0) {
        /* create_process() reported success and the task is not in the list it
         * appends to. Nothing can be recovered from that safely. */
        klog(LOG_LEVEL_ERROR, "PROCESS", "fork: the new task is missing from the run list.");
        cleanup_process_memory(child_pd);
        regs->eax = E_FAULT;
        return;
    }

    inherit_pcb_state(child, current_task);
    inherit_fd_table(child, current_task);

    /*
     * The child resumes where the parent is standing.
     *
     * Copied from the live frame rather than rebuilt, so cs, ss, eflags and every
     * register match the parent exactly - the child is the same program at the
     * same instruction, and anything create_process() filled in for a fresh ELF
     * image would be wrong here. regs->eip already points past the int 0x80, so
     * neither side re-enters this call.
     *
     * Written into the child's *saved* frame, which is what schedule() restores
     * the first time it picks the task up. The one existing precedent for writing
     * a return value anywhere but the live frame is reap_task(), delivering an
     * exit status to a blocked parent; the rule is the same, and so is the reason
     * it has to happen before any context switch.
     */
    child->regs = *regs;
    child->regs.eax = 0;

    klog_int(LOG_LEVEL_DEBUG, "PROCESS", "fork: child created", child_pid);

    regs->eax = child_pid;
}

/**
 * @brief Waits for a child to finish and returns its exit status.
 *
 * The counterpart to fork(). exec() has always delivered a status by blocking the
 * caller first and letting reap_task() write into its saved frame, which works
 * only because the parent cannot possibly be doing anything else. A forked child
 * runs independently, so the two orders both have to work: the parent arriving
 * first and waiting, and the child finishing first and leaving the status behind.
 *
 * Parked statuses are checked before blocking, which is what makes the second
 * order work. Checking after would block a parent whose child had already exited,
 * and nothing would ever wake it.
 *
 * Shaped like POSIX rather than returning the status directly, which is how it
 * shipped in v0.5.0. A shell forking two stages of a pipeline gets two statuses
 * back and needs to know which is which - the pipeline's own status is the last
 * stage's - and a status alone cannot say. The first real consumer is where an
 * API of this kind gets to be wrong, so it was changed while there was one.
 *
 * @param regs Live syscall frame. ebx is an int* in user memory receiving the
 *             status, or NULL when the caller only wants the pid; ecx carries
 *             WNOHANG and WUNTRACED. On return eax holds the pid of the child
 *             that reported, 0 when nothing is ready and the caller asked not to
 *             wait, or E_CHILD when it has no children left at all.
 */
void sys_wait(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = E_SRCH;
        return;
    }

    /* NULL is allowed and means the caller only wants to know which child. */
    int *user_status = (int *)regs->ebx;
    /*
     * WNOHANG: report that nothing is ready rather than block. A shell tracking
     * background jobs has to be able to ask without committing to a wait - it
     * has a prompt to print.
     *
     * ecx was the whole flag rather than a mask, and every existing caller
     * passes exactly 1 for it, so reading it as a bit field costs nothing and
     * leaves WUNTRACED somewhere to live.
     */
    int flags = (int)regs->ecx;
    int no_hang = (flags & WNOHANG) != 0;
    if (user_status && !validate_user_writable_pointer(user_status, sizeof(int))) {
        regs->eax = E_FAULT;
        return;
    }

    int child_pid = 0;
    int status = 0;

    if (take_parked_status(current_task->pid, &child_pid, &status)) {
        if (user_status && copy_to_user(user_status, &status, sizeof(int)) != E_OK) {
            /* The address was writable a few instructions ago, so this is close
             * to unreachable - but the status has already left the table, and
             * losing it silently would leave the caller waiting on a child that
             * can never report again. */
            klog(LOG_LEVEL_ERROR, "SYSCALL", "wait: status could not be written to user memory.");
            regs->eax = E_FAULT;
            return;
        }
        regs->eax = (uint32_t)child_pid;
        return;
    }

    /*
     * Exits first, stops second.
     *
     * A child that has finished is owed a collection - the slot holding its
     * status is a fixed resource and nobody else can free it - while a stop can
     * be reported at any point, because the child is still there. Asking in the
     * other order would let a shell with one stopped job and one that just
     * finished keep hearing about the stop.
     */
    if (flags & WUNTRACED) {
        int stop_sig = 0;
        if (take_parked_stop(current_task->pid, &child_pid, &stop_sig)) {
            status = WSTATUS_STOPPED | stop_sig;
            if (user_status && copy_to_user(user_status, &status, sizeof(int)) != E_OK) {
                klog(LOG_LEVEL_ERROR, "SYSCALL", "wait: stop notice could not be written to user memory.");
                regs->eax = E_FAULT;
                return;
            }
            regs->eax = (uint32_t)child_pid;
            return;
        }
    }

    /*
     * No status waiting and no child to produce one. Returning an error rather
     * than blocking matters: a parent that called wait() one time too many would
     * otherwise sleep until the machine was rebooted.
     */
    if (!has_pending_children(current_task->pid)) {
        regs->eax = E_CHILD;
        return;
    }

    /* Children exist, none has reported. Zero distinguishes that from E_CHILD,
     * which says there is nothing left to wait for at all. */
    if (no_hang) {
        regs->eax = 0;
        return;
    }

    /*
     * Block, and re-run this syscall when a child reports.
     *
     * Restarting rather than being handed the answer, which is what exec() does,
     * because the answer has to be written into the caller's own memory.
     * reap_task() runs with whichever directory happens to be in CR3 and cannot
     * reach it; it parks the status and wakes this task, and the loop above
     * collects it on the second pass with the right address space live.
     */
    if (!syscall_block_and_restart(regs, WAIT_PID)) {
        /* Not a syscall frame from Ring 3, so it cannot be rewound. Nothing here
         * can block safely; say so rather than sleeping forever. */
        regs->eax = E_AGAIN;
    }
}

/**
 * @brief Starts a program, either waiting for it or handing back its pid.
 *
 * The blocking form is what this call has always been: the caller sleeps in
 * WAIT_CHILD and reap_task() writes the child's exit status into its saved
 * frame. It is the right shape for init, which has nothing else to do while the
 * shell runs, and it is kept for that.
 *
 * EXEC_NOWAIT exists because a shell cannot use the blocking form to own a job.
 * It never learns the pid, so it cannot put the program in a group of its own,
 * cannot hand it the terminal, and - once a program can be stopped rather than
 * finished - has no way to name the thing it would bring back. Returning the pid
 * makes a foreground command exactly what a pipeline already is: a process the
 * shell placed, gave the terminal to, and waits for with wait().
 *
 * @param regs ebx is the path, ecx the mode (EXEC_WAIT or EXEC_NOWAIT), edx the
 *             argument string. On return eax is the child's exit status, or its
 *             pid for EXEC_NOWAIT, or a negative errno.
 */
void sys_exec(arch_regs_t *regs) {
    char target_path[MAX_PATH];
    if (!copy_user_string(target_path, (const char *)regs->ebx, sizeof(target_path))) {
        regs->eax = E_FAULT; 
        return; 
    }
    /*
     * The base directory for a relative program path is the caller's cwd, taken
     * from the PCB. It used to come from regs->ecx, which meant the caller chose
     * where its own lookup started.
     */
    fs_id_t calling_dir_id = current_task ? current_task->cwd_id : 0;

    char temp_args[128];
    for (int k = 0; k < 128; k++) temp_args[k] = '\0';
    const char *args_str = (const char *)regs->edx;
    
    if (args_str) {
        if (!copy_user_string(temp_args, args_str, sizeof(temp_args))) {
            regs->eax = E_FAULT; 
            return; 
        }
    }

    char basename[MAX_FILENAME];
    int parent_id = vfs_resolve_path(target_path, calling_dir_id, basename);
    if (parent_id < 0 || basename[0] == '\0') { 
        regs->eax = E_NOENT; 
        return; 
    }

    int bin_idx = fs_get_entry_idx("bin", 0);
    int bin_id = (bin_idx != -1) ? dir_table[bin_idx].entry_id : -1;

    // ROOT permission is required for programs outside the /bin directory
    if (parent_id != bin_id && current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "exec: Permission denied! (ROOT required for programs outside /bin)");
        regs->eax = E_ACCES; 
        return; 
    }

    int child_idx = load_and_exec_elf(basename, parent_id); 
    if (child_idx >= 0) {
        /*
         * The terminal is deliberately not touched here.
         *
         * This used to hand it to the new task's group, which looked right for
         * the one case it was written against - a command typed at the prompt -
         * and was wrong for every other. A background job forks, founds a group
         * of its own and then execs, so exec taking the terminal moved the job
         * into the foreground behind the shell's back: "grep foo &" followed by
         * Ctrl-C killed the job that was supposed to be out of reach.
         *
         * Nothing needs to happen instead. The new task inherits the caller's
         * group, so a program started by a task that already holds the terminal
         * holds it too, and one started by a background job does not. Which
         * group is in the foreground is the shell's decision and it makes it
         * with tcsetpgrp().
         */
        process_t *child_process = 0;
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == child_idx) { child_process = p; break; }
        }
        if (child_process) {
            int i = 0; 
            while (temp_args[i]) { 
                child_process->cmd_args[i] = temp_args[i]; 
            i++; 
            }
            child_process->cmd_args[i] = '\0';
        }

        /*
         * The caller asked for the pid rather than the outcome, so there is
         * nothing to wait for. The child is already on the run list and the
         * caller returns with it running - which is the whole point: it can now
         * place the child in a group, hand it the terminal, and wait for it with
         * a call that can report a stop as well as an exit.
         */
        if ((int)regs->ecx == EXEC_NOWAIT) {
            regs->eax = (uint32_t)child_idx;
            return;
        }

        /*
         * Which child this wait is for.
         *
         * reap_task() delivers into the frame below, and it used to do that for
         * whichever child died first - so a background job finishing while the
         * shell sat here returned the job's status as this program's. Recording
         * the pid is what lets the reaper tell them apart.
         */
        if (current_task) current_task->exec_child_pid = child_idx;

        /*
         * Publish the parent's return value BEFORE sleeping.
         *
         * sleep_current_task() snapshots *regs into the parent's PCB and then
         * schedule() overwrites *regs with the incoming task's context, so
         * anything written afterwards lands in a different task's saved
         * registers instead. The old order left the parent returning the
         * syscall number it went in with, and silently clobbered EAX for
         * whichever task ran next.
         *
         * E_OK is the placeholder for "the program started". The value the
         * caller actually receives is the child's exit status, written over
         * this one in the snapshot by exit_current_process() when the child
         * finishes. The two cannot be confused: a failure to start is reported
         * as a negative errno, and an exit status is masked to 0-255.
         */
        regs->eax = E_OK;
        sleep_current_task(regs, WAIT_CHILD);
    } else {
        regs->eax = E_NOEXEC;
    }
}

/**
 * @brief Function sys_set_priority
 */
void sys_set_priority(arch_regs_t *regs) {
    int target_pid = (int)regs->ebx;
    uint8_t new_priority = (uint8_t)regs->ecx;
    uint32_t my_uid = current_task->uid;
    
    int has_permission = 0;
    if (my_uid == 0) {
        has_permission = 1;
    } else {
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == target_pid && p->state != 0) {
                if (p->uid == my_uid) {
                    has_permission = 1;
                }
                break;
            }
        }
    }

    if (has_permission) {
        // [SECURITY PATCH NH-005]: CPU Starvation protection for normal users
        if (my_uid != 0 && new_priority > 10) {
            klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Maximum limit exceeded! DoS prevented.");
            new_priority = 10;
        }
        set_task_priority(target_pid, new_priority);
        regs->eax = 0;
    } else {
        klog(LOG_LEVEL_WARN, "SYSCALL", "set_priority: Permission denied (Unauthorized operation)!");
        regs->eax = E_PERM;
    }
}

/**
 * @brief Gives up the rest of the timeslice, and drives write-back on the way.
 *
 * The idle task is a Ring 3 loop of "mov eax, SYSCALL_YIELD; int 0x80" (see
 * init_multitasking()), so this is the one place in the kernel that is entered
 * continuously whenever nothing else wants the CPU - which makes it the right
 * place to notice that the block cache has been holding dirty sectors too long.
 *
 * It has to be here rather than in the timer handler: ata_write_sector() waits
 * for the ATA interrupt using hlt, and an ISR runs with interrupts masked, where
 * that hlt would never wake. Here we are in a syscall, on the task's own kernel
 * stack, with interrupts already re-enabled by syscall_handler().
 *
 * The flush happens before schedule(), because schedule() may switch away and
 * never come back to this frame. The flush itself is not interrupted: the timer
 * ISR only reschedules frames from Ring 3, so its arrival during the hlt loop
 * does not preempt us.
 */
void sys_yield(arch_regs_t *regs) {
    bcache_flush_if_due();
    schedule(regs);
}

/**
 * @brief Function sys_getuid
 */
void sys_getuid(arch_regs_t *regs) {
    if (current_task != 0) {
        regs->eax = current_task->uid;
    } else {
        regs->eax = E_FAULT;
    }
}

/**
 * @brief Function sys_getpid
 */
void sys_getpid(arch_regs_t *regs) {
    if (current_task != 0) {
        regs->eax = current_task->pid;
    } else {
        regs->eax = E_FAULT;
    }
}

/**
 * @brief Blocks the caller for a number of milliseconds.
 *
 * The first production use of WAIT_TIMER, which had been defined since the
 * beginning and referenced by one test.
 *
 * The mechanism is a deadline in the PCB plus wake_expired_sleepers() in
 * schedule(), not the kernel timer slots in signal.c: those are global, hold a
 * bare void(*)(void) and carry no pid, so they cannot express "wake this task
 * at tick N". wakeup_tasks(WAIT_TIMER) is equally unsuitable on its own - it
 * wakes every sleeper at once, whatever each of them asked for.
 *
 * Because syscall_block_and_restart() resumes on the trap instruction, this
 * handler runs again from the top when the task wakes, and sleep_active is what
 * tells the two entries apart. A wakeup that arrives early - anything else
 * calling wakeup_tasks(WAIT_TIMER) - finds the deadline still in the future and
 * blocks again rather than returning short.
 */
void sys_sleep(arch_regs_t *regs) {
    if (current_task == 0) { regs->eax = E_FAULT; return; }

    if (current_task->sleep_active) {
        uint32_t now = timer_get_ticks();

        /* Signed difference: timer_ticks wraps, see wake_expired_sleepers(). */
        if ((int32_t)(now - current_task->sleep_deadline) >= 0) {
            current_task->sleep_active = 0;
            current_task->sleep_deadline = 0;
            regs->eax = E_OK;
            return;
        }

        if (!syscall_block_and_restart(regs, WAIT_TIMER)) {
            current_task->sleep_active = 0;
            current_task->sleep_deadline = 0;
            regs->eax = E_AGAIN;
        }
        return;
    }

    int requested_ms = (int)regs->ebx;
    if (requested_ms < 0) { regs->eax = E_INVAL; return; }
    if (requested_ms == 0) { regs->eax = E_OK; return; }

    /*
     * Milliseconds to ticks, rounding up. Rounding up matters because TIMER_HZ
     * is 100, so the resolution is 10 ms and anything shorter would otherwise
     * convert to zero ticks and not wait at all - the one thing a sleep must
     * never do.
     *
     * Split into whole seconds and a remainder so the multiply cannot overflow:
     * ms * TIMER_HZ wraps past roughly 43 s of requested delay, and a wrapped
     * value produces a *shorter* sleep than asked for, which is exactly the
     * failure that would go unnoticed.
     */
    uint32_t ms = (uint32_t)requested_ms;
    uint32_t ticks = (ms / 1000u) * TIMER_HZ + ((ms % 1000u) * TIMER_HZ + 999u) / 1000u;
    if (ticks == 0) ticks = 1;

    current_task->sleep_deadline = timer_get_ticks() + ticks;
    current_task->sleep_active = 1;

    if (!syscall_block_and_restart(regs, WAIT_TIMER)) {
        /*
         * Not a live Ring 3 syscall frame, so the task cannot be suspended -
         * report that rather than pretending to have waited. Same contract as
         * the console read path in sys_read().
         */
        current_task->sleep_active = 0;
        current_task->sleep_deadline = 0;
        regs->eax = E_AGAIN;
    }
}

/**
 * @brief Function sys_get_args
 */
void sys_get_args(arch_regs_t *regs) {
    char *buf = (char *)regs->ebx;
    if (!validate_user_writable_pointer((const void *)buf, 128)) {
        regs->eax = E_FAULT; 
        return; 
    }
    
    int i = 0;
    char args[128];
    while (current_task->cmd_args[i] && i < 127) {
        args[i] = current_task->cmd_args[i];
        i++;
    }
    args[i] = '\0';
    regs->eax = copy_to_user(buf, args, (size_t)(i + 1)) == E_OK ? i : E_FAULT;
}


/* ── DEBUGGING AND MEMORY QUERIES ──────────────────────────── */

/**
 * @brief Function sys_stack_dump
 */
void sys_stack_dump(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    print_kernel_stack();
    regs->eax = 0;
}

/**
 * @brief Function sys_meminfo
 */
void sys_meminfo(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    /*
     * "Free" stopped being a complete answer when fork() started sharing pages
     * instead of duplicating them: almost nothing is spent at the fork itself,
     * and how much of the memory in use is held by more than one address space
     * is not visible from any other figure here.
     *
     * Reported in kilobytes because megabytes would round most of it away - a
     * shell and a forked child share a few dozen pages, not a few dozen
     * megabytes.
     */
    printk("RAM: Total %d MB | Free %d MB | Shared %d KB\n",
           pmm_get_total_memory() / (1024 * 1024),
           pmm_get_free_memory() / (1024 * 1024),
           (pmm_get_shared_frames() * PAGE_SIZE) / 1024);
    regs->eax = 0;
}

/**
 * @brief Function sys_test_malloc
 */
void sys_test_malloc(arch_regs_t *regs) {
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    char *w = (char *)kmalloc(50);
    if (w) { 
        w[0] = '4'; w[1] = '2'; w[2] = '\0'; 
        printk("Allocated: 0x%x, Size: %d\n", (uint32_t)w, kmalloc_size(w)); 
        kfree(w); 
    }
    regs->eax = 0;
}

/**
 * @brief Function sys_hexdump
 */
void sys_hexdump(arch_regs_t *regs) {
    if (!validate_user_pointer((const void *)regs->ebx, 64)) { 
        regs->eax = E_FAULT; 
        return; 
    }
    if (current_task->uid != 0) { 
        klog(LOG_LEVEL_WARN, "SYSCALL", "Permission denied: ROOT permission is required for this command.");
        regs->eax = E_PERM; 
        return; 
    }
    uint8_t data[64];
    if (copy_from_user(data, (const void *)regs->ebx, sizeof(data)) != E_OK) {
        regs->eax = E_FAULT;
        return;
    }
    print_hexdump((uint32_t)data, sizeof(data));
    regs->eax = E_OK;
}

/**
 * @brief Finds a live task by pid.
 *
 * @param pid Process to look for.
 * @return The task, or 0 when no live task has that pid.
 */
static process_t *find_task_by_pid(int pid) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == pid && p->state != TASK_EMPTY && p->state != TASK_DEAD) return p;
    }
    return 0;
}

/**
 * @brief Places a process in a process group.
 *
 * A caller may move itself or one of its children, and nothing else. That is
 * narrow on purpose: a group is what the terminal talks to and what Ctrl-C
 * reaches, so a program able to move an unrelated process into or out of a group
 * could take a shell's job away from it, or arrange to be interrupted in its
 * place.
 *
 * The shell and the child it has just forked both call this with the same
 * arguments, which is the ordinary answer to a race neither of them can win: the
 * child may run before the parent gets back from fork(), or the other way about,
 * and either order leaves the group set before anything can be signalled.
 *
 * @param regs ebx is the pid (0 for the caller), ecx the group (0 to found a new
 *             one named by that pid). On return eax is E_OK or a negative errno.
 */
void sys_setpgid(arch_regs_t *regs) {
    if (current_task == 0) { regs->eax = E_SRCH; return; }

    int pid = (int)regs->ebx;
    uint32_t pgid = regs->ecx;

    if (pid == 0) pid = current_task->pid;

    process_t *target = find_task_by_pid(pid);
    if (target == 0) { regs->eax = E_SRCH; return; }

    /* Itself, or one of its children. Root is not exempt: this is about which
     * process owns a job, not about privilege. */
    if (target != current_task && target->parent_pid != current_task->pid) {
        klog_int(LOG_LEVEL_WARN, "PROCESS",
                 "setpgid: refused, target is neither the caller nor its child. PID", pid);
        regs->eax = E_PERM;
        return;
    }

    if (pgid == 0) pgid = (uint32_t)pid;

    /*
     * A group has to be one that exists or one the target is founding. Naming an
     * arbitrary number would create a group with a single member that nothing
     * else could ever join, and - if the terminal were then handed to it - a
     * foreground group whose only member is not the process the user is looking
     * at.
     *
     * "Exists" means some live task is already in it, not that its founder is
     * still alive. A group outlives the process it is named after: the first
     * stage of a pipeline can finish while the rest are still running, and a
     * later stage must still be able to join the group it belongs to.
     */
    if (pgid != (uint32_t)pid) {
        int members = 0;
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->state == TASK_EMPTY || p->state == TASK_DEAD) continue;
            if (p->pgid == pgid) { members = 1; break; }
        }
        if (!members) { regs->eax = E_SRCH; return; }
    }

    target->pgid = pgid;
    regs->eax = E_OK;
}

/**
 * @brief Hands the terminal to a process group.
 *
 * Restricted to the caller's own group, or a group holding one of its children.
 * Without that a background job could take the terminal from the shell that
 * started it, and the next Ctrl-C would interrupt something the user was not
 * looking at while the thing they were looking at carried on.
 *
 * @param regs ebx is the group. On return eax is E_OK or a negative errno.
 */
void sys_tcsetpgrp(arch_regs_t *regs) {
    if (current_task == 0) { regs->eax = E_SRCH; return; }

    uint32_t pgid = regs->ebx;
    if (pgid == 0) { regs->eax = E_INVAL; return; }

    int allowed = (current_task->pgid == pgid);
    int exists = 0;

    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->state == TASK_EMPTY || p->state == TASK_DEAD) continue;
        if (p->pgid != pgid) continue;

        exists = 1;
        if (p->parent_pid == current_task->pid) allowed = 1;
    }

    /* A group with nobody in it would leave the terminal pointing at nothing,
     * and Ctrl-C would then reach no one at all. */
    if (!exists) { regs->eax = E_SRCH; return; }

    if (!allowed) {
        klog_int(LOG_LEVEL_WARN, "PROCESS",
                 "tcsetpgrp: refused, the caller owns neither the group nor a member. PGID", (int)pgid);
        regs->eax = E_PERM;
        return;
    }

    foreground_pgid = pgid;
    regs->eax = E_OK;
}

/**
 * @brief Reads a process's group.
 *
 * @param regs ebx is the pid, or 0 for the caller. On return eax is the group or
 *             a negative errno.
 */
void sys_getpgid(arch_regs_t *regs) {
    if (current_task == 0) { regs->eax = E_SRCH; return; }

    int pid = (int)regs->ebx;
    if (pid == 0) { regs->eax = current_task->pgid; return; }

    process_t *target = find_task_by_pid(pid);
    if (target == 0) { regs->eax = E_SRCH; return; }

    regs->eax = target->pgid;
}
