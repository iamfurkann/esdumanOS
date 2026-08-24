/*
 * File: test_jobctl.c
 * Purpose: Stopping a task, continuing it, and telling its parent about both.
 *
 * The scheduler gained a state it had never had. Everything else here follows
 * from that: a task that is neither runnable nor waiting has to be invisible to
 * the selection passes, has to be skipped by every wakeup, has to keep the frame
 * it was on, and has to be able to come back to exactly the wait it was in.
 *
 * The half that is easy to get wrong is not the state, though - it is who finds
 * out. A shell blocked waiting for the job it started is blocked forever if a
 * stop is not reported to it, and the job cannot be continued because the only
 * process that would continue it is the one that is stuck. So the notification
 * paths are tested as carefully as the state machine.
 *
 * Victims are given a real cloned address space, like test_reap.c: the zombie
 * reaper loads a dying task's page directory into CR3, and a fabricated value
 * there is a triple fault rather than a failed assertion.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "kheap.h"
#include "libft.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "signal.h"
#include "syscall.h"
#include "errno.h"

extern void sys_kill(arch_regs_t *regs);
extern process_t *zombie_tasks_head;

/**
 * @brief Finds a task in the live run list by pid.
 *
 * @param pid Process id to look for.
 * @return The task, or 0 when no live task holds that pid.
 */
static process_t *jc_find_live(int pid) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == pid) return p;
    }
    return 0;
}

/**
 * @brief Creates a task with a real address space, as a child of the caller.
 *
 * @param pid_out Receives the new pid; may be 0.
 * @return The new task, or 0 when it could not be built.
 */
static process_t *jc_make_task(int *pid_out) {
    uint32_t pd = clone_page_directory();
    if (pd == 0) return 0;

    int pid = create_process(0x1000, 0x2000, pd);
    if (pid < 0) {
        cleanup_process_memory(pd);
        return 0;
    }

    if (pid_out) *pid_out = pid;
    return jc_find_live(pid);
}

/**
 * @brief Runs the zombie reaper without letting the scheduler switch tasks.
 *
 * schedule() reaps zombies before it consults multitasking_enabled, so clearing
 * that flag drains the list and returns at the guard rather than trying to
 * context switch out of a test running on the boot stack.
 */
static void jc_drain_zombies(void) {
    int saved = multitasking_enabled;

    multitasking_enabled = 0;
    schedule(0);
    multitasking_enabled = saved;

    asm volatile("sti");
}

/**
 * @brief Throws away every notification parked for the running task.
 *
 * Between assertions, so that one test's leftovers cannot be mistaken for the
 * next one's evidence. The slots are a fixed resource shared by every process
 * and this module parks a great many of them.
 */
static void jc_drain_notices(void) {
    int pid = 0;
    int code = 0;

    while (take_parked_status(current_task->pid, &pid, &code)) { }
    while (take_parked_stop(current_task->pid, &pid, &code)) { }
}

/**
 * @brief Calls sys_kill() with a fabricated frame and returns what it decided.
 *
 * The handler rather than a trip through int 0x80, deliberately: an int 0x80
 * from here ends in apply_default_signal_action() against the synthetic task
 * this module runs as, and a signal aimed at a group that happened to include it
 * would stop the test suite itself.
 *
 * @param pid Target pid, or the negated group.
 * @param sig Signal number.
 * @return The value sys_kill() put in eax.
 */
static int jc_kill(int pid, int sig) {
    arch_regs_t regs;

    ft_memset(&regs, 0, sizeof(regs));
    regs.ebx = (uint32_t)pid;
    regs.ecx = (uint32_t)sig;
    sys_kill(&regs);
    return (int)regs.eax;
}

/**
 * @brief Verifies stopping, continuing, and the reporting of both.
 *
 * Expected behavior:
 * - A stopped task leaves TASK_RUNNING and is not woken by anything that wakes
 *   a waiting one.
 * - What it was doing is remembered: a task stopped inside exec() goes back into
 *   that wait, and one stopped in any other wait is released as runnable so its
 *   syscall re-runs.
 * - A registered handler wins over the default action, so a program can decline
 *   to be stopped.
 * - The idle task cannot be stopped, for the reason it cannot be reaped.
 * - SIG_KILL still ends a stopped task.
 * - A parent blocked in wait() is woken and given a stop notice, and only sees
 *   it if it asked for one.
 * - Continuing withdraws a notice nobody collected.
 * - A foreground group that has stopped gives up the terminal.
 * - kill() with a negative pid reaches every member of a group.
 *
 * Edge cases covered:
 * - A group holding another user's process, which may not be signalled wholesale.
 * - A group with nobody in it.
 */
void run_jobctl_tests(void) {
    printk("\n--- Job Control (stop / continue) Tests ---\n");

    if (current_task == 0) {
        KTEST_ASSERT(0, "[JOBCTL] a task context is required");
        return;
    }

    /*
     * This module sets up everything it needs and puts it all back. Running it
     * alone with MODULE=jobctl has to mean the same thing as running it in the
     * middle of a full suite, which it cannot if it inherits a foreground group
     * or a uid from whatever ran before it.
     */
    uint32_t saved_fg = foreground_pgid;
    uint32_t saved_pgid = current_task->pgid;
    uint32_t saved_uid = current_task->uid;
    task_state_t saved_state = current_task->state;

    current_task->pgid = (uint32_t)current_task->pid;
    current_task->uid = 0;
    foreground_pgid = current_task->pgid;
    jc_drain_notices();

    /* ------------------------------------------------------------------
     * The state itself.
     * ------------------------------------------------------------------ */
    int pid_a = 0;
    process_t *task_a = jc_make_task(&pid_a);

    KTEST_ASSERT(task_a != 0, "[JOBCTL] a task was created to stop");

    if (task_a != 0) {
        stop_task(task_a, SIG_TSTP);

        KTEST_ASSERT(task_a->state == TASK_STOPPED,
                     "[JOBCTL] a stopped task is in TASK_STOPPED");
        KTEST_ASSERT(task_a->state != TASK_RUNNING,
                     "[STRICT] [JOBCTL] and is therefore not a candidate the scheduler can pick");
        KTEST_ASSERT((task_a->pending_signals & (1u << SIG_TSTP)) == 0,
                     "[STRICT] [JOBCTL] the signal that stopped it is not left pending");

        /* Nothing that wakes a waiting task may release a stopped one: the user
         * put it aside, and only a continue undoes that. */
        wakeup_tasks(WAIT_KBD);
        wakeup_tasks(WAIT_IPC);
        wake_expired_sleepers();
        KTEST_ASSERT(task_a->state == TASK_STOPPED,
                     "[STRICT] [JOBCTL] and no wakeup releases it");

        /* Stopping it twice is not two stops. */
        stop_task(task_a, SIG_TSTP);
        KTEST_ASSERT(task_a->state == TASK_STOPPED,
                     "[JOBCTL] stopping an already stopped task changes nothing");

        continue_task(task_a);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[JOBCTL] continuing a stopped task makes it runnable again");
        KTEST_ASSERT(task_a->wait_reason == WAIT_NONE,
                     "[STRICT] [JOBCTL] with no wait left over from the stop");

        continue_task(task_a);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[JOBCTL] continuing a task that is not stopped changes nothing");
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * What it was doing is remembered.
     *
     * A blocked syscall resumes on the trap instruction and re-evaluates what it
     * was waiting for - so releasing it as runnable both restores the wait and
     * repairs any wakeup that arrived while it was stopped. exec() is the one
     * exception: it returns through eax without re-running, so it has to go back
     * into the wait it was in.
     * ------------------------------------------------------------------ */
    if (task_a != 0) {
        task_a->state = TASK_WAITING;
        task_a->wait_reason = WAIT_KBD;

        stop_task(task_a, SIG_TSTP);
        KTEST_ASSERT(task_a->stopped_wait_reason == WAIT_KBD,
                     "[STRICT] [JOBCTL] a task stopped while blocked remembers what it was blocked on");
        KTEST_ASSERT(task_a->wait_reason == WAIT_NONE,
                     "[STRICT] [JOBCTL] and is no longer waiting as far as the wakeups are concerned");

        continue_task(task_a);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[STRICT] [JOBCTL] a restartable wait comes back runnable, so its syscall re-runs");

        task_a->state = TASK_WAITING;
        task_a->wait_reason = WAIT_CHILD;
        task_a->exec_child_pid = 4242;

        stop_task(task_a, SIG_TSTP);
        continue_task(task_a);
        KTEST_ASSERT(task_a->state == TASK_WAITING && task_a->wait_reason == WAIT_CHILD,
                     "[STRICT] [JOBCTL] a task stopped inside exec() goes back into that wait");

        task_a->state = TASK_RUNNING;
        task_a->wait_reason = WAIT_NONE;
        task_a->exec_child_pid = 0;
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * The default action, and declining it.
     * ------------------------------------------------------------------ */
    if (task_a != 0) {
        send_user_signal(pid_a, SIG_TSTP);
        KTEST_ASSERT(task_a->state == TASK_STOPPED,
                     "[JOBCTL] SIG_TSTP stops a task that has not handled it");

        send_user_signal(pid_a, SIG_CONT);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[JOBCTL] SIG_CONT puts it back to work");
        KTEST_ASSERT((task_a->pending_signals & (1u << SIG_CONT)) == 0,
                     "[STRICT] [JOBCTL] and leaves no pending signal behind, since a stopped task would never deliver it");

        /*
         * A handler wins. This is what lets the shell and init survive a Ctrl-Z
         * at an idle prompt: both are in the foreground group when there is no
         * job, and a stop they could not decline would park the session.
         */
        task_a->signal_handlers[SIG_TSTP] = 0x400000;
        send_user_signal(pid_a, SIG_TSTP);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[STRICT] [JOBCTL] a task with a SIG_TSTP handler is not stopped by default");
        KTEST_ASSERT((task_a->pending_signals & (1u << SIG_TSTP)) != 0,
                     "[JOBCTL] the signal is recorded for delivery instead");

        task_a->signal_handlers[SIG_TSTP] = SIG_IGN;
        task_a->pending_signals = 0;
        send_user_signal(pid_a, SIG_TSTP);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[STRICT] [JOBCTL] and one that ignores it is not stopped either");

        task_a->signal_handlers[SIG_TSTP] = 0;
        task_a->pending_signals = 0;

        /*
         * SIG_TTIN parks a job for the same reason and by the same route. It is
         * what a background job gets for reading the terminal, and stopping is
         * the answer that loses nothing: fg gives it the terminal it asked for.
         */
        send_user_signal(pid_a, SIG_TTIN);
        KTEST_ASSERT(task_a->state == TASK_STOPPED,
                     "[JOBCTL] SIG_TTIN stops a task that has not handled it");

        send_user_signal(pid_a, SIG_CONT);
        KTEST_ASSERT(task_a->state == TASK_RUNNING,
                     "[JOBCTL] and SIG_CONT releases it again");

        /*
         * A continue reaches user space only when somebody asked for it.
         *
         * The default action needs no delivery - being runnable again is the
         * whole of it - but a program that draws on the screen has to redraw
         * what the shell wrote over it while it was stopped, and there is no
         * other moment it could find out.
         */
        task_a->signal_handlers[SIG_CONT] = 0x400000;
        stop_task(task_a, SIG_TSTP);
        continue_task(task_a);
        KTEST_ASSERT((task_a->pending_signals & (1u << SIG_CONT)) != 0,
                     "[STRICT] [JOBCTL] a continue is delivered to a task that registered a handler");

        task_a->signal_handlers[SIG_CONT] = SIG_IGN;
        task_a->pending_signals = 0;
        stop_task(task_a, SIG_TSTP);
        continue_task(task_a);
        KTEST_ASSERT((task_a->pending_signals & (1u << SIG_CONT)) == 0,
                     "[STRICT] [JOBCTL] and not to one that declined it");

        task_a->signal_handlers[SIG_CONT] = 0;
        task_a->pending_signals = 0;
    }

    jc_drain_notices();

    /* The idle task is the scheduler's guarantee that there is always something
     * to pick, so it is no more stoppable than it is reapable. */
    if (idle_task != 0) {
        task_state_t before = idle_task->state;
        stop_task(idle_task, SIG_TSTP);
        KTEST_ASSERT(idle_task->state == before,
                     "[STRICT] [JOBCTL] the idle task cannot be stopped");
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * A stopped task can still be killed.
     *
     * It is parked, not protected. A job the user cannot get out of the way with
     * Ctrl-Z and then get rid of with kill would be a worse trap than no job
     * control at all.
     * ------------------------------------------------------------------ */
    if (task_a != 0) {
        stop_task(task_a, SIG_TSTP);
        send_user_signal(pid_a, SIG_KILL);

        KTEST_ASSERT(task_a->state == TASK_DEAD,
                     "[JOBCTL] SIG_KILL ends a task that is stopped");
        jc_drain_zombies();
        task_a = 0;
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * Who finds out, and what they are told.
     * ------------------------------------------------------------------ */
    int pid_b = 0;
    process_t *task_b = jc_make_task(&pid_b);

    if (task_b != 0) {
        stop_task(task_b, SIG_TSTP);

        int who = 0;
        int code = 0;
        KTEST_ASSERT(take_parked_status(current_task->pid, &who, &code) == 0,
                     "[STRICT] [JOBCTL] a stop is not an exit and is invisible to a plain wait()");

        KTEST_ASSERT(take_parked_stop(current_task->pid, &who, &code) == 1,
                     "[JOBCTL] a stop notice is parked for the parent");
        KTEST_ASSERT(who == pid_b && code == SIG_TSTP,
                     "[STRICT] [JOBCTL] naming the task that stopped and the signal that stopped it");

        /* One stop is one notice, however many times it is repeated. */
        stop_task(task_b, SIG_TSTP);
        continue_task(task_b);
        stop_task(task_b, SIG_TSTP);
        stop_task(task_b, SIG_TSTP);

        int notices = 0;
        while (take_parked_stop(current_task->pid, &who, &code)) notices++;
        KTEST_ASSERT(notices == 1,
                     "[STRICT] [JOBCTL] a second stop replaces the notice rather than queueing behind it");

        /* And continuing withdraws one nobody collected: a stop describes what a
         * task is, not something that happened to it. */
        stop_task(task_b, SIG_TSTP);
        continue_task(task_b);
        KTEST_ASSERT(take_parked_stop(current_task->pid, &who, &code) == 0,
                     "[STRICT] [JOBCTL] continuing withdraws an uncollected stop notice");

        /* A parent blocked in wait() is woken, because a stopped child will never
         * finish and the parent is the only thing that could continue it. */
        current_task->state = TASK_WAITING;
        current_task->wait_reason = WAIT_PID;

        stop_task(task_b, SIG_TSTP);

        KTEST_ASSERT(current_task->state == TASK_RUNNING &&
                     current_task->wait_reason == WAIT_NONE,
                     "[STRICT] [JOBCTL] a parent blocked in wait() is woken when its child stops");
        KTEST_ASSERT(take_parked_stop(current_task->pid, &who, &code) == 1 && who == pid_b,
                     "[JOBCTL] and the notice it will collect on the restart is waiting for it");

        current_task->state = TASK_RUNNING;
        current_task->wait_reason = WAIT_NONE;
        continue_task(task_b);
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * The terminal.
     *
     * A stopped group will not read a key and will not act on an interrupt, so
     * holding the terminal it cannot use would leave the user typing at nothing.
     * A group with a member still running keeps it, exactly as it does when one
     * member of a pipeline exits.
     * ------------------------------------------------------------------ */
    int pid_c = 0, pid_d = 0;
    process_t *task_c = jc_make_task(&pid_c);
    process_t *task_d = jc_make_task(&pid_d);

    if (task_c != 0 && task_d != 0) {
        task_c->pgid = (uint32_t)pid_c;
        task_d->pgid = (uint32_t)pid_c;
        foreground_pgid = (uint32_t)pid_c;

        stop_task(task_c, SIG_TSTP);
        KTEST_ASSERT(foreground_pgid == (uint32_t)pid_c,
                     "[STRICT] [JOBCTL] a group with a member still running keeps the terminal");

        stop_task(task_d, SIG_TSTP);
        KTEST_ASSERT(foreground_pgid != (uint32_t)pid_c,
                     "[JOBCTL] and gives it up once every member has stopped");
        KTEST_ASSERT(foreground_pgid != 0,
                     "[STRICT] [JOBCTL] a live task was found to take it, so an interrupt still reaches somebody");
    }

    jc_drain_notices();

    /*
     * And a task that never held the terminal cannot give it away.
     *
     * Waking a parent used to move the terminal to it whatever the dying task
     * was. The shell's wait() is woken by the job it is waiting for and by every
     * other child it has, so a background job finishing while a foreground job
     * ran took the terminal away from the job the user was looking at - and the
     * next interrupt reached a shell that ignores it rather than the program on
     * the screen.
     */
    int pid_fg = 0, pid_bg = 0;
    process_t *task_fg = jc_make_task(&pid_fg);
    process_t *task_bg = jc_make_task(&pid_bg);

    if (task_fg != 0 && task_bg != 0) {
        task_fg->pgid = (uint32_t)pid_fg;
        task_bg->pgid = (uint32_t)pid_bg;
        foreground_pgid = (uint32_t)pid_fg;

        current_task->state = TASK_WAITING;
        current_task->wait_reason = WAIT_PID;

        send_user_signal(pid_bg, SIG_KILL);

        KTEST_ASSERT(current_task->state == TASK_RUNNING,
                     "[JOBCTL] a background child exiting still wakes a parent in wait()");
        KTEST_ASSERT(foreground_pgid == (uint32_t)pid_fg,
                     "[STRICT] [JOBCTL] but does not take the terminal from the foreground group");

        current_task->state = TASK_RUNNING;
        current_task->wait_reason = WAIT_NONE;

        reap_task(task_fg);
        jc_drain_zombies();
    }

    jc_drain_notices();

    /* ------------------------------------------------------------------
     * Signalling a whole group by name.
     *
     * The shell needs this to continue a job: the process it forked is often not
     * the only member, because that process started the program the user is
     * actually looking at, and one Ctrl-Z stopped both.
     * ------------------------------------------------------------------ */
    if (task_c != 0 && task_d != 0) {
        KTEST_ASSERT(task_c->state == TASK_STOPPED && task_d->state == TASK_STOPPED,
                     "[JOBCTL] both members of the group are stopped to begin with");

        KTEST_ASSERT(jc_kill(-pid_c, SIG_CONT) == 0,
                     "[JOBCTL] kill() accepts a negated pid as a group");
        KTEST_ASSERT(task_c->state == TASK_RUNNING && task_d->state == TASK_RUNNING,
                     "[STRICT] [JOBCTL] and the continue reached every member, not just the leader");

        KTEST_ASSERT(jc_kill(-987654, SIG_CONT) == E_SRCH,
                     "[STRICT] [JOBCTL] a group with nobody in it is refused rather than quietly succeeding");

        /*
         * A group holding somebody else's process is out of reach for anyone but
         * root - checked against every member, because one that is not yours is
         * enough to make the group not yours.
         */
        current_task->uid = 1000;
        task_c->uid = 1000;
        task_d->uid = 7;

        KTEST_ASSERT(jc_kill(-pid_c, SIG_TSTP) == E_PERM,
                     "[STRICT] [JOBCTL] a group holding another user's process cannot be signalled");
        KTEST_ASSERT(task_c->state == TASK_RUNNING,
                     "[STRICT] [JOBCTL] and the refusal really did signal nobody");

        task_d->uid = 1000;
        KTEST_ASSERT(jc_kill(-pid_c, SIG_TSTP) == 0,
                     "[JOBCTL] a group entirely of the caller's own processes is allowed");
        KTEST_ASSERT(task_c->state == TASK_STOPPED && task_d->state == TASK_STOPPED,
                     "[JOBCTL] and every member stopped");

        current_task->uid = 0;
    }

    /* ------------------------------------------------------------------
     * Leave nothing of ours behind.
     * ------------------------------------------------------------------ */
    if (task_c != 0) reap_task(task_c);
    if (task_d != 0) reap_task(task_d);
    if (task_b != 0) reap_task(task_b);
    jc_drain_zombies();
    jc_drain_notices();

    KTEST_ASSERT(zombie_tasks_head == 0,
                 "[STRICT] [JOBCTL] the zombie list is empty once the reaper has run");

    current_task->state = saved_state;
    current_task->uid = saved_uid;
    current_task->pgid = saved_pgid;
    foreground_pgid = saved_fg;
}
