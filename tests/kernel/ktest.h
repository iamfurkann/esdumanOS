#ifndef KTEST_H
#define KTEST_H

#include "types.h"
#include "stdio.h"

extern int tests_passed;
extern int tests_failed;

static inline void serial_putchar(char c) {
    __asm__ volatile ( "outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3F8) );
}

static inline void serial_print(const char *str) {
    while (*str) serial_putchar(*str++);
}

#define KTEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            printk("  [PASS] "); printk(message); printk("\n"); \
            serial_print("  [PASS] "); serial_print(message); serial_print("\n"); \
            tests_passed++; \
        } else { \
            printk("  [FAIL] "); printk(message); printk("\n"); \
            serial_print("  [FAIL] "); serial_print(message); serial_print("\n"); \
            tests_failed++; \
        } \
    } while(0)

// Test Modülleri
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
#endif // KTEST_H