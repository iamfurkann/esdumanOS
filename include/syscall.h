#ifndef SYSCALL_H
#define SYSCALL_H

#define SYSCALL_EXIT            1
#define SYSCALL_EXEC            5
#define SYSCALL_SET_PRIORITY    7
#define SYSCALL_YIELD           99

#define SYSCALL_READ            3
#define SYSCALL_WRITE           4
#define SYSCALL_CLEAR_SCREEN    10
#define SYSCALL_SET_LAYOUT      12

// ==========================================================
// VFS (Virtual File System) & Dizin Yönetimi Syscall'ları
// ==========================================================
#define SYSCALL_CREATE_FILE     8
#define SYSCALL_LIST_FILES      9
#define SYSCALL_CAT_FILE        11
#define SYSCALL_RM_FILE         22
#define SYSCALL_MV_FILE         23

#define SYSCALL_MKDIR           26  
#define SYSCALL_LS_DIR          28  
#define SYSCALL_GET_DIR_ID      29  

#define SYSCALL_STACK_DUMP      14
#define SYSCALL_MEMINFO         15
#define SYSCALL_TEST_MALLOC     16
#define SYSCALL_HEXDUMP         17

#define SYSCALL_IPC_SEND        2
#define SYSCALL_IPC_RECEIVE     6
#define SYSCALL_ALARM           18
#define SYSCALL_SIGNAL_REG      24
#define SYSCALL_KILL            25
#define SYSCALL_SIGRETURN       27

#define SYSCALL_LOCKDOWN        13
#define SYSCALL_PANIC           19
#define SYSCALL_REBOOT          20
#define SYSCALL_HALT            21

#define SYSCALL_CRYPTO_ENCRYPT  30
#define SYSCALL_CRYPTO_DECRYPT  31
#define SYSCALL_CRYPTO_KEYGEN   32
#define SYSCALL_SET_SEC_LEVEL   33
#define SYSCALL_CAT_RAW         34
#define SYSCALL_SETUID          35
#define SYSCALL_PIPE            36
#define SYSCALL_DUP2            37
#define SYSCALL_CLOSE           38
#define SYSCALL_DMESG           39
#define SYSCALL_OPEN            40
#define SYSCALL_AUTH            41
#define SYSCALL_GET_ARGS        42
#endif // SYSCALL_H