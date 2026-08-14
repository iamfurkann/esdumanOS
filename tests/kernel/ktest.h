/*
 * File: ktest.h
 * Purpose: Header file defining test suite functions, assertions, and tracking for tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#ifndef KTEST_H
#define KTEST_H

#include "types.h"
#include "stdio.h"
#include "registers.h"

extern int tests_passed;
extern int tests_failed;

/*
 * SYSCALL_KTEST_REPORT protocol, shared with the Ring 3 payload in
 * tests/user/ktest_user.c. Keep the two sides in sync.
 */
#define KT_REPORT_FAIL 0
#define KT_REPORT_PASS 1
#define KT_REPORT_DONE 2

/*
 * Returns timer_get_ticks() to the caller instead of recording a result.
 *
 * Ring 3 has no clock of its own, so without this the sleep() assertions could
 * only check that the call returned - not that it waited. Measuring is the whole
 * point: a sleep that returns instantly and a sleep that works are
 * indistinguishable from user space otherwise.
 *
 * Test builds only, like the rest of syscall 200; production kernels answer
 * E_NOSYS and no timing surface is added to them.
 */
#define KT_REPORT_TICKS 3

/*
 * Returns pmm_get_free_memory() to the caller.
 *
 * The crash-teardown test needs to see whether a task that faults actually gets
 * reaped, and "the parent survived" alone does not prove that - the leak was the
 * other half of the defect. Ring 3 cannot read the physical allocator, so it
 * asks through the same test-only channel as the tick counter.
 */
#define KT_REPORT_FREEMEM 4

/**
 * @brief Terminates QEMU with the suite's verdict.
 * @param is_success Non-zero when every assertion passed.
 */
void qemu_shutdown(int is_success);

/**
 * @brief Prints the pass/fail tally and ends the run.
 *
 * Callable from the Ring 3 half of the suite, which finishes inside a syscall
 * rather than on the boot path.
 */
void ktest_finish(void);

/**
 * @brief SYSCALL_KTEST_REPORT handler; records one result reported from Ring 3.
 *
 * Linked only into test builds. syscall.c reaches it through a weak symbol.
 *
 * @param regs Saved register state of the calling user process.
 */
void sys_ktest_report(arch_regs_t *regs);

/*
 * serial_putchar() and serial_print() used to live here: a raw write to port
 * 0x3F8 and a loop over it, justified as a low-level path for "when higher-level
 * printing subsystems are unstable".
 *
 * They are gone because printk() already reaches COM1 and everything that called
 * them called printk() with the same text on the line above, so the whole suite
 * was emitting its output twice on the serial console. The names also shadowed
 * the real serial_print() in drivers/serial.c, which does wait for the transmit
 * register rather than writing blind.
 *
 * If a future test genuinely needs output from somewhere printk() cannot be
 * trusted, include serial.h and call the driver's version.
 */

/**
 * @brief Converts an integer to a null-terminated string representation.
 *
 * Provides a highly isolated integer-to-string conversion utility designed explicitly 
 * for the testing framework, preventing reliance on potentially untested libc implementations.
 *
 * @param n The integer to convert.
 * @param buf The pre-allocated character buffer to store the resulting string.
 * @expected The buffer safely contains the numerical string representation of 'n'.
 */
static inline void ktest_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char temp[16]; int i = 0;
    while(n > 0) { temp[i++] = (n % 10) + '0'; n /= 10; }
    int j = 0;
    while(i > 0) { buf[j++] = temp[--i]; }
    buf[j] = '\0';
}

/**
 * @brief Records one failure so the run can list them all together at the end.
 *
 * A full run prints several hundred lines, and hunting the [FAIL] markers out of
 * that scrollback is its own chore. Every failure is also collected here and
 * reprinted as a block just before the tally.
 *
 * @param message What was asserted.
 * @param file Source file, or 0 for results reported from Ring 3.
 * @param line Source line; ignored when file is 0.
 */
void ktest_record_failure(const char *message, const char *file, int line);

/*
 * Every result used to be written twice: once through printk() and once more
 * through a serial_print() alongside it. printk() already emits to COM1 as well
 * as the VGA console and the dmesg ring (see lib/stdio.c), so on a
 * "-serial stdio" run - which is how the suite is normally watched, and how CI
 * captures it - every [PASS] and [FAIL] appeared twice. A single failure showed
 * up as two identical numbered entries in the summary, which reads like two
 * separate defects.
 *
 * printk() is also the better of the two paths: serial_write_char() waits for
 * the UART to report the transmit register empty, while the raw port write the
 * test framework carried did not, so it could shift characters out from under an
 * unfinished byte.
 */
#define KTEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            printk("  [PASS] "); printk(message); printk("\n"); \
            tests_passed++; \
        } else { \
            printk("  [FAIL] "); printk(message); \
            printk(" ("); printk(__FILE__); printk(":"); \
            char line_str[16]; ktest_itoa(__LINE__, line_str); printk(line_str); printk(")\n"); \
            ktest_record_failure(message, __FILE__, __LINE__); \
            tests_failed++; \
        } \
    } while(0)

// Test Modules
void run_string_tests(void);
void run_memory_tests(void);
void run_pipe_tests(void);
void run_vfs_tests(void);
void run_security_tests(void);
void run_stress_tests(void);
void run_adversarial_tests(void);
void run_integration_tests(void);
void run_regression_tests(void);
void run_concurrency_tests(void);
void run_devfs_tests(void);
void run_passwd_tests(void);
void run_paging_tests(void);
void run_pmm_tests(void);
void run_lifecycle_tests(void);
void run_fault_tests(void);
void run_syscall_tests(void);
void run_process_tests(void);
void run_signal_tests(void);
void run_crypto_tests(void);
void run_entropy_tests(void);
void run_bcache_tests(void);
void run_elf_tests(void);

#endif // KTEST_H
