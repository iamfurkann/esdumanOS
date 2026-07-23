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