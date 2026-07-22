#include "klog.h"
#include "kernel.h"
#include "stdio.h"
#include "serial.h"

int current_log_level = LOG_LEVEL_INFO;

void ft_itoa_hex(uint32_t n, char *buf) {
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

void klog_int(int level, const char *module, const char *message, int val) {
    if (level < current_log_level) return;
    char num_str[16];
    extern void ft_itoa(int n, char *buf);
    ft_itoa(val, num_str);
    
    klog(level, module, message);
    printk(" -> Value: "); printk(num_str); printk("\n");
}

#define KLOG_BUF_SIZE 8192
static char dmesg_buffer[KLOG_BUF_SIZE];
static int dmesg_idx = 0;

void klog_write_char(char c) {
    if (dmesg_idx < KLOG_BUF_SIZE - 1) {
        dmesg_buffer[dmesg_idx++] = c;
        dmesg_buffer[dmesg_idx] = '\0';
    }
}

void dump_klog(void) {
    extern void terminal_putchar(char);
    for(int i = 0; i < dmesg_idx; i++) {
        terminal_putchar(dmesg_buffer[i]);
    }
}

void klog_hex(int level, const char *module, const char *message, uint32_t val) {
    if (level < current_log_level) return;
    
    char hex_str[16];
    ft_itoa_hex(val, hex_str);
    
    klog(level, module, message);
    printk(" -> Hex: 0x"); printk(hex_str); printk("\n");
}