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
    WAIT_CHILD = 5
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

    uint8_t kstack[4096] __attribute__((aligned(16)));
    mutex_t *held_mutex;
    file_descriptor_t *fd_table;
    uint32_t fd_table_size;

    char cmd_args[128];

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
 * @param m Pointer to the mutex to unlock.
 */
void mutex_unlock(mutex_t *m);

/**
 * @brief Registers a custom signal handler for the current process.
 * @param sig_num Signal number to handle.
 * @param handler_addr Memory address of the user-space handler function.
 */
void register_user_signal(int sig_num, uint32_t handler_addr);

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

/**
 * @brief Switches execution to user mode.
 * @param eip Entry point in user space.
 * @param esp Stack pointer in user space.
 */
void switch_to_user_mode(uint32_t eip, uint32_t esp);

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
 * @param regs Pointer to the saved registers of the current task.
 */
void check_and_deliver_signals(arch_regs_t *regs);


// --- Added by Refactor Script ---
extern void process_pending_kernel_timers(void);
extern void sleep_current_task(arch_regs_t *regs, int reason);
extern void wakeup_tasks(int reason);
extern int check_free_task_slot(void);
extern void exit_current_process(arch_regs_t *regs);
extern void set_task_priority(int pid, uint8_t priority);
extern int send_message(int to_pid, uint32_t payload);
extern int receive_message(uint32_t *sender_out, uint32_t *payload_out);
extern int schedule_kernel_timer(int ticks, int pid);

typedef struct {
    int readers;
    int writer_active;
    mutex_t mutex;
} rwlock_t;

void rwlock_init(rwlock_t *lock);
void rwlock_acquire_read(rwlock_t *lock, arch_regs_t *regs);
void rwlock_release_read(rwlock_t *lock);
void rwlock_acquire_write(rwlock_t *lock, arch_regs_t *regs);
void rwlock_release_write(rwlock_t *lock);

#endif
