#ifndef SYSCALLS_INTERNAL_H
#define SYSCALLS_INTERNAL_H

#include "types.h"
#include "registers.h"

//sys_utils.c
int validate_user_pointer(const void *ptr, size_t size);
int validate_string_pointer(const char *str, size_t max_len);
int validate_fd(int fd);
int vfs_resolve_path(const char *path, int start_dir_id, char *basename);
int check_vfs_access(int entry_id, int needs_write);
void print_hexdump(uint32_t addr, int lenght);
uint32_t hash_djb2_salted(const char *str);


// sys_process.c
void sys_exit(arch_regs_t *regs);
void sys_exec(arch_regs_t *regs);
void sys_set_priority(arch_regs_t *regs);
void sys_yield(arch_regs_t *regs);
void sys_getuid(arch_regs_t *regs);
void sys_get_args(arch_regs_t *regs);
void sys_stack_dump(arch_regs_t *regs);
void sys_meminfo(arch_regs_t *regs);
void sys_test_malloc(arch_regs_t *regs);
void sys_hexdump(arch_regs_t *regs);

// sys_fs.c
void sys_read(arch_regs_t *regs);
void sys_write(arch_regs_t *regs);
void sys_pipe(arch_regs_t *regs);
void sys_dup2(arch_regs_t *regs);
void sys_close(arch_regs_t *regs);
void sys_open(arch_regs_t *regs);
void sys_create_file(arch_regs_t *regs);
void sys_rm_file(arch_regs_t *regs);
void sys_mv_file(arch_regs_t *regs);
void sys_mkdir(arch_regs_t *regs);
void sys_ls_dir(arch_regs_t *regs);
void sys_get_dir_id(arch_regs_t *regs);
void sys_list_files(arch_regs_t *regs);
void sys_cat_raw(arch_regs_t *regs);
void sys_cat_file(arch_regs_t *regs);

// sys_ipc.c
void sys_ipc_send(arch_regs_t *regs);
void sys_ipc_receive(arch_regs_t *regs);
void sys_alarm(arch_regs_t *regs);
void sys_signal_reg(arch_regs_t *regs);
void sys_kill(arch_regs_t *regs);

// sys_sec.c
void sys_auth(arch_regs_t *regs);
void sys_setuid_call(arch_regs_t *regs);
void sys_set_layout(arch_regs_t *regs);
void sys_set_sec_level(arch_regs_t *regs);
void sys_lockdown(arch_regs_t *regs);
void sys_panic(arch_regs_t *regs);
void sys_reboot(arch_regs_t *regs);
void sys_halt(arch_regs_t *regs);
void sys_dmesg(arch_regs_t *regs);

#endif // SYSCALLS_INTERNAL_H