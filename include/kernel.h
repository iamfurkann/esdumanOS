#ifndef KERNEL_H
#define KERNEL_H

/**
 * @brief OS Versioning Definitions
 * MAJOR : Major/architectural changes
 * MINOR : New large features
 * PATCH : Bug fixes, minor patches
 */
#define OS_VERSION_MAJOR    0
#define OS_VERSION_MINOR    4
#define OS_VERSION_PATCH    2

/**
 * @brief Pre-release qualifier, or "" for a final release.
 */
#define OS_VERSION_PRE      "-alpha"

/**
 * @brief Helper macros for stringifying version numbers
 */
#define STRINGIFY_HELPER(x) #x

/**
 * @brief Macro to stringify a value
 */
#define STRINGIFY(x) STRINGIFY_HELPER(x)

/**
 * @brief Formatted OS version string
 */
#define OS_VERSION_STR "  v" STRINGIFY(OS_VERSION_MAJOR) "." STRINGIFY(OS_VERSION_MINOR) "." STRINGIFY(OS_VERSION_PATCH) OS_VERSION_PRE " "

#include "types.h"
#include "tty.h"
#include "stdio.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "paging.h"
#include "kheap.h"
#include "io.h"
#include "rtc.h"
#include "signal.h"
#include "ata.h"
#include "fs.h"
#include "elf.h"
#include "process.h"
#include "errno.h"
#include "isr.h"
#include "security.h"
#include "devfs.h"
#include "serial.h"
#include "klog.h"
#include "crypto.h"

/**
 * @brief Spinlock structure for atomic synchronization
 */
typedef struct {
    volatile uint32_t locked;
} spinlock_t;

/**
 * @brief Releases an acquired spinlock
 * @param lock Pointer to the spinlock
 */
void spinlock_release(spinlock_t *lock);

/**
 * @brief Acquires a spinlock, spinning until it becomes available
 * @param lock Pointer to the spinlock
 */
void spinlock_acquire(spinlock_t *lock);

/**
 * @brief Initializes a spinlock to an unlocked state
 * @param lock Pointer to the spinlock
 */
void spinlock_init(spinlock_t *lock);

/**
 * @brief Array containing the initialization ELF binary
 */
extern unsigned char init_elf[];

/**
 * @brief Length of the initialization ELF binary
 */
extern unsigned int init_elf_len;

/**
 * @brief Writes a single character to the kernel log
 * @param c Character to write
 */
void klog_write_char(char c);

/**
 * @brief Dumps the kernel log to the console or serial output
 */
void dump_klog(void);

// --- Added by Refactor Script ---
extern void init_timer(uint32_t freq);
extern void init_kernel_timers(void);
extern void __attribute__((weak)) run_all_selftests(void);
extern int is_test_mode;
extern uint32_t __bss_end;
extern uint32_t _bss_start;
extern uint32_t _bss_end;

extern int kernel_panic_mode;
extern void init_stack_protect(void);
extern void register_kernel_timer(int sig_num, void (*handler)(void));
extern const uint32_t sh_elf_len;
extern const uint32_t hello_elf_len;
extern const uint32_t clear_elf_len;
extern const uint32_t echo_elf_len;
extern unsigned char touch_elf[]; extern unsigned int touch_elf_len;
extern unsigned char rm_elf[]; extern unsigned int rm_elf_len;
extern unsigned char mv_elf[]; extern unsigned int mv_elf_len;
extern unsigned char cp_elf[]; extern unsigned int cp_elf_len;
extern unsigned char free_elf[]; extern unsigned int free_elf_len;
extern unsigned char whoami_elf[]; extern unsigned int whoami_elf_len;
extern unsigned char kill_elf[]; extern unsigned int kill_elf_len;
extern unsigned char grep_elf[]; extern unsigned int grep_elf_len;
extern unsigned char head_elf[]; extern unsigned int head_elf_len;
extern unsigned char date_elf[]; extern unsigned int date_elf_len;
extern unsigned char stat_elf[]; extern unsigned int stat_elf_len;

#endif //KERNEL_H