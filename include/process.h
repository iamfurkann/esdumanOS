#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"
#include "registers.h"
#include "isr.h"
#define MAX_TASKS 16
#define MAX_MESSAGES 8
#define MAX_USER_SIGNALS 32

typedef enum { TASK_EMPTY, TASK_RUNNING, TASK_WAITING, TASK_DEAD } task_state_t;

typedef enum { 
    WAIT_NONE = 0, 
    WAIT_KBD = 1,   
    WAIT_IPC = 2,   
    WAIT_TIMER = 3, 
    WAIT_MUTEX = 4,
    WAIT_CHILD = 5
} wait_reason_t;

typedef struct {
    uint32_t sender_pid;
    uint32_t payload;
} message_t;

#define MAX_FD_PER_TASK 16

#define FD_TYPE_NONE    0
#define FD_TYPE_CONSOLE 1 // Ekran / Klavye
#define FD_TYPE_FILE    2 // VFS Dosyası
#define FD_TYPE_PIPE    3 // Boru Hattı
#define FD_TYPE_DEVICE  4 

typedef struct {
    uint8_t type;       // Dosya türü
    uint32_t ptr;       // pipe_t* veya vfs_file_t* bellek adresi
    uint32_t offset;    // Okuma/Yazma imleci
    uint8_t mode;       // O_RDONLY, O_WRONLY vb.
} file_descriptor_t;

typedef struct {
    int pid;
    int parent_pid;
    uint32_t uid;
    task_state_t state;
    wait_reason_t wait_reason;
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
    file_descriptor_t fd_table[MAX_FD_PER_TASK];

    char cmd_args[128];
} process_t;

extern process_t tasks[MAX_TASKS];
extern int current_task;
extern int multitasking_enabled;
extern int foreground_task;
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m, arch_regs_t *regs);
void mutex_unlock(mutex_t *m);
void register_user_signal(int sig_num, uint32_t handler_addr);
void send_user_signal(int target_pid, int sig_num);


int create_process(uint32_t eip, uint32_t esp, uint32_t cr3);
void schedule(arch_regs_t *regs);
void set_kernel_stack(uint32_t stack);
void switch_to_user_mode(uint32_t eip, uint32_t esp);
void start_first_task(void);
void init_multitasking(void);
void check_and_deliver_signals(arch_regs_t *regs);

#endif