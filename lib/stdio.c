/*
 * File: stdio.c
 * Purpose: Standard I/O library functions (printk, kvsnprintf).
 *
 * This file is part of the esdumanOS test suite.
 */
#include "stdio.h"
#include "tty.h"
#include "process.h"
#include "kernel.h"
#include "serial.h"
mutex_t vga_mutex;
int vga_mutex_initialized = 0;
int kernel_panic_mode = 0;

// =========================================================================
// HELPER: BUFFER WRITER
// =========================================================================
/**
 * @brief Helper function to write a character to a buffer.
 * 
 * @param buf The buffer to write to.
 * @param offset Pointer to the current offset in the buffer.
 * @param max Maximum size of the buffer.
 * @param c The character to write.
 */
static void buf_putc(char *buf, uint32_t *offset, uint32_t max, char c) {
    if (*offset < max - 1) {
        buf[*offset] = c;
        (*offset)++;
    }
}

// =========================================================================
// CORE: UNIVERSAL NUMBER FORMATTER
// =========================================================================
/**
 * @brief Formats and prints a number into the buffer.
 * 
 * @param buf The buffer to write to.
 * @param offset Pointer to the current offset in the buffer.
 * @param max Maximum size of the buffer.
 * @param num The number to print.
 * @param base The base (radix) of the number.
 * @param is_signed Whether the number is signed.
 * @param width Minimum width for formatting.
 * @param pad_zero Whether to pad with zeros instead of spaces.
 * @param uppercase Whether to use uppercase characters for hex digits.
 * @param left_justify Whether to left-justify the output.
 */
static void print_number(char *buf, uint32_t *offset, uint32_t max,
                         unsigned long num, int base, int is_signed,
                         int width, int pad_zero, int uppercase, int left_justify) 
{
    char temp[64];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    int is_negative = 0;

    if (is_signed && (long)num < 0) {
        is_negative = 1;
        num = (unsigned long)(-(long)num);
    }

    if (num == 0) {
        temp[i++] = '0';
    } else {
        while (num != 0) {
            temp[i++] = digits[num % base];
            num /= base;
        }
    }

    int pad_len = width - i - is_negative;
    char pad_char = (pad_zero && !left_justify) ? '0' : ' ';

    if (pad_zero && is_negative && !left_justify) {
        buf_putc(buf, offset, max, '-');
        is_negative = 0; 
    }

    // Right-justify (Normal): Print spaces/zeros before the number
    if (!left_justify) {
        while (pad_len > 0) { buf_putc(buf, offset, max, pad_char); pad_len--; }
    }

    if (is_negative) buf_putc(buf, offset, max, '-');

    // Print the number in reverse
    while (i > 0) {
        i--;
        buf_putc(buf, offset, max, temp[i]);
    }

    // Left-justify (-): Print spaces after the number
    if (left_justify) {
        while (pad_len > 0) { buf_putc(buf, offset, max, ' '); pad_len--; }
    }
}

// =========================================================================
// LIBC STANDARD KVSNPRINTF (FULL FEATURED)
// =========================================================================
/**
 * @brief Formats a string and writes it to a buffer.
 * 
 * @param buf The buffer to write the formatted string to.
 * @param size Maximum size of the buffer.
 * @param format The format string.
 * @param args Variable arguments list.
 * @return The number of characters written, excluding the null terminator.
 */
int kvsnprintf(char *buf, uint32_t size, const char *format, va_list args) {
    uint32_t offset = 0;
    int i = 0;

    if (!buf || size == 0) return 0;

    while (format[i] && offset < size - 1) {
        if (format[i] == '%') {
            i++;
            int left_justify = 0;
            int pad_zero = 0;
            int width = 0;
            int is_long = 0, is_long_long = 0, is_size_t = 0;

            // Flags -> '-' or '0'
            while (format[i] == '-' || format[i] == '0') {
                if (format[i] == '-') left_justify = 1;
                if (format[i] == '0') pad_zero = 1;
                i++;
            }

            // Width -> '5', '10', etc.
            while (format[i] >= '0' && format[i] <= '9') {
                width = width * 10 + (format[i] - '0');
                i++;
            }

            // Length modifiers -> 'l', 'll', 'z', 'h' (VARARG DESYNC PROTECTION)
            while (format[i] == 'l' || format[i] == 'h' || format[i] == 'z') {
                if (format[i] == 'l') {
                    if (is_long) is_long_long = 1;
                    else is_long = 1;
                } else if (format[i] == 'z') {
                    is_size_t = 1;
                }
                i++;
            }

            // Type specifier
            switch (format[i]) {
                case 'c':
                    if (!left_justify) while(width-- > 1) buf_putc(buf, &offset, size, ' ');
                    buf_putc(buf, &offset, size, (char)va_arg(args, int));
                    if (left_justify) while(width-- > 1) buf_putc(buf, &offset, size, ' ');
                    break;
                    
                case 's': {
                    char *s = va_arg(args, char *);
                    if (!s) s = "(null)";
                    int slen = 0;
                    while(s[slen]) slen++;
                    if (!left_justify) while(width-- > slen) buf_putc(buf, &offset, size, ' ');
                    while (*s) buf_putc(buf, &offset, size, *s++);
                    if (left_justify) while(width-- > slen) buf_putc(buf, &offset, size, ' ');
                    break;
                }
                
                case 'd':
                case 'i': {
                    long val;
                    if (is_long_long) {
                        // Consume 64-bit (8 byte) to prevent vararg alignment issues
                        long long llval = va_arg(args, long long);
                        val = (long)llval; // Downcast to 32-bit when printing to avoid x86-32bit udivdi3 error
                    } else {
                        val = va_arg(args, int);
                    }
                    print_number(buf, &offset, size, val, 10, 1, width, pad_zero, 0, left_justify);
                    break;
                }
                    
                case 'u':
                case 'x':
                case 'X': {
                    unsigned long val;
                    if (is_long_long) {
                        unsigned long long ullval = va_arg(args, unsigned long long);
                        val = (unsigned long)ullval;
                    } else if (is_size_t) {
                        val = va_arg(args, uint32_t);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    int base = (format[i] == 'u') ? 10 : 16;
                    int uppercase = (format[i] == 'X') ? 1 : 0;
                    print_number(buf, &offset, size, val, base, 0, width, pad_zero, uppercase, left_justify);
                    break;
                }
                    
                case 'p':
                    buf_putc(buf, &offset, size, '0');
                    buf_putc(buf, &offset, size, 'x');
                    print_number(buf, &offset, size, (unsigned long)va_arg(args, void *), 16, 0, width > 2 ? width - 2 : 0, pad_zero, 0, left_justify);
                    break;
                    
                case '%':
                    buf_putc(buf, &offset, size, '%');
                    break;
                    
                default:
                    buf_putc(buf, &offset, size, '%');
                    buf_putc(buf, &offset, size, format[i]);
                    break;
            }
        } else {
            buf_putc(buf, &offset, size, format[i]);
        }
        i++;
    }

    buf[offset] = '\0'; 
    return offset;
}

/**
 * @brief Appends formatted text to a buffer that is being built up.
 *
 * The counterpart to printk() for output that is going to a caller rather than
 * to the screen. It takes the length so far and returns the new one, so a run of
 * these reads like the run of printk() calls it replaces and no call has to work
 * out where the previous one stopped.
 *
 * Bounded at both ends: nothing is written once the buffer is full, and
 * kvsnprintf() terminates whatever it does write.
 *
 * @param buf Destination.
 * @param cap Capacity, terminator included.
 * @param used Characters already in the buffer.
 * @param format Format string.
 * @param ... Arguments.
 * @return The new length, excluding the terminator.
 */
int kbprintf(char *buf, uint32_t cap, uint32_t used, const char *format, ...) {
    va_list args;
    int n;

    if (!buf || cap == 0 || used >= cap - 1) return (int)used;

    va_start(args, format);
    n = kvsnprintf(buf + used, cap - used, format, args);
    va_end(args);

    return (int)used + n;
}

// =========================================================================
// KERNEL LOG FUNCTION
// =========================================================================
#define PRINTK_BUF_SIZE 256 // Stack protection: reduced from 1024 to 256 Bytes!

/**
 * @brief Prints formatted output to the kernel logs, terminal, and serial port.
 * 
 * @param format The format string.
 * @param ... Variable arguments.
 * @return The number of characters printed.
 */
int printk(const char *format, ...) {
    // 256 Byte buffer uses only 6% of the 4KB stack.
    // Greatly prevents stack overflow during interrupts.
    char print_buffer[PRINTK_BUF_SIZE]; 
    va_list args;

    if (!vga_mutex_initialized) {
        mutex_init(&vga_mutex);
        vga_mutex_initialized = 1;
    }

    va_start(args, format);
    int len = kvsnprintf(print_buffer, sizeof(print_buffer), format, args);
    va_end(args);

    if (!kernel_panic_mode) mutex_lock(&vga_mutex, 0);

    /*
     * The screen and the serial port, and no longer the log.
     *
     * Every character printed used to be fed to klog_write_char() as well, so
     * the 8 KB log held the boot banner, the ASCII art and the first-boot
     * password prompts alongside the records that mattered - and, because the
     * buffer did not wrap, filled with them and then dropped everything after.
     * A log is a record of events rather than a transcript of the screen; klog()
     * records its own lines now.
     */
    for (int i = 0; i < len; i++) {
        terminal_putchar(print_buffer[i]);
        serial_write_char(print_buffer[i]);
    }

    if (!kernel_panic_mode) mutex_unlock(&vga_mutex);

    return len;
}