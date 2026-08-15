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

    if (copy_user_space(child_pd) != E_OK) {
        cleanup_process_memory(child_pd);
        regs->eax = E_NOMEM;
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
 * @param regs Live syscall frame. On return eax holds the child's status, or
 *             E_CHILD when the caller has no children left to wait for.
 */
void sys_wait(arch_regs_t *regs) {
    if (current_task == 0) {
        regs->eax = E_SRCH;
        return;
    }

    int status = 0;
    if (take_parked_status(current_task->pid, &status)) {
        regs->eax = (uint32_t)status;
        return;
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

    /*
     * Block. reap_task() writes the status into this task's saved frame and marks
     * it runnable again, the same delivery sys_exec() relies on - so nothing is
     * published into eax here, because whatever were written would be overwritten
     * by exactly that.
     */
    sleep_current_task(regs, WAIT_CHILD);
}

/**
 * @brief Function sys_exec
 */
void sys_exec(arch_regs_t *regs) {
    char target_path[MAX_FILENAME];
    if (!copy_user_string(target_path, (const char *)regs->ebx, sizeof(target_path))) {
        regs->eax = E_FAULT; 
        return; 
    }
    /*
     * The base directory for a relative program path is the caller's cwd, taken
     * from the PCB. It used to come from regs->ecx, which meant the caller chose
     * where its own lookup started.
     */
    uint8_t calling_dir_id = current_task ? current_task->cwd_id : 0;

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
        foreground_task = child_idx;
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
    printk("RAM: Total %d MB | Free %d MB\n", pmm_get_total_memory() / (1024 * 1024), pmm_get_free_memory() / (1024 * 1024));
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
