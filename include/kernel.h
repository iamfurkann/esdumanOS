#ifndef KERNEL_H
#define KERNEL_H

/**
 * @brief OS Versioning Definitions
 * MAJOR : Major/architectural changes
 * MINOR : New large features
 * PATCH : Bug fixes, minor patches
 */
#define OS_VERSION_MAJOR    0
#define OS_VERSION_MINOR    7
#define OS_VERSION_PATCH    1

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

/**
 * @brief The version with no padding, for anything that is not a status bar.
 *
 * OS_VERSION_STR carries the two spaces the status bar wants around it, which
 * makes it unusable anywhere the version is data rather than decoration. This is
 * what /etc/os-release is written from, so the file and the banner cannot drift.
 */
#define OS_VERSION_PLAIN STRINGIFY(OS_VERSION_MAJOR) "." STRINGIFY(OS_VERSION_MINOR) "." STRINGIFY(OS_VERSION_PATCH) OS_VERSION_PRE

/**
 * @brief The label on the left of the status bar.
 *
 * One definition because there are two callers - the boot-time draw and the
 * per-second refresh - and they used to disagree: the first passed the version
 * string and the second a literal "esdumanOS", so the label changed as soon as
 * the clock first ticked and stayed changed. The name is what belongs there; the
 * version is in /etc/os-release, where a program can read it.
 */
#define OS_STATUS_LABEL "esdumanOS"

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

/*
 * klog_write_char() and dump_klog() were declared here, in a header that pulls
 * in twenty-two others. klog.h owns them - it is the header for the log, and
 * this one was reaching past it.
 */

// --- Added by Refactor Script ---
extern void init_timer(uint32_t freq);
extern void init_kernel_timers(void);
extern void __attribute__((weak)) run_all_selftests(void);
extern int is_test_mode;

/*
 * "__bss_end" was declared here and defined nowhere - the linker script provides
 * _bss_start and _bss_end, with one underscore. The two that do exist are used
 * only from boot.asm, which reads them as assembly symbols and does not go
 * through this header; they are kept because a C consumer is plausible.
 */
extern uint32_t _bss_start;
extern uint32_t _bss_end;

extern int kernel_panic_mode;
extern void init_stack_protect(void);
/*
 * register_kernel_timer() was also declared here, spelled with a bare
 * void(*)(void) where signal.h spells it signal_handler_t. The two agree - the
 * typedef is exactly that - but a second declaration of a timer function in a
 * second header is precisely the shape of the bug that once had
 * schedule_kernel_timer() declared twice with signatures that did NOT agree.
 * signal.h owns it.
 */
/*
 * These four were declared "const uint32_t" while the other eleven below are
 * "unsigned int" - and xxd -i, which generates every one of them, emits
 * "unsigned int NAME_len = N;". So the first four disagreed with their own
 * definitions in both qualification and spelling. Incompatible types across
 * translation units is undefined behaviour the linker cannot catch.
 */
extern unsigned int sh_elf_len;
extern unsigned int hello_elf_len;
extern unsigned int clear_elf_len;
extern unsigned int echo_elf_len;
extern unsigned char touch_elf[]; extern unsigned int touch_elf_len;
extern unsigned char rm_elf[]; extern unsigned int rm_elf_len;
extern unsigned char mv_elf[]; extern unsigned int mv_elf_len;
extern unsigned char cp_elf[]; extern unsigned int cp_elf_len;
extern unsigned char free_elf[]; extern unsigned int free_elf_len;
extern unsigned char whoami_elf[]; extern unsigned int whoami_elf_len;
extern unsigned char kill_elf[]; extern unsigned int kill_elf_len;
extern unsigned char grep_elf[]; extern unsigned int grep_elf_len;
extern unsigned char head_elf[]; extern unsigned int head_elf_len;
extern unsigned char wc_elf[]; extern unsigned int wc_elf_len;
extern unsigned char date_elf[]; extern unsigned int date_elf_len;
extern unsigned char stat_elf[]; extern unsigned int stat_elf_len;

#endif //KERNEL_H