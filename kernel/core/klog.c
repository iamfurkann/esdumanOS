/*
 * File: klog.c
 * Purpose: Kernel logging system with multi-level support.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "klog.h"
#include "kernel.h"
#include "stdio.h"
#include "tty.h"

int current_log_level = LOG_LEVEL_INFO;

/**
 * @brief klog_itoa
 * @param n
 * @param buf
 */
static void klog_itoa(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0, is_neg = 0;
    if (n < 0) { is_neg = 1; n = -n; }
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    if (is_neg) buf[i++] = '-';
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

/**
 * @brief klog_itoa_hex
 * @param n
 * @param buf
 */
static void klog_itoa_hex(uint32_t n, char *buf) {
    static const char hex_chars[] = "0123456789ABCDEF";
    if (n == 0) { 
        buf[0] = '0'; 
        buf[1] = '\0'; 
        return; 
    }
    
    char temp[9];
    int i = 0;

    while (n > 0) {
        temp[i++] = hex_chars[n & 0xF];
        n >>= 4;
    }

    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    
    buf[j] = '\0';
}

static const char* level_strings[] = {
    "[DEBUG]",
    "[INFO ]",
    "[WARN ]",
    "[ERROR]",
    "[FATAL]"
};

static uint8_t level_colors[] = {
    VGA_COLOR_LIGHT_GREY, // DEBUG
    VGA_COLOR_LIGHT_CYAN, // INFO
    VGA_COLOR_BROWN,      // WARN
    VGA_COLOR_LIGHT_RED,  // ERROR
    VGA_COLOR_WHITE       // FATAL/CRITICAL
};

static uint8_t level_bg_colors[] = {
    VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_BLACK, VGA_COLOR_RED
};

/**
 * @brief klog
 * @param level
 * @param module
 * @param message
 */
void klog(int level, const char *module, const char *message) {
    if (level < current_log_level) return;
    if (level < 0) level = 0;
    if (level > 4) level = 4;

    terminal_setcolor(level_colors[level], level_bg_colors[level]);
    printk(level_strings[level]);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    printk(" ");
    printk(module);
    printk(": ");
    printk(message);
    printk("\n");
}

/**
 * @brief klog_int
 * @param level
 * @param module
 * @param message
 * @param val
 */
void klog_int(int level, const char *module, const char *message, int val) {
    if (level < current_log_level) return;
    char num_str[16];
    klog_itoa(val, num_str);
    
    klog(level, module, message);
    printk(" -> Value: "); printk(num_str); printk("\n");
}

#define KLOG_BUF_SIZE 8192
static char dmesg_buffer[KLOG_BUF_SIZE];
static int dmesg_idx = 0;

/**
 * @brief klog_write_char
 * @param c
 */
void klog_write_char(char c) {
    if (dmesg_idx < KLOG_BUF_SIZE - 1) {
        dmesg_buffer[dmesg_idx++] = c;
        dmesg_buffer[dmesg_idx] = '\0';
    }
}

/**
 * @brief dump_klog
 */
void dump_klog(void) {
for(int i = 0; i < dmesg_idx; i++) {
        terminal_putchar(dmesg_buffer[i]);
    }
}

/**
 * @brief Copies a slice of the log into a caller-supplied buffer.
 *
 * dump_klog() writes to the screen with terminal_putchar(), so its output never
 * reaches the calling process's descriptor 1 - "dmesg | head" fed an empty pipe
 * and "dmesg > file" wrote nothing. Handing the bytes back instead lets the
 * caller write them itself, through whatever its descriptor 1 happens to be.
 *
 * Handing them back rather than writing them from in here is the whole point.
 * A write into a full pipe blocks, and a blocked syscall resumes by re-running
 * from the int 0x80 - so a kernel-side dump that blocked halfway would start
 * over and emit everything twice. A caller reading a slice at a time keeps the
 * position in its own loop, and each write it issues is a syscall of its own
 * that can block and restart harmlessly.
 *
 * @param dst    Kernel buffer receiving the bytes.
 * @param max    Capacity of dst.
 * @param offset Byte position in the log to start from.
 * @return Bytes copied; 0 once offset reaches the end of the log.
 */
int klog_read(char *dst, int max, int offset) {
    if (dst == 0 || max <= 0 || offset < 0 || offset >= dmesg_idx) return 0;

    int n = dmesg_idx - offset;
    if (n > max) n = max;

    for (int i = 0; i < n; i++) dst[i] = dmesg_buffer[offset + i];
    return n;
}

/**
 * @brief klog_hex
 * @param level
 * @param module
 * @param message
 * @param val
 */
void klog_hex(int level, const char *module, const char *message, uint32_t val) {
    if (level < current_log_level) return;
    
    char hex_str[16];
    klog_itoa_hex(val, hex_str);
    
    klog(level, module, message);
    printk(" -> Hex: 0x"); printk(hex_str); printk("\n");
}