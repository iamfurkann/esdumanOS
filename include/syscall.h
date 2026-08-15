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
/** @brief Execute a new process */
#define SYSCALL_EXEC            5
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
/** @brief Send a signal to a process */
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
/** @brief Wait for a child to finish; returns its exit status */
#define SYSCALL_WAIT            54

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