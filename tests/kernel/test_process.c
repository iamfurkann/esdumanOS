/*
 * File: test_process.c
 * Purpose: Process scheduler unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"
#include "signal.h"
#include "libft.h"
#include "errno.h"
#include "syscall.h"

extern void sys_setpgid(arch_regs_t *regs);
extern void sys_tcsetpgrp(arch_regs_t *regs);
extern void sys_getpgid(arch_regs_t *regs);

/**
 * @brief Calls a group syscall handler and returns what it put in eax.
 */
static int call_pg(void (*fn)(arch_regs_t *), uint32_t ebx, uint32_t ecx) {
    arch_regs_t regs;

    ft_memset(&regs, 0, sizeof(regs));
    regs.ebx = ebx;
    regs.ecx = ecx;
    fn(&regs);
    return (int)regs.eax;
}

/**
 * @brief Process groups: who may join one, who may hold the terminal.
 *
 * Expected behavior:
 * - A task founds a group named by its own pid when it has no creator.
 * - A caller may place itself or a child, and nothing else.
 * - A group has to exist before anything can join it.
 * - The terminal may only be handed to a group the caller owns or parented.
 * - A signal to a group reaches every member.
 *
 * Edge cases covered:
 * - A group whose founder has exited but whose members have not.
 * - A pid that names no task.
 */
void run_pgroup_tests(void) {
    printk("\n--- Process Group Tests ---\n");

    if (current_task == 0) {
        KTEST_ASSERT(0, "[PGROUP] a task context is required");
        return;
    }

    uint32_t saved_fg = foreground_pgid;
    uint32_t saved_pgid = current_task->pgid;

    /* ------------------------------------------------------------------
     * Reading a group, and founding one.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(call_pg(sys_getpgid, 0, 0) == (int)current_task->pgid,
                 "[PGROUP] getpgid(0) reports the caller's own group");
    KTEST_ASSERT(call_pg(sys_getpgid, 999999, 0) == E_SRCH,
                 "[STRICT] [PGROUP] a pid that names no task is refused");

    KTEST_ASSERT(call_pg(sys_setpgid, 0, 0) == E_OK,
                 "[PGROUP] a task may found a group of its own");
    KTEST_ASSERT(current_task->pgid == (uint32_t)current_task->pid,
                 "[STRICT] [PGROUP] and that group is named by its pid");

    /* ------------------------------------------------------------------
     * A group has to exist before anything can join it.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(call_pg(sys_setpgid, 0, 987654) == E_SRCH,
                 "[STRICT] [PGROUP] joining a group nobody is in is refused");

    /* ------------------------------------------------------------------
     * Only itself or a child.
     *
     * The narrow rule is the point: a group is what the terminal talks to and
     * what Ctrl-C reaches, so a program able to move an unrelated process could
     * take a shell's job away from it, or arrange to be interrupted in its place.
     * ------------------------------------------------------------------ */
    process_t *stranger = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p != current_task && p != idle_task &&
            p->state != TASK_EMPTY && p->state != TASK_DEAD &&
            p->parent_pid != current_task->pid) {
            stranger = p;
            break;
        }
    }

    if (stranger != 0) {
        uint32_t before = stranger->pgid;
        KTEST_ASSERT(call_pg(sys_setpgid, (uint32_t)stranger->pid, 0) == E_PERM,
                     "[STRICT] [PGROUP] a task that is neither the caller nor its child is refused");
        KTEST_ASSERT(stranger->pgid == before,
                     "[STRICT] [PGROUP] and its group really did not move");
    }

    /* ------------------------------------------------------------------
     * The terminal.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(call_pg(sys_tcsetpgrp, current_task->pgid, 0) == E_OK,
                 "[PGROUP] a caller may hand the terminal to its own group");
    KTEST_ASSERT(foreground_pgid == current_task->pgid,
                 "[STRICT] [PGROUP] and the terminal follows");

    KTEST_ASSERT(call_pg(sys_tcsetpgrp, 0, 0) == E_INVAL,
                 "[PGROUP] group zero is not a group");
    KTEST_ASSERT(call_pg(sys_tcsetpgrp, 987654, 0) == E_SRCH,
                 "[STRICT] [PGROUP] and neither is one with nobody in it");

    if (stranger != 0 && stranger->pgid != current_task->pgid) {
        KTEST_ASSERT(call_pg(sys_tcsetpgrp, stranger->pgid, 0) == E_PERM,
                     "[STRICT] [PGROUP] a background task's group cannot take the terminal");
    }

    current_task->pgid = saved_pgid;
    foreground_pgid = saved_fg;
}
/**
 * @brief Tests the Process Scheduler's state management and task creation.
 *
 * This function validates the fundamental structures and behaviors of the kernel's
 * process scheduler, ensuring that processes can be created, properly queued, and
 * manipulated without interfering with live hardware states (such as interrupts).
 *
 * Expected Behavior:
 * - create_process returns a valid PID within the allowed max tasks boundary.
 * - The new process is correctly loaded into the task array with a RUNNING state.
 * - State transitions (e.g., RUNNING to WAITING) are accurately reflected.
 *
 * Edge Cases Covered:
 * - Isolation from real-time interrupts using CLI/STI to prevent accidental
 *   execution of incomplete dummy contexts which could cause Triple Faults.
 */
void run_process_tests(void) {
    printk("\n--- Process Scheduler Tests ---\n");

    asm volatile("cli");

    int pid = create_process(0x1000, 0x2000, 0x3000);
    KTEST_ASSERT(pid > 0, "Scheduler: create_process successfully returned a valid PID");
    
    process_t *ptask = 0;
    for(process_t *p = task_list_head; p != 0; p = p->next) {
        if(p->pid == pid) {
            ptask = p;
            break;
        }
    }
    
    KTEST_ASSERT(ptask != 0, "Scheduler: New task placed in Task Array");
    
    if (ptask != 0) {
        KTEST_ASSERT(ptask->state == TASK_RUNNING, 
                     "Scheduler: New task started with TASK_RUNNING state");
        
        ptask->state = TASK_WAITING;
        ptask->wait_reason = WAIT_TIMER;
        KTEST_ASSERT(ptask->state == TASK_WAITING, "Scheduler: Task successfully transitioned to WAITING state");
        
        ptask->state = TASK_EMPTY;
    }

    /* ------------------------------------------------------------------
     * Working directory inheritance.
     *
     * cwd_id lives in the PCB rather than in user space, so relative paths are
     * resolved against a directory the kernel chose. That only holds if a new
     * task actually picks the value up from its creator - otherwise every
     * exec'd program would silently land back at root, and "cd somewhere &&
     * run something" would not work.
     *
     * Checked against the creating task rather than a constant, so the
     * assertion still means something if the default ever changes.
     * ------------------------------------------------------------------ */
    uint8_t saved_cwd = current_task->cwd_id;

    current_task->cwd_id = 7;
    int child_pid = create_process(0x1000, 0x2000, 0x3000);
    process_t *child = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == child_pid) { child = p; break; }
    }

    KTEST_ASSERT(child != 0, "[CWD] child task created for the inheritance check");
    KTEST_ASSERT(child != 0 && child->cwd_id == 7,
                 "[STRICT] [CWD] a new task inherits the creator's working directory");

    /* A second child from a root-standing parent must land at root, not keep 7. */
    current_task->cwd_id = 0;
    int root_child_pid = create_process(0x1000, 0x2000, 0x3000);
    process_t *root_child = 0;
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == root_child_pid) { root_child = p; break; }
    }

    KTEST_ASSERT(root_child != 0 && root_child->cwd_id == 0,
                 "[STRICT] [CWD] inheritance tracks the creator, it is not sticky");

    if (child) child->state = TASK_EMPTY;
    if (root_child) root_child->state = TASK_EMPTY;
    current_task->cwd_id = saved_cwd;

    asm volatile("sti");

    /* ------------------------------------------------------------------
     * Live interrupt frame detection.
     *
     * Only the frame the ISR stub pushed on the current kernel stack may be
     * handed to the locking primitives. The VFS used to pass
     * &current_task->regs, which is a saved copy: writing through it corrupts
     * the task's stored context and leaves current_task and CR3 describing
     * different tasks. The guard has to reject it by address, because the two
     * are indistinguishable by content.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(!trap_frame_is_live(0),
                 "Locks: a null frame is not a live interrupt frame");
    KTEST_ASSERT(!trap_frame_is_live(&current_task->regs),
                 "[STRICT] Locks: the PCB's saved regs are rejected as a live frame");
    KTEST_ASSERT(trap_frame_is_live((arch_regs_t *)current_task->kstack),
                 "Locks: a frame inside the current kernel stack is accepted");
    KTEST_ASSERT(!trap_frame_is_live(
                     (arch_regs_t *)((uint32_t)current_task->kstack + KERNEL_STACK_SIZE)),
                 "[STRICT] Locks: a frame past the end of the kernel stack is rejected");

    /* ------------------------------------------------------------------
     * Read/write lock state machine.
     * ------------------------------------------------------------------ */
    rwlock_t rw;
    rwlock_init(&rw);

    rwlock_acquire_read(&rw, 0);
    KTEST_ASSERT(rw.readers == 1, "RWLock: read acquire counts the reader");
    rwlock_release_read(&rw);
    KTEST_ASSERT(rw.readers == 0, "RWLock: read release drops the count");

    rwlock_release_read(&rw);
    KTEST_ASSERT(rw.readers == 0,
                 "[STRICT] RWLock: an unmatched read release cannot underflow the count");

    rwlock_acquire_write(&rw, 0);
    KTEST_ASSERT(rw.writer_active == 1,
                 "[STRICT] RWLock: write acquire marks the lock held before returning");
    KTEST_ASSERT(rw.readers == 0, "RWLock: write acquire leaves the reader count alone");
    rwlock_release_write(&rw);
    KTEST_ASSERT(rw.writer_active == 0, "RWLock: write release clears the flag");

    /* ------------------------------------------------------------------
     * Syscall restart guard.
     *
     * Blocking used to be spelled "regs->eip -= 2", applied to whatever frame
     * the caller happened to be holding. Nothing checked that the frame was a
     * syscall entry at all, so an interrupt frame from Ring 3 would have had
     * its user EIP moved into the middle of an instruction. The replacement
     * refuses anything it cannot prove is a Ring 3 syscall frame owned by this
     * task, and a refusal must leave the task exactly as it found it.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(syscall_block_and_restart(0, WAIT_KBD) == 0,
                 "Syscall restart: a null frame is refused");
    KTEST_ASSERT(syscall_block_and_restart(&current_task->regs, WAIT_KBD) == 0,
                 "Syscall restart: refused when the task is not inside a syscall");

    // Now claim to be mid-syscall and offer the PCB's saved frame. Every field
    // in it looks plausible, so only the address check can reject it.
    current_task->regs.eip = 0xDEADBEEF;
    current_task->in_syscall = 1;
    int blocked = syscall_block_and_restart(&current_task->regs, WAIT_KBD);
    current_task->in_syscall = 0;

    KTEST_ASSERT(blocked == 0,
                 "[STRICT] Syscall restart: the PCB's saved frame is refused");
    KTEST_ASSERT(current_task->regs.eip == 0xDEADBEEF,
                 "[STRICT] Syscall restart: a refused frame is left untouched");
    KTEST_ASSERT(current_task->state != TASK_WAITING,
                 "[STRICT] Syscall restart: a refused call does not block the task");

    /* ------------------------------------------------------------------
     * Scheduler fallback.
     *
     * The scheduler must always have somewhere to go. When it found nothing
     * runnable it used to halt and return, which left the interrupt frame
     * untouched and resumed the task it had just rejected - a blocked task
     * carried on running as if it had never blocked. The idle task is the
     * invariant that makes that case impossible, so it is asserted directly.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(idle_task != 0, "[STRICT] Scheduler: an idle task exists as the fallback");
    KTEST_ASSERT(idle_task == 0 || idle_task->state == TASK_RUNNING,
                 "[STRICT] Scheduler: the idle task is always runnable");
    KTEST_ASSERT(idle_task == 0 || idle_task->base_priority == 0,
                 "Scheduler: the idle task has the lowest priority");
}
