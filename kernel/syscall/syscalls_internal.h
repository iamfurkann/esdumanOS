#ifndef SYSCALLS_INTERNAL_H
#define SYSCALLS_INTERNAL_H

#include "types.h"
#include "registers.h"

// sys_utils.c
/** @brief Validates if a user-space pointer is safe to access */
int validate_user_pointer(const void *ptr, size_t size);
/** @brief Validates that a user-space range is present, user-accessible, and writable */
int validate_user_writable_pointer(const void *ptr, size_t size);
/** @brief Validates if a user-space string is safely null-terminated within max_len */
int validate_string_pointer(const char *str, size_t max_len);
/** @brief Validates if a file descriptor is valid for the current task */
int validate_fd(int fd);
/** @brief Resolves a VFS path to a directory ID and extracts the basename */
int vfs_resolve_path(const char *path, int start_dir_id, char *basename);
/** @brief Checks if the current task has permission to access a VFS entry */
int check_vfs_access(int entry_id, int needs_write);
/** @brief Prints a hex dump of memory at the specified address */
/**
 * @brief Renders a hex dump into a buffer.
 *
 * Printed to the screen until v0.9.2, which meant the dump could not be piped or
 * redirected. The caller writes it now.
 *
 * @param buf Destination.
 * @param cap Capacity, terminator included.
 * @param addr Address to dump.
 * @param length Bytes to dump.
 * @return Characters written, excluding the terminator.
 */
int format_hexdump(char *buf, uint32_t cap, uint32_t addr, int length);
/** @brief Computes a salted djb2 hash of a string */
uint32_t hash_djb2_salted(const char *str);


// sys_process.c
/** @brief Syscall handler for exiting the current process */
void sys_exit(arch_regs_t *regs);
/** @brief Syscall handler for executing a new process */
void sys_exec(arch_regs_t *regs);
/** @brief Syscall handler for duplicating the calling process */
void sys_fork(arch_regs_t *regs);
/** @brief Syscall handler for waiting on a child and collecting its status */
void sys_wait(arch_regs_t *regs);
/** @brief Syscall handler for setting process priority */
void sys_set_priority(arch_regs_t *regs);
/** @brief Syscall handler for yielding the CPU */
void sys_yield(arch_regs_t *regs);
/** @brief Syscall handler for getting the current user ID */
void sys_getuid(arch_regs_t *regs);
/** @brief Syscall handler for placing a process in a process group */
void sys_setpgid(arch_regs_t *regs);
/** @brief Syscall handler for handing the terminal to a process group */
void sys_tcsetpgrp(arch_regs_t *regs);
/** @brief Syscall handler for reading a process's group */
void sys_getpgid(arch_regs_t *regs);
/** @brief Syscall handler for getting the current process ID */
void sys_getpid(arch_regs_t *regs);
/** @brief Syscall handler for blocking the caller for a number of milliseconds */
void sys_sleep(arch_regs_t *regs);
/** @brief Syscall handler for reading the current wall-clock time */
void sys_time(arch_regs_t *regs);
/** @brief Syscall handler for setting the wall clock */
void sys_settime(arch_regs_t *regs);
/** @brief Syscall handler for getting command line arguments */
void sys_get_args(arch_regs_t *regs);
/** @brief Syscall handler for dumping the task's stack */
void sys_stack_dump(arch_regs_t *regs);
/** @brief Syscall handler for displaying memory information */
void sys_meminfo(arch_regs_t *regs);
/** @brief Syscall handler for testing memory allocation */
void sys_test_malloc(arch_regs_t *regs);
/** @brief Syscall handler for displaying a memory hex dump */
void sys_hexdump(arch_regs_t *regs);

// sys_fs.c
/** @brief Syscall handler for reading from a file descriptor */
void sys_read(arch_regs_t *regs);
/** @brief Syscall handler asking whether a read would block */
void sys_poll(arch_regs_t *regs);
/** @brief Syscall handler for writing to a file descriptor */
void sys_write(arch_regs_t *regs);
/** @brief Syscall handler for creating a pipe */
void sys_pipe(arch_regs_t *regs);
/** @brief Syscall handler for duplicating a file descriptor */
void sys_dup2(arch_regs_t *regs);
/** @brief Syscall handler for closing a file descriptor */
void sys_close(arch_regs_t *regs);
/** @brief Syscall handler for opening a file */
void sys_open(arch_regs_t *regs);
/** @brief Syscall handler for creating a new file */
void sys_create_file(arch_regs_t *regs);
/** @brief Syscall handler for removing a file */
void sys_rm_file(arch_regs_t *regs);
/** @brief Syscall handler for moving or renaming a file */
void sys_mv_file(arch_regs_t *regs);
/** @brief Syscall handler for creating a directory */
void sys_mkdir(arch_regs_t *regs);
/** @brief Syscall handler for listing directory contents */
void sys_ls_dir(arch_regs_t *regs);
/** @brief Syscall handler for getting a directory ID */
void sys_get_dir_id(arch_regs_t *regs);
/** @brief Syscall handler for listing all files */
void sys_list_files(arch_regs_t *regs);
/** @brief Syscall handler for reading an open file's stored, undecrypted bytes */
void sys_read_raw(arch_regs_t *regs);
/** @brief Syscall handler for reporting a path's metadata */
void sys_stat(arch_regs_t *regs);
/** @brief Syscall handler for reporting an open descriptor's metadata */
void sys_fstat(arch_regs_t *regs);
/** @brief Syscall handler for repositioning an open file's read offset */
void sys_lseek(arch_regs_t *regs);
/** @brief Syscall handler for changing a file's permission bits */
void sys_chmod(arch_regs_t *regs);
/** @brief Syscall handler for changing a file's owner and group */
void sys_chown(arch_regs_t *regs);

// sys_ipc.c
/** @brief Syscall handler for sending an IPC message */
void sys_ipc_send(arch_regs_t *regs);
/** @brief Syscall handler for receiving an IPC message */
void sys_ipc_receive(arch_regs_t *regs);
/** @brief Syscall handler for setting an alarm */
void sys_alarm(arch_regs_t *regs);
/** @brief Syscall handler for registering a signal */
void sys_signal_reg(arch_regs_t *regs);
/** @brief Syscall handler for killing a process */
void sys_kill(arch_regs_t *regs);

// sys_sec.c
/** @brief Syscall handler for authenticating a user */
void sys_auth(arch_regs_t *regs);
/** @brief Syscall handler for setting the user ID */
void sys_setuid_call(arch_regs_t *regs);
/** @brief Syscall handler for setting keyboard layout */
void sys_set_layout(arch_regs_t *regs);
/** @brief Syscall handler for setting the system security level */
void sys_set_sec_level(arch_regs_t *regs);
/** @brief Syscall handler for locking down the system */
void sys_lockdown(arch_regs_t *regs);
/** @brief Syscall handler for triggering a kernel panic */
void sys_panic(arch_regs_t *regs);
/** @brief Syscall handler for rebooting the system */
void sys_reboot(arch_regs_t *regs);
/** @brief Syscall handler for halting the system */
void sys_halt(arch_regs_t *regs);
/** @brief Syscall handler for displaying diagnostic messages */
void sys_dmesg(arch_regs_t *regs);
/** @brief Syscall handler for inspecting and controlling the kernel log */
void sys_klog_ctl(arch_regs_t *regs);
/** @brief Syscall handler for flushing the block cache to disk */
void sys_sync(arch_regs_t *regs);
/** @brief Syscall handler for changing the working directory */
void sys_chdir(arch_regs_t *regs);
/** @brief Syscall handler for reading the working directory */
void sys_getcwd(arch_regs_t *regs);


// sys_mem.c
/** @brief Syscall handler for moving the program break; returns the resulting break */
void sys_brk(arch_regs_t *regs);
/** @brief Syscall handler for mapping anonymous zeroed pages; returns the address */
void sys_mmap(arch_regs_t *regs);
/** @brief Syscall handler for releasing pages obtained from mmap */
void sys_munmap(arch_regs_t *regs);

#endif // SYSCALLS_INTERNAL_H
