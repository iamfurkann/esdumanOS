#ifndef STACK_H
# define STACK_H

#include "types.h"

/**
 * @brief Prints the current kernel stack trace.
 * 
 * Walks through the kernel stack frames to display the call sequence, 
 * which is highly useful for debugging kernel panics or faults.
 */
void print_kernel_stack(void);

# endif