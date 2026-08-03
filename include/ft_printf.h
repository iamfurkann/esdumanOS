/**
 * @file ft_printf.h
 * @brief Kernel print formatting functions.
 */
#ifndef FT_PRINTF_H
# define FT_PRINTF_H

# include <stdarg.h>
# include "tty.h"

/**
 * @brief Formats and prints a string to the kernel terminal.
 * 
 * @param c The format string.
 * @param ... The arguments for the format string.
 * @return The number of characters printed.
 */
int	printk(const char *c, ...);

/**
 * @brief Prints a character to the kernel terminal.
 * 
 * @param c The character to print.
 * @return The number of characters printed.
 */
int	ft_kputchar(int c);

/**
 * @brief Prints a string to the kernel terminal.
 * 
 * @param c The string to print.
 * @return The number of characters printed.
 */
int	ft_kputstr(char *c);

/**
 * @brief Prints a signed integer to the kernel terminal.
 * 
 * @param c The integer to print.
 * @return The number of characters printed.
 */
int	ft_kputnbr(int c);

/**
 * @brief Prints an unsigned integer to the kernel terminal.
 * 
 * @param c The unsigned integer to print.
 * @return The number of characters printed.
 */
int	ft_kputnbru(unsigned int c);

/**
 * @brief Prints an unsigned integer in hexadecimal format.
 * 
 * @param c The unsigned integer to print.
 * @param mod A modifier indicating lowercase (0) or uppercase (1).
 * @return The number of characters printed.
 */
int	ft_kputhex(unsigned int c, int mod);

/**
 * @brief Prints a pointer address in hexadecimal format.
 * 
 * @param ptr The pointer to print.
 * @return The number of characters printed.
 */
int	ft_kputptr(void *ptr);


#endif