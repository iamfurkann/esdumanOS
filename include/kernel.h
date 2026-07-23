#ifndef KERNEL_H
#define KERNEL_H

/**
 * @brief OS Versioning Definitions
 * MAJOR : Major/architectural changes
 * MINOR : New large features
 * PATCH : Bug fixes, minor patches
 */
#define OS_VERSION_MAJOR    3
#define OS_VERSION_MINOR    4
#define OS_VERSION_PATCH    3

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
#define OS_VERSION_STR "  v" STRINGIFY(OS_VERSION_MAJOR) "." STRINGIFY(OS_VERSION_MINOR) "." STRINGIFY(OS_VERSION_PATCH) " "

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
 * @brief Switches execution context to user mode (Ring 3)
 * @param eip Instruction pointer for user mode
 * @param esp Stack pointer for user mode
 */
extern void switch_to_user_mode(uint32_t eip, uint32_t esp);

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
#endif //KERNEL_H