#ifndef SYSCALL_H
#define SYSCALL_H

/**
 * @brief Encoded length of the syscall trap instruction (int 0x80 is CD 80).
 *
 * The processor pushes the address *after* the trap, so a syscall that has to
 * block must resume this many bytes earlier to run again. This is the only
 * place that assumption lives: syscall_handler() converts it into a recorded
 * entry address once, and every blocking site uses that recorded value instead
 * of subtracting from EIP itself.
 */
#define SYSCALL_INSN_LEN 2

/** @brief Terminate current process */
#define SYSCALL_EXIT            1
/**
 * @brief Start a program; ebx is the path, ecx the mode, edx the argument string.
 *
 * A zero ecx is the form this call has always had: the caller blocks until the
 * program finishes and receives its exit status. init uses it, because init has
 * nothing else to do while the shell runs.
 *
 * EXEC_NOWAIT returns the child's pid immediately instead. A shell needs that to
 * own the program it just started the way it owns a pipeline - put it in a group
 * of its own, hand it the terminal, and wait for it with wait(), which can
 * report a child that stopped as well as one that exited. With the blocking form
 * there is no pid to name, so a stopped foreground command could not be brought
 * back.
 */
#define SYSCALL_EXEC            5

/** @brief exec() mode: block until the program finishes and return its status. */
#define EXEC_WAIT               0
/** @brief exec() mode: return the new process's pid and leave it running. */
#define EXEC_NOWAIT             1
/** @brief Set process scheduling priority */
#define SYSCALL_SET_PRIORITY    7
/** @brief Yield CPU to another process */
#define SYSCALL_YIELD           99

/** @brief Read from a file descriptor */
#define SYSCALL_READ            3
/** @brief Write to a file descriptor */
#define SYSCALL_WRITE           4
/** @brief Clear the console screen */
#define SYSCALL_CLEAR_SCREEN    10
/** @brief Set keyboard layout */
#define SYSCALL_SET_LAYOUT      12

// ==========================================================
// VFS (Virtual File System) & Directory Management Syscalls
// ==========================================================
/** @brief Create a new file */
#define SYSCALL_CREATE_FILE     8
/** @brief List files in current directory */
#define SYSCALL_LIST_FILES      9
/** @brief Output file contents to console */
#define SYSCALL_CAT_FILE        11
/** @brief Remove a file */
#define SYSCALL_RM_FILE         22
/** @brief Move or rename a file */
#define SYSCALL_MV_FILE         23

/** @brief Create a new directory */
#define SYSCALL_MKDIR           26  
/** @brief List directory contents */
#define SYSCALL_LS_DIR          28  
/** @brief Get directory ID */
#define SYSCALL_GET_DIR_ID      29  

/** @brief Dump current task stack */
#define SYSCALL_STACK_DUMP      14
/** @brief Display memory information */
#define SYSCALL_MEMINFO         15
/** @brief Test dynamic memory allocation */
#define SYSCALL_TEST_MALLOC     16
/** @brief Display hex dump of memory */
#define SYSCALL_HEXDUMP         17

/** @brief Send IPC message */
#define SYSCALL_IPC_SEND        2
/** @brief Receive IPC message */
#define SYSCALL_IPC_RECEIVE     6
/** @brief Set an alarm signal */
#define SYSCALL_ALARM           18
/** @brief Register a signal handler */
#define SYSCALL_SIGNAL_REG      24
/**
 * @brief Send a signal; ebx is the pid, ecx the signal.
 *
 * A negative pid names the process group -pid and signals every member, which is
 * how the shell continues a stopped job: a job is a group, and the process the
 * shell forked is rarely the only member of it - that process may itself have
 * started the program the user is actually looking at, and both were stopped.
 */
#define SYSCALL_KILL            25
/** @brief Return from a signal handler */
#define SYSCALL_SIGRETURN       27

/** @brief Enable system lockdown mode */
#define SYSCALL_LOCKDOWN        13
/** @brief Trigger kernel panic (testing) */
#define SYSCALL_PANIC           19
/** @brief Reboot the system */
#define SYSCALL_REBOOT          20
/** @brief Halt the system */
#define SYSCALL_HALT            21

// Syscall numbers 30-32 are reserved for future crypto API.
/** @brief Set system security level */
#define SYSCALL_SET_SEC_LEVEL   33
/** @brief Output raw file contents bypassing text formatting */
#define SYSCALL_CAT_RAW         34
/** @brief Set user ID of current process */
#define SYSCALL_SETUID          35
/** @brief Create an IPC pipe */
#define SYSCALL_PIPE            36
/** @brief Duplicate a file descriptor */
#define SYSCALL_DUP2            37
/** @brief Close a file descriptor */
#define SYSCALL_CLOSE           38
/** @brief Display kernel diagnostic messages */
#define SYSCALL_DMESG           39
/** @brief Open a file */
#define SYSCALL_OPEN            40
/** @brief Authenticate user */
#define SYSCALL_AUTH            41
/** @brief Get process arguments */
#define SYSCALL_GET_ARGS        42
/** @brief Get user ID of current process */
#define SYSCALL_GETUID          43
/** @brief Read directory entries into user buffer */
#define SYSCALL_READDIR         44
/** @brief Write every dirty block-cache sector out to disk */
#define SYSCALL_SYNC            45
/** @brief Change the calling process's working directory */
#define SYSCALL_CHDIR           46
/** @brief Write the calling process's working directory into a user buffer */
#define SYSCALL_GETCWD          47
/** @brief Report a path's metadata into a user esd_stat_t */
#define SYSCALL_STAT            48
/** @brief Report an open descriptor's metadata into a user esd_stat_t */
#define SYSCALL_FSTAT           49
/** @brief Reposition the read/write offset of an open file */
#define SYSCALL_LSEEK           50
/** @brief Return the calling process's pid */
#define SYSCALL_GETPID          51
/** @brief Block the calling process for a number of milliseconds */
#define SYSCALL_SLEEP           52
/** @brief Duplicate the calling process; returns 0 in the child, its pid in the parent */
#define SYSCALL_FORK            53
/**
 * @brief Collect a child that has something to report; ebx is an int*, ecx flags.
 *
 * eax comes back as the pid that reported, 0 when children exist but none has
 * anything to say and WNOHANG was asked for, or E_CHILD when there is nothing
 * left to wait for at all. The status is written through ebx, which may be NULL
 * when the caller only wants the pid.
 */
#define SYSCALL_WAIT            54

/** @brief wait() flag: report that nothing is ready rather than blocking. */
#define WNOHANG                 1
/**
 * @brief wait() flag: report a child that stopped, not only one that exited.
 *
 * Without it a stopped child is invisible here, which is the right default: a
 * caller that does not know about job control would otherwise be handed a pid
 * whose process is still very much alive and would treat it as finished.
 */
#define WUNTRACED               2

/**
 * @brief Status bit meaning "this child stopped", OR'd with the signal number.
 *
 * Above the eight bits an exit status occupies - sys_exit() masks its argument
 * to 0-255 and a signalled death is reported as 128 plus the signal - so there
 * is no value a caller could confuse with either.
 */
#define WSTATUS_STOPPED         0x100
/** @brief Fill an esd_time_t with the current time; non-zero ecx asks for UTC */
#define SYSCALL_TIME            55
/**
 * @brief Move the program break; returns the resulting break.
 *
 * Raw brk semantics, as the kernel call rather than the libc wrapper: the
 * requested break goes in ebx and the *resulting* break comes back in eax. A
 * request that cannot be satisfied returns the break unchanged rather than an
 * errno, so the caller finds out by comparing the answer with what it asked
 * for - and `brk(0)`, which can never be granted, is therefore how you read the
 * current break without moving it.
 */
#define SYSCALL_BRK             56
/**
 * @brief Map anonymous, private, zero-filled pages; returns the address.
 *
 * ebx is the length in bytes, rounded up to a page. ecx is reserved and must be
 * zero. Returns 0xFFFFFFFF when the mapping could not be made. The kernel picks
 * the address; there is no MAP_FIXED and no file backing.
 */
#define SYSCALL_MMAP            57
/**
 * @brief Release pages obtained from mmap; returns 0 or a negative errno.
 *
 * ebx is the address and ecx the length. Both are rounded to page boundaries,
 * and the range must lie inside the region mmap hands out.
 */
#define SYSCALL_MUNMAP          58
/**
 * @brief Inspect and control the kernel log; ebx selects the operation.
 *
 * The read side of the log has been reachable since v0.4.x through DMESG. What
 * has never been reachable is everything around it: the severity threshold has
 * sat at INFO since boot with no way to move it, so every DEBUG record the
 * kernel composes has been discarded unseen, and there has been no way to clear
 * the ring or to ask how much of it was lost when it wrapped.
 */
#define SYSCALL_KLOG_CTL        59

/** @brief Discard every held record. Root only. */
#define KLOG_CTL_CLEAR          0
/** @brief Set the severity threshold from ecx. Root only. */
#define KLOG_CTL_SET_LEVEL      1
/** @brief Return the current severity threshold. */
#define KLOG_CTL_GET_LEVEL      2
/** @brief Return how many records are currently held. */
#define KLOG_CTL_HELD           3
/** @brief Return how many records the ring has overwritten since boot. */
#define KLOG_CTL_DROPPED        4

/**
 * @brief Place a process in a process group; ebx is the pid, ecx the group.
 *
 * A pid of 0 means the caller, and a group of 0 means "found a new group named
 * by that pid". A caller may place itself or one of its children and nothing
 * else, which is what stops an unrelated program moving a shell's job out from
 * under it.
 *
 * Both the shell and the child it just forked call this with the same arguments,
 * which is the ordinary Unix answer to a race neither of them can win: whichever
 * runs first, the group is set before the child can be signalled.
 */
#define SYSCALL_SETPGID         60
/**
 * @brief Hand the terminal to a process group; ebx is the group.
 *
 * The foreground group is what Ctrl-C interrupts. A caller may only name its own
 * group or a group containing one of its children - a background job cannot take
 * the terminal away from the shell that started it.
 */
#define SYSCALL_TCSETPGRP       61
/**
 * @brief Read a process's group; ebx is the pid, or 0 for the caller.
 */
#define SYSCALL_GETPGID         62

/**
 * @brief Ask whether a read would block; ebx is the descriptor.
 *
 * Returns 1 when a read would return straight away - because there is data, or
 * because there is an end of file to report - 0 when it would block, and a
 * negative errno for a descriptor that is not open.
 *
 * It exists for one problem with no other honest answer. The keyboard sends the
 * escape sequences a terminal sends, so `ESC` now arrives both as the Escape key
 * and as the first byte of a sequence, and nothing in this system has a timer
 * fine enough to tell them apart by how long the next byte takes. Asking whether
 * a second byte is already waiting does tell them apart, and asking is the only
 * thing a program can do that does not involve consuming a byte it may have to
 * give back.
 */
#define SYSCALL_POLL            63

/**
 * @brief Change a file's permission bits.
 *
 * ebx is a path, ecx is the new mode. Only the owner and root may change one,
 * which is the same rule every Unix applies and for the same reason: a mode a
 * stranger can rewrite is not a permission, it is a suggestion.
 *
 * The bits above the permission mask are ignored rather than refused. There are
 * no set-user-id or sticky bits here yet, and a caller that sets one is asking
 * for something this system has no meaning for - silently keeping only what
 * exists is kinder than an error nobody can act on.
 */
#define SYSCALL_CHMOD           64

/**
 * @brief Change a file's owner and group.
 *
 * ebx is a path, ecx is the new uid, edx the new gid. **Root only**, and that is
 * stricter than it strictly has to be: handing a file to somebody else is how a
 * user escapes their own quota on systems that have one, and this system does
 * not have one to escape yet. The restriction is easier to keep than to add back.
 */
#define SYSCALL_CHOWN           65

// ==========================================================
// Test-build-only syscalls (>= 200)
//
// Present in every build so user-space test payloads compile against a single
// header, but only serviced when the kernel was linked with the test modules
// AND booted with kernel_pass=selftest. Production kernels answer-ENOSYS.
// ==========================================================
/** @brief Report one Ring 3 self-test result to the kernel test framework */
#define SYSCALL_KTEST_REPORT    200
#endif // SYSCALL_H