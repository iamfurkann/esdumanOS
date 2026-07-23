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

extern int tests_passed;
extern int tests_failed;

/**
 * @brief Transmits a single character to the primary serial port.
 *
 * Bypasses standard output mechanisms to provide low-level debugging capabilities 
 * over the COM1 port (0x3F8). Critical for safely logging test results when higher-level 
 * printing subsystems are unstable.
 *
 * @param c The character to transmit.
 * @expected The character is written directly to the serial I/O port without relying on interrupts.
 */
static inline void serial_putchar(char c) {
    __asm__ volatile ( "outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3F8) );
}

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
 * @brief Transmits a full null-terminated string to the serial port.
 *
 * Iterates through a character string and sequentially pushes each byte to the serial 
 * interface using the primitive `serial_putchar` function.
 *
 * @param str A pointer to the null-terminated string to output.
 * @expected The entire string is emitted to the COM1 serial interface.
 */
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
            printk("  [FAIL] "); printk(message); \
            printk(" ("); printk(__FILE__); printk(":"); \
            char line_str[16]; ktest_itoa(__LINE__, line_str); printk(line_str); printk(")\n"); \
            serial_print("  [FAIL] "); serial_print(message); \
            serial_print(" ("); serial_print(__FILE__); serial_print(":"); \
            serial_print(line_str); serial_print(")\n"); \
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
void run_syscall_tests(void);
void run_process_tests(void);
void run_signal_tests(void);
void run_crypto_tests(void);
void run_bcache_tests(void);
#endif // KTEST_H