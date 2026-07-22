#include "types.h"
#include "arch.h"
#include "registers.h"
#include "stdio.h"
#include "syscall.h"
#include "syscalls_internal.h"

extern void terminal_initialize(void);
extern void restore_signal_context(arch_regs_t *regs);
extern void check_and_deliver_signals(arch_regs_t *regs);


void syscall_handler(arch_regs_t *regs) {
    asm volatile("sti");
    
    uint32_t syscall_num = regs->eax;

    switch (syscall_num) {
        //sys_process.c
        case SYSCALL_EXIT:         sys_exit(regs); break;
        case SYSCALL_EXEC:         sys_exec(regs); break;
        case SYSCALL_SET_PRIORITY: sys_set_priority(regs); break;
        case SYSCALL_YIELD:        sys_yield(regs); break;
        case SYSCALL_GETUID:       sys_getuid(regs); break;
        case SYSCALL_GET_ARGS:     sys_get_args(regs); break;
        case SYSCALL_STACK_DUMP:   sys_stack_dump(regs); break;
        case SYSCALL_MEMINFO:      sys_meminfo(regs); break;
        case SYSCALL_TEST_MALLOC:  sys_test_malloc(regs); break;
        case SYSCALL_HEXDUMP:      sys_hexdump(regs); break;

        //sys_fs.c
        case SYSCALL_READ:         sys_read(regs); break;
        case SYSCALL_WRITE:        sys_write(regs); break;
        case SYSCALL_PIPE:         sys_pipe(regs); break;
        case SYSCALL_DUP2:         sys_dup2(regs); break;
        case SYSCALL_CLOSE:        sys_close(regs); break;
        case SYSCALL_OPEN:         sys_open(regs); break;
        case SYSCALL_CREATE_FILE:  sys_create_file(regs); break;
        case SYSCALL_RM_FILE:      sys_rm_file(regs); break;
        case SYSCALL_MV_FILE:      sys_mv_file(regs); break;
        case SYSCALL_MKDIR:        sys_mkdir(regs); break;
        case SYSCALL_LS_DIR:       sys_ls_dir(regs); break;
        case SYSCALL_GET_DIR_ID:   sys_get_dir_id(regs); break;
        case SYSCALL_LIST_FILES:   sys_list_files(regs); break;
        case SYSCALL_CAT_RAW:      sys_cat_raw(regs); break;
        case SYSCALL_CAT_FILE:     sys_cat_file(regs); break;

        //sys_ipc.c
        case SYSCALL_IPC_SEND:     sys_ipc_send(regs); break;
        case SYSCALL_IPC_RECEIVE:  sys_ipc_receive(regs); break;
        case SYSCALL_ALARM:        sys_alarm(regs); break;
        case SYSCALL_SIGNAL_REG:   sys_signal_reg(regs); break;
        case SYSCALL_KILL:         sys_kill(regs); break;

        //sys_sec.c
        case SYSCALL_AUTH:         sys_auth(regs); break;
        case SYSCALL_SETUID:       sys_setuid_call(regs); break;
        case SYSCALL_SET_LAYOUT:   sys_set_layout(regs); break;
        case SYSCALL_SET_SEC_LEVEL:sys_set_sec_level(regs); break;
        case SYSCALL_LOCKDOWN:     sys_lockdown(regs); break;
        case SYSCALL_PANIC:        sys_panic(regs); break;
        case SYSCALL_REBOOT:       sys_reboot(regs); break;
        case SYSCALL_HALT:         sys_halt(regs); break;
        case SYSCALL_DMESG:        sys_dmesg(regs); break;

        case SYSCALL_CLEAR_SCREEN:
            terminal_initialize();
            regs->eax = 0;
            break;
            
        case SYSCALL_SIGRETURN:
            restore_signal_context(regs);
            break;

        default:
            printk("Bilinmeyen Syscall Numarasi: %d\n", syscall_num);
            regs->eax = -1;
            break;
    }
    check_and_deliver_signals(regs);
}