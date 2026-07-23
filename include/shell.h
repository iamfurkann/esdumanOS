#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/**
 * @brief Converts a hexadecimal string to an integer.
 * 
 * Utility function to parse a string representing a hexadecimal number 
 * and return its 32-bit integer equivalent.
 * 
 * @param hex_str The hexadecimal string to convert.
 * @return uint32_t The parsed integer value.
 */
uint32_t hex_to_int(const char *hex_str);

/**
 * @brief Prints a memory hexdump.
 * 
 * Displays the contents of memory starting at the given address for 
 * the specified length in a standard hex dump format.
 * 
 * @param addr The starting memory address to dump.
 * @param lenght The number of bytes to dump.
 */
void print_hexdump(uint32_t addr, int lenght);

/**
 * @brief Executes a shell command.
 * 
 * The main interpreter function that parses and executes a command string 
 * entered by the user in the kernel shell.
 * 
 * @param cmd The command string to execute.
 */
void execute_command(char *cmd);

#endif