#ifndef STACK_H
# define STACK_H

#include "types.h"

/**
 * @brief Buffer a whole stack dump fits in.
 *
 * 20 rows of 16 bytes, each row an address, 16 hex pairs and 16 characters -
 * about 78 characters a row, so 2 KB has room to spare. Named here so the caller
 * that allocates it and the renderer that fills it cannot disagree.
 */
#define STACK_DUMP_BUF 2048

/**
 * @brief Renders the current kernel stack into a buffer.
 *
 * Walks the kernel stack to display the call sequence, which is useful for
 * debugging kernel panics or faults.
 *
 * It printed to the screen until v0.9.2. That made `stack > dump.txt` write an
 * empty file and `stack | grep` feed an empty pipe, because the bytes went to
 * the terminal and never reached the caller's descriptor 1. The caller writes
 * them now.
 *
 * @param buf Destination.
 * @param cap Capacity, terminator included; 2 KB holds the whole dump.
 * @return Characters written, excluding the terminator.
 */
int format_kernel_stack(char *buf, uint32_t cap);

# endif