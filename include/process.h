#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "registers.h"
#include "isr.h"
/**
 * @brief Maximum number of tasks the OS can handle concurrently.
 */
#define MAX_TASKS 16

/**
 * @brief Per-process kernel stack size.
 *
 * Raised from 4 KB. The deepest measured path is sys_auth(), which reaches
 * roughly 2.5-3 KB on its own: verify_user_password() alone holds a 1 KB
 * /etc/shadow buffer, pbkdf2_hmac_sha256() adds 320 bytes, the VFS read below
 * it uses 512-byte sector buffers, and printk() contributes 256 bytes per call.
 * sys_open() and sys_exec() spend 512-640 bytes on path buffers before calling
 * anything. Interrupts nest on top of all of it, because syscall_handler()
 * re-enables them on entry.
 *
 * 8 KB leaves real headroom rather than a few hundred bytes. Overflow is now
 * caught by the double fault handler instead of triple faulting silently.
 */
#define KERNEL_STACK_SIZE 8192

/** 
 * @brief Maximum number of pending messages per task mailbox. 
 */
#define MAX_MESSAGES 8

/** 
 * @brief Maximum number of user-defined signal handlers per process. 
 */
#define MAX_USER_SIGNALS 32

/**
 * @brief Enumeration of possible task states.
 * Defines the current execution status of a process.
 */
typedef enum { TASK_EMPTY, TASK_RUNNING, TASK_WAITING, TASK_DEAD } task_state_t;

/**
 * @brief Enumeration of reasons a task might be in a waiting state.
 * Helps the scheduler determine when a task is ready to resume execution.
 */
typedef enum { 
    WAIT_NONE = 0, 
    WAIT_KBD = 1,   
    WAIT_IPC = 2,   
    WAIT_TIMER = 3, 
    WAIT_MUTEX = 4,
    WAIT_CHILD = 5,
    /*
     * Blocked in wait(), as distinct from blocked inside exec().
     *
     * Both are waiting for a child, and reap_task() has to tell them apart
     * because it delivers the status differently. exec() gets it written
     * straight into its saved frame - it returns the status itself, and cannot
     * re-run without launching the program a second time. wait() has to write
     * the status into user memory, which the reaper cannot do from another
     * address space, so its status is parked and the syscall is restarted to
     * collect it with the caller's own directory live.
     */
    WAIT_PID = 6
} wait_reason_t;

/**
 * @brief Structure representing a message for Inter-Process Communication (IPC).
 */
typedef struct {
    uint32_t sender_pid;
    uint32_t payload;
} message_t;

/**
 * @brief Maximum number of file descriptors a single task can have open.
 */
#define MAX_FD_PER_TASK 32

/**
 * @brief Macros defining different types of file descriptors.
 */
#define FD_TYPE_NONE    0
#define FD_TYPE_CONSOLE 1 // Screen / Keyboard
#define FD_TYPE_FILE    2 // VFS File
#define FD_TYPE_PIPE    3 // Pipe
#define FD_TYPE_DEVICE  4 

/**
 * @brief Structure representing an open file descriptor for a task.
 */
typedef struct {
    uint8_t type;       // File type
    uint32_t ptr;       // pipe_t* or vfs_file_t* memory address
    uint32_t offset;    // Read/Write cursor
    uint8_t mode;       // O_RDONLY, O_WRONLY etc.
} file_descriptor_t;

/**
 * @brief Process Control Block (PCB) structure.
 * Contains all the necessary context, state, and resources for a task.
 */
typedef struct process_s {
    int pid;
    int parent_pid;
    uint32_t uid;

    /*
     * Current working directory, as a VFS directory entry id (0 is root).
     *
     * Lives here rather than in user space because the shell used to keep it in
     * a global of its own and pass it down to every syscall that touched a path.
     * That made relative-path resolution something the caller chose, so a process
     * could name any directory id it liked as the base; with the value in the PCB
     * the kernel decides, and userspace can only move it through chdir().
     *
     * Inherited from the creating process, like uid - see create_process(). fork()
     * will rely on that same inheritance when it lands.
     */
    uint8_t cwd_id;

    /*
     * sleep() bookkeeping.
     *
     * sleep_deadline is an absolute timer_get_ticks() value, not a countdown,
     * so the sweep in schedule() can decide whether a task is due without
     * anything having to decrement it per tick. Comparisons against it are
     * signed differences: timer_ticks is a 32-bit counter and wraps roughly
     * every 497 days at TIMER_HZ, and a plain >= would then sleep for weeks.
     *
     * sleep_active exists because a blocking syscall re-runs from the start when
     * it wakes - see syscall_block_and_restart(). Without a flag, sys_sleep()
     * could not tell a fresh call from a resumption and would arm itself again
     * forever. It also lets a spurious wakeup be told from a real one, so the
     * task can go back to sleep rather than return early.
     *
     * The kernel timer slots in signal.c cannot serve this: they are global,
     * hold a bare void(*)(void), and carry no pid to wake.
     */
    uint32_t sleep_deadline;
    uint8_t sleep_active;

    /*
     * Status this task passed to exit(), 0-255.
     *
     * sys_exit() used to discard its argument outright and nothing anywhere
     * held an exit status, so a parent could learn that its child had finished
     * but never how it went. The shell's && and || are built on exactly that
     * answer, and with none available it recorded success unconditionally -
     * "stat /no_such_file && echo CHAINED" printed CHAINED.
     *
     * Masked to the low 8 bits on the way in, which is both what POSIX exposes
     * through wait() and what keeps the value from ever looking like the
     * negative errno sys_exec() returns when a program could not be started.
     */
    int exit_code;

    task_state_t state;
    wait_reason_t wait_reason;
    mutex_t *wait_mutex;
    arch_regs_t regs;
    arch_paddr_t page_directory;
    uint8_t base_priority; 
    uint8_t current_priority;

    message_t mailbox[MAX_MESSAGES];
    uint8_t msg_head;
    uint8_t msg_tail;
    uint8_t msg_count;

    uint32_t pending_signals;
    uint32_t signal_handlers[MAX_USER_SIGNALS];
    arch_regs_t signal_saved_regs; 
    uint8_t in_signal_handler;

    /*
     * Syscall restart bookkeeping. syscall_handler() records where the trap came
     * from so a blocking syscall can resume on the trapping instruction rather
     * than after it - without guessing the instruction length at the blocking
     * site, and without assuming the frame it was handed is a syscall frame at
     * all. in_syscall is what distinguishes a syscall frame from an interrupt
     * frame that merely also came from Ring 3.
     */
    uint8_t in_syscall;
    uint32_t syscall_entry_eip;

    uint8_t kstack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
    mutex_t *held_mutex;
    file_descriptor_t *fd_table;
    uint32_t fd_table_size;

    char cmd_args[128];

    /*
     * The program break: where this task's heap starts, and where it currently
     * ends. Both are page-aligned, and both are zero for a task that was not
     * built from an ELF image - the idle task, and anything the test suite
     * creates by hand. sys_brk() refuses to move a break that was never set
     * rather than inventing one, because a heap has to begin above the program's
     * own data and there is no program here to be above.
     *
     * The loader sets these from the highest address any PT_LOAD segment
     * reaches; fork() carries them over, since the child is the same image at
     * the same instruction and its heap is already mapped.
     */
    uint32_t brk_start;
    uint32_t brk_current;

    /*
     * Where this process has read up to in the kernel log through /dev/kmsg.
     *
     * Per process rather than per open descriptor, because the device interface
     * has no per-descriptor state to hang a cursor on and a single global one
     * would have two readers stealing records from each other. Linux keeps one
     * per open; two descriptors in the same program share a position here.
     *
     * Zero means "start at the oldest record still held", which is where a fresh
     * process and a process whose cursor has fallen off the back of the ring
     * both end up.
     */
    uint32_t kmsg_seq;

    uint8_t fpu_state[512] __attribute__((aligned(16)));
    int fpu_initialized;
    uint32_t auth_fail_ticks;
    struct process_s *next;
    struct process_s *prev;
} process_t;

/**
 * @brief Maximum number of CPU cores supported by the OS.
 */
#define MAX_CPUS 8

/**
 * @brief Structure representing the state of a CPU core.
 */
typedef struct {
    int cpu_id;               // Core's hardware ID (Local APIC ID)
    process_t *active_task;         // Task currently running on this processor
    int is_bsp;               // Is this processor the Boot Strap Processor (Main Processor)?
    uint32_t local_tss_addr;  // Each core must have its own TSS
} cpu_state_t;

extern cpu_state_t cpus[MAX_CPUS];

/**
 * @brief Retrieves the hardware ID of the currently executing CPU core.
 * @return Returns the Local APIC ID of the core.
 */
static inline int get_current_cpu_id(void) {
    // This will be converted to APIC ID reading code in the future.
    return 0; 
}

/**
 * @brief Macro to get the active task ID of the current CPU.
 */
#define current_task (cpus[get_current_cpu_id()].active_task)

extern process_t *task_list_head;
extern process_t *task_list_tail;
extern int multitasking_enabled;
extern int foreground_task;

/**
 * @brief The idle task: always present, always runnable.
 *
 * The scheduler's fallback when nothing else can run, so that it never has to
 * resume a task it has just found unrunnable.
 */
extern process_t *idle_task;

/**
 * @brief Initializes a mutex.
 * @param m Pointer to the mutex to initialize.
 */
void mutex_init(mutex_t *m);

/**
 * @brief Locks a mutex, putting the current task to sleep if already locked.
 * @param m Pointer to the mutex to lock.
 * @param regs CPU registers state to save if context switch occurs.
 */
void mutex_lock(mutex_t *m, arch_regs_t *regs);

/**
 * @brief Unlocks a mutex and wakes up a waiting task if any.
 *
 * Releases the lock only if the running task is the one holding it.
 *
 * @param m Pointer to the mutex to unlock.
 */
void mutex_unlock(mutex_t *m);

/**
 * @brief Releases a mutex on behalf of a named owner, waking a waiter if any.
 *
 * The form reap_task() needs: a task being killed is not the task running, so
 * the ownership test cannot be made against current_task. Performs none of the
 * interrupt or multitasking checks of its own - mutex_unlock() is the guarded
 * entry point for ordinary unlocking.
 *
 * @param m Pointer to the mutex to release.
 * @param owner Task the lock must belong to; nothing happens if it does not.
 */
void mutex_release_owned_by(mutex_t *m, process_t *owner);

/**
 * @brief Registers a custom signal handler for the current process.
 *
 * Sole owner of the disposition check, so that sys_signal_reg() has nothing of
 * its own to keep in step - it had a duplicate and they drifted apart.
 *
 * @param sig_num Signal number to handle.
 * @param handler_addr Address of the user-space handler, or SIG_IGN (1) to
 *                     discard the signal, or 0 for the default action.
 * @return E_OK when the disposition was stored, E_INVAL for a signal number out
 *         of range, E_FAULT for a handler address user space could not execute.
 */
int register_user_signal(int sig_num, uint32_t handler_addr);

/**
 * @brief Sends a signal to a specific process.
 * @param target_pid The Process ID to send the signal to.
 * @param sig_num The signal number to send.
 */
void send_user_signal(int target_pid, int sig_num);


/**
 * @brief Creates a new process.
 * @param eip Instruction pointer entry point.
 * @param esp Stack pointer.
 * @param cr3 Page directory physical address.
 * @return The Process ID (PID) of the created process.
 */
int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);

/**
 * @brief Task scheduler function. Context switches to the next ready task.
 * @param regs Pointer to the saved registers of the currently running task.
 */
void schedule(arch_regs_t *regs);

/**
 * @brief Sets the kernel stack for the current CPU.
 * @param stack Pointer to the top of the kernel stack.
 */
void set_kernel_stack(uint32_t stack);

/*
 * switch_to_user_mode() is gone. It was an assembly routine that built an iret
 * frame by hand to drop into Ring 3, declared in both this header and kernel.h,
 * and called from nowhere - start_first_task() below does the same job through
 * the scheduler, which is the only path a task actually takes into user mode.
 */

/**
 * @brief Starts execution of the very first task.
 */
void start_first_task(void);

/**
 * @brief Initializes the multitasking subsystem.
 */
void init_multitasking(void);

/**
 * @brief Checks for pending signals and prepares the task to execute them.
 *
 * Handler delivery only; an unhandled fatal signal is left pending for
 * apply_default_signal_action(). Safe to call from schedule().
 *
 * @param regs Pointer to the saved registers of the current task.
 */
void check_and_deliver_signals(arch_regs_t *regs);

/**
 * @brief Terminates the running task if it holds an unhandled fatal signal.
 *
 * Call only from the return-to-user path. It reaches exit_current_process(),
 * which calls schedule(), so calling it from the scheduler recurses.
 *
 * @param regs Live interrupt frame of the returning task.
 */
void apply_default_signal_action(arch_regs_t *regs);

/**
 * @brief Gives a new task a reference-counted copy of another's descriptors.
 *
 * Shared by exec() and fork(). Standard descriptors are not defaulted here — that
 * is exec()'s business, since a forked child inherits the parent's table verbatim.
 *
 * @param child  Task receiving the copies; its table must already be allocated.
 * @param parent Task to copy from.
 */
void inherit_fd_table(process_t *child, process_t *parent);

/**
 * @brief Copies the accumulated parent state a forked child continues with.
 *
 * Signal handlers, priority, arguments, FPU state and the authentication rate
 * limit. uid and cwd_id are not repeated — create_process() already inherits them
 * from the creating task.
 *
 * @param child  Freshly created task.
 * @param parent Task being forked.
 */
void inherit_pcb_state(process_t *child, process_t *parent);

/**
 * @brief Takes one exit status parked for a parent, oldest first.
 *
 * A child that finishes while its parent is not blocked in wait() leaves its
 * status behind rather than losing it. Before fork() this could not happen — the
 * only way to have a child was exec(), which blocks the caller first.
 *
 * @param parent_pid    Parent asking.
 * @param child_pid_out Receives the pid the status belonged to.
 * @param exit_code_out Receives the status when one is found.
 * @return 1 when a status was taken, 0 when none is parked for that parent.
 */
int take_parked_status(int parent_pid, int *child_pid_out, int *exit_code_out);

/**
 * @brief Whether a task has any child that could still report a status.
 *
 * Counts both live children and statuses already parked, so wait() can tell "not
 * yet" from "never" and refuse to block on the second.
 *
 * @param pid Parent to check.
 * @return 1 when something is still outstanding, 0 otherwise.
 */
int has_pending_children(int pid);

/**
 * @brief Releases a task's resources and hands it to the zombie reaper.
 *
 * Everything a task's death entails except switching away from it, so that a
 * task can be ended by something other than itself. The address space is not
 * freed here; the reaper in schedule() does that.
 *
 * @param victim Task to reap; may or may not be the running task.
 */
void reap_task(process_t *victim);


// --- Added by Refactor Script ---
/* process_pending_kernel_timers() is declared in signal.h, which owns the kernel
 * timer slots; it was duplicated here. schedule() calls it, and process.c gets
 * signal.h through kernel.h. */
extern void sleep_current_task(arch_regs_t *regs, int reason);
extern void wakeup_tasks(int reason);

/**
 * @brief Wakes every task whose sleep() deadline has passed.
 *
 * Called from schedule() before a task is selected, so a task that becomes due
 * this pass can be chosen in it. Distinct from wakeup_tasks(WAIT_TIMER), which
 * would wake every sleeper at once regardless of when each asked to be woken.
 */
extern void wake_expired_sleepers(void);
extern int check_free_task_slot(void);
extern void exit_current_process(arch_regs_t *regs);
extern void set_task_priority(int pid, uint8_t priority);
extern int send_message(int to_pid, uint32_t payload);
extern int receive_message(uint32_t *sender_out, uint32_t *payload_out);
/*
 * schedule_kernel_timer() is NOT declared here any more. The declaration that
 * used to sit on this line - "int schedule_kernel_timer(int ticks, int pid)" -
 * matched no definition in the tree: signal.c defines
 * "void schedule_kernel_timer(int timer_id, uint32_t delay_ticks)". The two
 * disagreed on the meaning of both parameters and on the return type, and
 * sys_alarm() - which includes this header - was compiled against the wrong one.
 * The real declaration lives in signal.h; include that instead.
 */

typedef struct {
    int readers;
    int writer_active;
    mutex_t mutex;
} rwlock_t;

/**
 * @brief Tells a live interrupt frame from a saved copy of one.
 *
 * Callers that hold the frame the ISR stub pushed may pass it to the locking
 * primitives so a contended lock can sleep. Callers that do not must pass 0 -
 * never &current_task->regs, which is a saved copy and corrupts the task's
 * stored context when written through.
 *
 * @param regs Candidate frame.
 * @return 1 when regs lies wholly inside the current task's kernel stack.
 */
int trap_frame_is_live(const arch_regs_t *regs);

/**
 * @brief Blocks the current task so that its syscall re-runs when it wakes.
 *
 * Replaces the "regs->eip -= 2" idiom. That rewound whatever frame it was
 * handed, on the assumption that it was always an int 0x80 entry - an
 * assumption nothing enforced, so an interrupt frame from Ring 3 would have had
 * a user EIP moved into the middle of an instruction.
 *
 * @param regs   Live interrupt frame of the calling task.
 * @param reason Why the task is blocking; whoever satisfies it calls
 *               wakeup_tasks() with the same value.
 * @return 1 when the task was blocked, 0 when it could not be - the caller
 *         should then report E_AGAIN rather than assume it slept.
 */
int syscall_block_and_restart(arch_regs_t *regs, wait_reason_t reason);

void rwlock_init(rwlock_t *lock);
void rwlock_acquire_read(rwlock_t *lock, arch_regs_t *regs);
void rwlock_release_read(rwlock_t *lock);
void rwlock_acquire_write(rwlock_t *lock, arch_regs_t *regs);
void rwlock_release_write(rwlock_t *lock);

#endif
