#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include "syscall.h"
#include "tty.h"

/**
 * @brief Prints formatted text to the kernel terminal.
 * 
 * Functions similarly to the standard C library 'printf', interpreting 
 * format specifiers (e.g., %d, %s, %x) to format and output arguments.
 * 
 * @param format The format string.
 * @param ... The variable arguments matching the format specifiers.
 * @return int The total number of characters printed.
 */
int printk(const char *format, ...);

/**
 * @brief Formats into a buffer, as printk() does into the screen.
 *
 * Exported as of v0.9.2 because commands stopped printing their output from the
 * kernel. A syscall that prints cannot be piped or redirected - the bytes go to
 * the screen and never reach the caller's descriptor 1 - so the ones that
 * produced output now fill a buffer the caller wrote, and this is what they fill
 * it with.
 *
 * @param buf Destination.
 * @param size Capacity, terminator included.
 * @param format Format string.
 * @param args Arguments.
 * @return Characters written, excluding the terminator.
 */
int kvsnprintf(char *buf, uint32_t size, const char *format, va_list args);

/**
 * @brief Appends formatted text to a buffer that is being built up.
 *
 * Takes the length so far and returns the new one, so a run of these reads like
 * the run of printk() calls it replaces without any of them having to think
 * about where the last one stopped. Never writes past the capacity and never
 * leaves the buffer unterminated; once full, further calls add nothing and
 * return the length unchanged.
 *
 * @param buf Destination.
 * @param cap Capacity, terminator included.
 * @param used Characters already in the buffer.
 * @param format Format string.
 * @param ... Arguments.
 * @return The new length, excluding the terminator.
 */
int kbprintf(char *buf, uint32_t cap, uint32_t used, const char *format, ...);

/**
 * @brief Kernel helper to output a single character.
 * 
 * @param c The character to output.
 * @return int The number of characters output (1).
 */
int	ft_kputchar(int c);

/**
 * @brief Kernel helper to output a string.
 * 
 * @param c The null-terminated string to output.
 * @return int The number of characters output.
 */
int	ft_kputstr(char *c);

/**
 * @brief Kernel helper to output a signed integer.
 * 
 * @param c The integer to output.
 * @return int The number of characters output.
 */
int	ft_kputnbr(int c);

/**
 * @brief Kernel helper to output an unsigned integer.
 * 
 * @param c The unsigned integer to output.
 * @return int The number of characters output.
 */
int	ft_kputnbru(unsigned int c);

/**
 * @brief Kernel helper to output an unsigned integer in hexadecimal format.
 * 
 * @param c The integer to output in hex.
 * @param mod Determine uppercase (1) or lowercase (0) hex digits.
 * @return int The number of characters output.
 */
int	ft_kputhex(unsigned int c, int mod);

/**
 * @brief Kernel helper to output a memory pointer.
 * 
 * @param ptr The pointer to output (usually in hex format).
 * @return int The number of characters output.
 */
int	ft_kputptr(void *ptr);

/**
 * @brief Creates a unidirectional data channel (pipe).
 * 
 * Used for inter-process communication, enabling data written to the 
 * write end (pipefd[1]) to be read from the read end (pipefd[0]).
 * 
 * @param pipefd Array of two integers to store the read/write file descriptors.
 * @return int 0 on success, negative error code on failure.
 */
int pipe(int pipefd[2]);

/**
 * @brief Duplicates a file descriptor.
 * 
 * Makes newfd be the copy of oldfd, closing newfd first if necessary.
 * 
 * @param oldfd The existing file descriptor.
 * @param newfd The desired new file descriptor.
 * @return int The new file descriptor on success, negative error code on failure.
 */
int dup2(int oldfd, int newfd);

#endif // STDIO_H