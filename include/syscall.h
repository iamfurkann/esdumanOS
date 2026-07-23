#ifndef SYSCALL_H
#define SYSCALL_H

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

/** @brief Encrypt data using crypto subsystem */
#define SYSCALL_CRYPTO_ENCRYPT  30
/** @brief Decrypt data using crypto subsystem */
#define SYSCALL_CRYPTO_DECRYPT  31
/** @brief Generate a cryptographic key */
#define SYSCALL_CRYPTO_KEYGEN   32
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
#endif // SYSCALL_H