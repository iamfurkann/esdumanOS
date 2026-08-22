/*
 * File: test_reap.c
 * Purpose: Task reaping and the default action of fatal signals.
 *
 * Two things are under test here, and they are the same thing seen from two
 * sides. reap_task() ends a task without being that task - the step
 * exit_current_process() used to have welded into itself - and the default
 * action of SIG_KILL/SIG_TERM is the first caller that needs it: kill() reaps
 * its target from inside the killer's syscall.
 *
 * Before this existed a signal with no handler registered was recorded and then
 * dropped, so kill(1) could not terminate anything at all; a process that had
 * never called signal() ignored every signal the system could send it.
 *
 * Victims are given a real cloned address space rather than the fabricated cr3
 * the other scheduler tests use. The zombie reaper hands their page directory to
 * cleanup_process_memory(), which loads it into CR3 - a made-up value there is a
 * triple fault, not a failed assertion.
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

/*
 * Ring 0 trip through int 0x80, so a syscall wrapper can be tested rather than
 * the function behind it. register_user_signal() and sys_signal_reg() each held
 * a copy of the same address check; relaxing one for SIG_IGN and not the other
 * left every Ring 3 caller refused while a direct call succeeded, and a test
 * that only called the function could not see it.
 */
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/** Rounds used to warm the kernel heap before the frame count is baselined. */
#define REAP_WARMUP_ROUNDS  3
/** Measured create/kill rounds. A per-round leak of one frame is visible. */
#define REAP_LEAK_ROUNDS    8

/*
 * process.c's pid cursor and zombie list. Neither is in a header: they are
 * internal to the process module and this is the only code outside it with a
 * reason to look.
 */
extern int next_pid;
extern process_t *zombie_tasks_head;

/**
 * @brief Finds a task in the live run list by pid.
 *
 * @param pid Process id to look for.
 * @return The task, or 0 when no live task holds that pid.
 */
static process_t *find_live_task(int pid) {
    for (process_t *p = task_list_head; p != 0; p = p->next) {
        if (p->pid == pid) return p;
    }
    return 0;
}

/**
 * @brief Creates a task with a real address space.
 *
 * @param pid_out Receives the new pid; may be 0.
 * @return The new task, or 0 when either the directory or the PCB could not be
 *         allocated.
 */
static process_t *make_victim(int *pid_out) {
    uint32_t pd = clone_page_directory();
    if (pd == 0) return 0;

    int pid = create_process(0x1000, 0x2000, pd);
    if (pid < 0) {
        cleanup_process_memory(pd);
        return 0;
    }

    if (pid_out) *pid_out = pid;
    return find_live_task(pid);
}

/**
 * @brief Runs the zombie reaper without letting the scheduler switch tasks.
 *
 * schedule() reaps zombies before it consults multitasking_enabled, so clearing
 * that flag drains the list and returns at the guard rather than trying to
 * context switch out of a test running on the boot stack. Interrupts are put
 * back because schedule() masks them on entry and normally leaves the iret to
 * restore them.
 */
static void drain_zombies(void) {
    int saved = multitasking_enabled;

    multitasking_enabled = 0;
    schedule(0);
    multitasking_enabled = saved;

    asm volatile("sti");
}

/**
 * @brief Verifies task reaping and the default action of fatal signals.
 *
 * Expected behavior:
 * - A new PCB arrives zeroed, so no field carries whatever the heap held before.
 * - A pid held by a live task is never handed out a second time.
 * - A signal with a handler registered is delivered, not acted on by default.
 * - SIG_KILL and SIG_TERM terminate a target that has no handler, including one
 *   blocked in a syscall, and release its descriptors.
 * - A parent waiting on the victim is woken with the status in its saved frame.
 * - The terminal only changes hands when the task holding it dies.
 * - Repeated create/kill rounds return every frame.
 *
 * Edge cases covered:
 * - Killing a task that is not the running one, which is the case
 *   exit_current_process() could not express.
 * - A victim parked in TASK_WAITING rather than runnable.
 */
void run_reap_tests(void) {
    printk("\n--- Task Reaping / Signal Default Action Tests ---\n");

    uint32_t saved_fg = foreground_pgid;

    /* ------------------------------------------------------------------
     * A fresh PCB is zeroed.
     *
     * create_process() initialises the fields it has values for and used to
     * leave the rest - cmd_args, fpu_state, signal_saved_regs and the mailbox -
     * holding whatever the kernel heap last put there. Nothing read them before
     * writing them, so it never showed; fork() copies a PCB whole and would have
     * handed that garbage to the child.
     *
     * The heap is dirtied with a block of the same size first, so a missing
     * memset shows up as a real failure rather than as an accident of what the
     * allocator happened to return.
     * ------------------------------------------------------------------ */
    void *dirt = kmalloc(sizeof(process_t));
    if (dirt) {
        ft_memset(dirt, 0xAA, sizeof(process_t));
        kfree(dirt);
    }

    int fresh_pid = 0;
    process_t *fresh = make_victim(&fresh_pid);
    KTEST_ASSERT(fresh != 0, "[REAP] victim task created with a real address space");

    if (fresh != 0) {
        KTEST_ASSERT(fresh->cmd_args[0] == '\0',
                     "[STRICT] [REAP] a new PCB starts with empty cmd_args");
        KTEST_ASSERT(fresh->fpu_initialized == 0,
                     "[STRICT] [REAP] a new PCB starts with no FPU state claimed");
        KTEST_ASSERT(fresh->msg_count == 0 && fresh->mailbox[0].sender_pid == 0,
                     "[STRICT] [REAP] a new PCB starts with an empty mailbox");
        KTEST_ASSERT(fresh->signal_saved_regs.eip == 0 && fresh->in_signal_handler == 0,
                     "[STRICT] [REAP] a new PCB starts with no saved signal context");

        /* ------------------------------------------------------------------
         * A live pid is never reused.
         *
         * next_pid only moved forward and reset to 2 on overflow, with nothing
         * checking what it landed on. kill(), wait() and the foreground
         * bookkeeping all identify a task by pid and stop at the first match,
         * so two tasks sharing a number send signals and exit statuses to
         * whichever of them sits earlier in the list.
         * ------------------------------------------------------------------ */
        int saved_next = next_pid;
        next_pid = fresh_pid;

        int second_pid = 0;
        process_t *second = make_victim(&second_pid);
        KTEST_ASSERT(second != 0 && second_pid != fresh_pid,
                     "[STRICT] [REAP] a pid held by a live task is skipped, not reissued");

        next_pid = saved_next;
        if (second) {
            second->exit_code = 0;
            reap_task(second);
        }
    }

    /* ------------------------------------------------------------------
     * A registered handler wins over the default action.
     *
     * The whole point of registering a handler for SIG_KILL is to be told about
     * it instead of dying, so the default must not fire underneath one. This is
     * also the assertion that catches the default action being applied too
     * eagerly - to every signal, or before the handler lookup.
     * ------------------------------------------------------------------ */
    if (fresh != 0) {
        fresh->signal_handlers[SIG_KILL] = 0x40000000;
        send_user_signal(fresh_pid, SIG_KILL);

        KTEST_ASSERT(fresh->state != TASK_DEAD,
                     "[STRICT] [SIGNAL] a handled SIG_KILL does not terminate the target");
        KTEST_ASSERT((fresh->pending_signals & (1u << SIG_KILL)) != 0,
                     "[SIGNAL] a handled SIG_KILL is left pending for delivery");

        fresh->signal_handlers[SIG_KILL] = 0;
        fresh->pending_signals = 0;

        /* An unhandled signal with no default action is still ignored. */
        send_user_signal(fresh_pid, 4);
        KTEST_ASSERT(fresh->state != TASK_DEAD,
                     "[STRICT] [SIGNAL] a signal without a default action does not terminate");
        fresh->pending_signals = 0;

        fresh->exit_code = 0;
        reap_task(fresh);
    }

    /* ------------------------------------------------------------------
     * SIG_KILL terminates a target with no handler.
     *
     * The defect this release exists for: the pending bit was set, then cleared
     * by check_and_deliver_signals() without a handler to hand it to, and the
     * target carried on running.
     * ------------------------------------------------------------------ */
    foreground_pgid = current_task->pgid;

    int victim_pid = 0;
    process_t *victim = make_victim(&victim_pid);
    KTEST_ASSERT(victim != 0, "[REAP] victim task created for the SIG_KILL check");

    if (victim != 0) {
        send_user_signal(victim_pid, SIG_KILL);

        KTEST_ASSERT(victim->state == TASK_DEAD,
                     "[STRICT] [SIGNAL] an unhandled SIG_KILL terminates the target");
        KTEST_ASSERT(victim->exit_code == 128 + SIG_KILL,
                     "[STRICT] [SIGNAL] a signalled task exits with 128 + signal number");
        KTEST_ASSERT(victim->fd_table == 0,
                     "[STRICT] [REAP] the victim's descriptor table is released");
        KTEST_ASSERT(find_live_task(victim_pid) == 0,
                     "[STRICT] [REAP] the victim is unlinked from the run list");
        KTEST_ASSERT(foreground_pgid == current_task->pgid,
                     "[STRICT] [REAP] killing a background task does not take the terminal");
    }

    /* SIG_TERM shares the default action. */
    int term_pid = 0;
    process_t *term_victim = make_victim(&term_pid);
    if (term_victim != 0) {
        send_user_signal(term_pid, SIG_TERM);
        KTEST_ASSERT(term_victim->state == TASK_DEAD && term_victim->exit_code == 128 + SIG_TERM,
                     "[STRICT] [SIGNAL] an unhandled SIG_TERM terminates the target");
    }

    /* ------------------------------------------------------------------
     * SIG_PIPE joins the two of them.
     *
     * A writer whose reader has gone has nothing left to do. Refusing the write
     * was not enough on its own: the refusal is only a return value, and nothing
     * in userland reads one, so the writer ran to the end of its input with every
     * write failing quietly. The same default action stops it.
     * ------------------------------------------------------------------ */
    int pipe_pid = 0;
    process_t *pipe_victim = make_victim(&pipe_pid);
    if (pipe_victim != 0) {
        send_user_signal(pipe_pid, SIG_PIPE);
        KTEST_ASSERT(pipe_victim->state == TASK_DEAD,
                     "[STRICT] [SIGNAL] an unhandled SIG_PIPE terminates the target");
        KTEST_ASSERT(pipe_victim->exit_code == 128 + SIG_PIPE,
                     "[STRICT] [SIGNAL] a pipe-signalled task exits with 141");
    }

    /* ------------------------------------------------------------------
     * SIG_IGN declines a signal that would otherwise be fatal.
     *
     * The disposition a shell needs for itself: it has no business dying because
     * something it wrote to stopped reading. It lives in signal_handlers[] as a
     * sentinel rather than a real address, so "non-zero" no longer means "there
     * is somewhere to jump" - which is what the eip assertion further down is
     * guarding.
     * ------------------------------------------------------------------ */
    int ign_pid = 0;
    process_t *ign_victim = make_victim(&ign_pid);
    if (ign_victim != 0) {
        ign_victim->signal_handlers[SIG_PIPE] = SIG_IGN;
        send_user_signal(ign_pid, SIG_PIPE);

        KTEST_ASSERT(ign_victim->state != TASK_DEAD,
                     "[STRICT] [SIGNAL] a SIG_PIPE the target ignores does not terminate it");

        ign_victim->signal_handlers[SIG_PIPE] = 0;
        ign_victim->pending_signals = 0;
        ign_victim->exit_code = 0;
        reap_task(ign_victim);
    }

    /* register_user_signal() acts on the running task, so this half is tested
     * against ourselves. The address check has to let the sentinel through: 1 is
     * not a mappable user page, and without the exemption there would be no way
     * to decline a signal at all. */
    if (current_task != 0) {
        uint32_t saved_handler = current_task->signal_handlers[SIG_PIPE];
        uint32_t saved_pending = current_task->pending_signals;

        KTEST_ASSERT(register_user_signal(SIG_PIPE, SIG_IGN) == E_OK,
                     "[STRICT] [SIGNAL] SIG_IGN is accepted where a handler address would be rejected");
        KTEST_ASSERT(current_task->signal_handlers[SIG_PIPE] == SIG_IGN,
                     "[STRICT] [SIGNAL] the ignore disposition is stored");

        /* A real address that is not user memory is still refused - the
         * exemption must be for the sentinel alone. */
        KTEST_ASSERT(register_user_signal(SIG_PIPE, 0xC0001234) == E_FAULT,
                     "[STRICT] [SIGNAL] a kernel address is still refused as a handler");
        KTEST_ASSERT(current_task->signal_handlers[SIG_PIPE] == SIG_IGN,
                     "[SIGNAL] a refused registration leaves the previous disposition alone");

        /*
         * And the same three answers through the syscall.
         *
         * Not redundant with the calls above, which is the whole point: this
         * wrapper carried a second copy of the address check and went on
         * refusing SIG_IGN with E_FAULT after the copy in process.c had been
         * relaxed for it. Every Ring 3 caller - the shell included - was turned
         * away while these direct calls passed, so the tests said the feature
         * worked and no user of it could reach it.
         */
        current_task->signal_handlers[SIG_PIPE] = 0;

        KTEST_ASSERT(ktest_syscall(SYSCALL_SIGNAL_REG, SIG_PIPE, SIG_IGN, 0) == E_OK,
                     "[STRICT] [SIGNAL] signal() accepts SIG_IGN through the syscall, not just the function");
        KTEST_ASSERT(current_task->signal_handlers[SIG_PIPE] == SIG_IGN,
                     "[STRICT] [SIGNAL] the syscall stored the ignore disposition");

        KTEST_ASSERT(ktest_syscall(SYSCALL_SIGNAL_REG, SIG_PIPE, 0xC0001234, 0) == E_FAULT,
                     "[STRICT] [SIGNAL] the syscall still refuses a kernel address");
        KTEST_ASSERT(ktest_syscall(SYSCALL_SIGNAL_REG, MAX_USER_SIGNALS, 0, 0) == E_INVAL,
                     "[STRICT] [SIGNAL] the syscall refuses a signal number out of range");

        /*
         * Delivery discards an ignored signal instead of jumping to it. Without
         * the explicit branch in check_and_deliver_signals(), the sentinel would
         * be written straight into eip and the process would resume at address 1
         * - the one real risk of storing a disposition where an address goes.
         */
        arch_regs_t probe_regs;
        ft_bzero(&probe_regs, sizeof(probe_regs));
        probe_regs.eip = 0x08048000;

        current_task->pending_signals = (1u << SIG_PIPE);
        check_and_deliver_signals(&probe_regs);

        KTEST_ASSERT(probe_regs.eip == 0x08048000,
                     "[STRICT] [SIGNAL] delivering an ignored signal does not touch eip");
        KTEST_ASSERT((current_task->pending_signals & (1u << SIG_PIPE)) == 0,
                     "[STRICT] [SIGNAL] an ignored signal is cleared rather than left pending");
        KTEST_ASSERT(current_task->in_signal_handler == 0,
                     "[SIGNAL] an ignored signal does not enter a handler");

        current_task->signal_handlers[SIG_PIPE] = saved_handler;
        current_task->pending_signals = saved_pending;
    }

    /* ------------------------------------------------------------------
     * A blocked task can be killed.
     *
     * The case that motivated the default action in the first place: a task
     * parked in a syscall is exactly the one a user cannot get rid of any other
     * way. It has no live kernel continuation - the kernel is not preemptible,
     * so a task that is not running is parked at a syscall boundary with its
     * frame saved in p->regs - which is what makes reaping it from here sound.
     * ------------------------------------------------------------------ */
    int blocked_pid = 0;
    process_t *blocked = make_victim(&blocked_pid);
    if (blocked != 0) {
        blocked->state = TASK_WAITING;
        blocked->wait_reason = WAIT_KBD;

        send_user_signal(blocked_pid, SIG_KILL);
        KTEST_ASSERT(blocked->state == TASK_DEAD,
                     "[STRICT] [SIGNAL] a task blocked in a syscall can still be killed");
    }

    /* ------------------------------------------------------------------
     * A killed task's mutex is released.
     *
     * mutex_unlock() identifies the owner as current_task, which was the same
     * thing while a task's locks could only be dropped by its own exit. Reached
     * from kill() it is not: the ownership test would be made against the
     * killer, fail quietly, and strand the lock on a task that no longer exists.
     * Nothing revisits a mutex afterwards, so every later waiter would block for
     * the rest of the boot.
     * ------------------------------------------------------------------ */
    mutex_t held;
    mutex_init(&held);

    int holder_pid = 0;
    process_t *holder = make_victim(&holder_pid);
    if (holder != 0) {
        held.locked = 1;
        held.owner_pid = holder_pid;
        holder->held_mutex = &held;

        send_user_signal(holder_pid, SIG_KILL);

        KTEST_ASSERT(held.locked == 0 && held.owner_pid == -1,
                     "[STRICT] [REAP] a killed task's mutex is released, not stranded");
        KTEST_ASSERT(holder->held_mutex == 0,
                     "[REAP] the killed task no longer records the lock");
    }

    /* A mutex the victim does not own is left alone. */
    mutex_t other;
    mutex_init(&other);

    int nonholder_pid = 0;
    process_t *nonholder = make_victim(&nonholder_pid);
    if (nonholder != 0) {
        other.locked = 1;
        other.owner_pid = nonholder_pid + 1000;
        nonholder->held_mutex = &other;

        send_user_signal(nonholder_pid, SIG_KILL);

        KTEST_ASSERT(other.locked == 1 && other.owner_pid == nonholder_pid + 1000,
                     "[STRICT] [REAP] a lock held by someone else is not stolen by the reap");
    }

    /* ------------------------------------------------------------------
     * The idle task cannot be killed.
     *
     * schedule() reaches the idle task through a named pointer rather than
     * through the run list, so reaping it would leave that pointer aimed at
     * memory the zombie reaper had already freed - a use-after-free on the way
     * into the panic about having nothing to run. Unreachable while dying meant
     * calling exit(); a working kill() opens it up.
     *
     * The real idle task is not in the list the kernel-mode suite runs against,
     * so the guard is exercised by pointing idle_task at a victim of our own.
     * ------------------------------------------------------------------ */
    int idle_pid = 0;
    process_t *fake_idle = make_victim(&idle_pid);
    if (fake_idle != 0) {
        process_t *saved_idle = idle_task;
        idle_task = fake_idle;

        send_user_signal(idle_pid, SIG_KILL);
        KTEST_ASSERT(fake_idle->state != TASK_DEAD,
                     "[STRICT] [SIGNAL] SIG_KILL is refused against the idle task");

        reap_task(fake_idle);
        KTEST_ASSERT(fake_idle->state != TASK_DEAD,
                     "[STRICT] [REAP] reap_task refuses the idle task outright");

        idle_task = saved_idle;

        fake_idle->pending_signals = 0;
        fake_idle->exit_code = 0;
        reap_task(fake_idle);
        KTEST_ASSERT(fake_idle->state == TASK_DEAD,
                     "[REAP] the same task is reapable once it is no longer the idle task");
    }

    /* ------------------------------------------------------------------
     * The terminal moves when the task holding it dies.
     * ------------------------------------------------------------------ */
    int fg_pid = 0;
    process_t *fg_victim = make_victim(&fg_pid);
    if (fg_victim != 0) {
        /*
         * A group of its own. The victim inherited this task's group from
         * create_process(), and the terminal now belongs to a group rather than
         * to a process - so killing a member while another survives is exactly
         * the case that must *not* hand it on, and the check below would have
         * been testing the opposite of what it says.
         */
        fg_victim->pgid = (uint32_t)fg_pid;
        foreground_pgid = (uint32_t)fg_pid;
        send_user_signal(fg_pid, SIG_KILL);

        KTEST_ASSERT(foreground_pgid != (uint32_t)fg_pid,
                     "[STRICT] [REAP] the terminal is handed on when a group's last member dies");
        KTEST_ASSERT(foreground_pgid != 0,
                     "[REAP] a live task was found to take the terminal");
    }

    /*
     * And the other half of the same rule: a group that still has somebody in it
     * keeps the terminal. "ls | grep etc" is three tasks and one intention, and
     * the first stage exiting must not take the terminal from the stage that is
     * still running.
     */
    int pair_a = 0, pair_b = 0;
    process_t *task_a = make_victim(&pair_a);
    process_t *task_b = make_victim(&pair_b);

    if (task_a != 0 && task_b != 0) {
        task_a->pgid = (uint32_t)pair_a;
        task_b->pgid = (uint32_t)pair_a;
        foreground_pgid = (uint32_t)pair_a;

        send_user_signal(pair_a, SIG_KILL);
        KTEST_ASSERT(foreground_pgid == (uint32_t)pair_a,
                     "[STRICT] [REAP] a group with a survivor keeps the terminal");

        send_user_signal(pair_b, SIG_KILL);
        KTEST_ASSERT(foreground_pgid != (uint32_t)pair_a,
                     "[STRICT] [REAP] and gives it up once the last member goes");
    }

    /* ------------------------------------------------------------------
     * A parent blocked in wait() collects the status.
     *
     * Written into the parent's saved frame, because the parent is not running:
     * schedule() restores that copy into the real frame when it next picks the
     * task up. This is the path v0.5.0's wait() is built on, and it has to work
     * for a child that was killed as well as one that called exit().
     * ------------------------------------------------------------------ */
    int child_pid = 0;
    process_t *child = make_victim(&child_pid);
    if (child != 0) {
        process_t *parent = current_task;

        KTEST_ASSERT(child->parent_pid == parent->pid,
                     "[REAP] the victim was created as a child of the running task");

        parent->state = TASK_WAITING;
        parent->wait_reason = WAIT_CHILD;
        /*
         * Which child the wait is for. sys_exec() records this and the reaper
         * now requires it to match, because a parent in WAIT_CHILD used to be
         * woken by whichever child died first - a background job finishing while
         * the shell sat in exec() handed the shell the job's status as the
         * foreground command's. A task assembled here has to set it for the same
         * reason it has to set pgid: it never went through sys_exec().
         */
        parent->exec_child_pid = child_pid;
        parent->regs.eax = 0xDEADBEEF;

        send_user_signal(child_pid, SIG_KILL);

        KTEST_ASSERT(parent->state == TASK_RUNNING && parent->wait_reason == WAIT_NONE,
                     "[STRICT] [REAP] a parent waiting on the victim is woken");
        KTEST_ASSERT(parent->regs.eax == (uint32_t)(128 + SIG_KILL),
                     "[STRICT] [REAP] the killed child's status reaches the parent's saved frame");

        parent->state = TASK_RUNNING;
        parent->wait_reason = WAIT_NONE;
        parent->exec_child_pid = 0;
        parent->regs.eax = 0;
    }

    /* ------------------------------------------------------------------
     * And the other half of that rule: a child the parent is not waiting for
     * does not wake it.
     *
     * This is the defect the pid above exists to close. Two children, a parent
     * blocked in exec() for the second, and the first exiting - the parent used
     * to wake with the wrong status while the program it was waiting for was
     * still running, and the shell printed a prompt over the top of it.
     * ------------------------------------------------------------------ */
    int bystander_pid = 0;
    int wanted_pid = 0;
    process_t *bystander = make_victim(&bystander_pid);
    process_t *wanted = make_victim(&wanted_pid);

    if (bystander != 0 && wanted != 0) {
        process_t *parent = current_task;

        /*
         * Clear what earlier sections left behind first.
         *
         * Every victim killed above this point is a child of the running task
         * that nothing was waiting for, so each one parked a status - eight of
         * them by here. take_parked_status() hands back the *oldest*, so the
         * check below would be shown the first victim of the module rather than
         * the one it just killed, and would fail against a kernel doing exactly
         * the right thing. What a test asserts and what it needs in order to
         * assert it are two different things, and the second is the test's own
         * job to arrange.
         */
        int drained_pid = 0;
        int drained_code = 0;
        while (take_parked_status(parent->pid, &drained_pid, &drained_code)) { }

        parent->state = TASK_WAITING;
        parent->wait_reason = WAIT_CHILD;
        parent->exec_child_pid = wanted_pid;
        parent->regs.eax = 0xDEADBEEF;

        send_user_signal(bystander_pid, SIG_KILL);

        KTEST_ASSERT(parent->state == TASK_WAITING,
                     "[STRICT] [REAP] a child the parent is not waiting for leaves it blocked");
        KTEST_ASSERT(parent->regs.eax == 0xDEADBEEF,
                     "[STRICT] [REAP] and does not overwrite the status it is waiting for");

        int reported_pid = 0;
        int reported_code = 0;
        KTEST_ASSERT(take_parked_status(parent->pid, &reported_pid, &reported_code) &&
                     reported_pid == bystander_pid,
                     "[REAP] that child's status is parked for a later wait() instead");

        send_user_signal(wanted_pid, SIG_KILL);

        KTEST_ASSERT(parent->state == TASK_RUNNING,
                     "[STRICT] [REAP] the child it was waiting for does wake it");

        parent->state = TASK_RUNNING;
        parent->wait_reason = WAIT_NONE;
        parent->exec_child_pid = 0;
        parent->regs.eax = 0;
    }

    /* ------------------------------------------------------------------
     * Repeated create/kill rounds return every frame.
     *
     * The measurement fork() will lean on: v0.5.0 copies whole address spaces,
     * and a leak of one frame per child is invisible in any single round. The
     * warm-up rounds come first so that the kernel heap has already grown to the
     * size a PCB needs - heap_grow() maps frames and never gives them back, so
     * counting across the first allocation would measure the heap, not a leak.
     * ------------------------------------------------------------------ */
    for (int w = 0; w < REAP_WARMUP_ROUNDS; w++) {
        int pid = 0;
        process_t *v = make_victim(&pid);
        if (!v) break;
        send_user_signal(pid, SIG_KILL);
        drain_zombies();
    }

    uint32_t before = pmm_get_free_memory();
    int rounds_done = 0;

    for (int r = 0; r < REAP_LEAK_ROUNDS; r++) {
        int pid = 0;
        process_t *v = make_victim(&pid);
        if (!v) break;

        send_user_signal(pid, SIG_KILL);
        drain_zombies();
        rounds_done++;
    }

    uint32_t after = pmm_get_free_memory();

    KTEST_ASSERT(rounds_done == REAP_LEAK_ROUNDS,
                 "[REAP] every create/kill round completed");
    KTEST_ASSERT(after == before,
                 "[STRICT] [REAP] repeated create/kill rounds leak no physical frames");
    KTEST_ASSERT(zombie_tasks_head == 0,
                 "[STRICT] [REAP] the zombie list is empty once the reaper has run");

    /* Leave nothing of ours in the run list or the zombie list. */
    drain_zombies();
    foreground_pgid = saved_fg;
}
