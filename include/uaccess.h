#ifndef UACCESS_H
#define UACCESS_H

#include "types.h"

/**
 * @brief Copies data from user space to kernel space safely.
 * If a page fault occurs during the copy, it returns an error instead of panicking.
 * 
 * @param to Pointer to kernel memory destination.
 * @param from Pointer to user memory source.
 * @param n Number of bytes to copy.
 * @return 0 on success, or -EFAULT if a memory fault occurred.
 */
int copy_from_user(void *to, const void *from, size_t n);

/**
 * @brief Copies data from kernel space to user space safely.
 * If a page fault occurs during the copy, it returns an error instead of panicking.
 * 
 * @param to Pointer to user memory destination.
 * @param from Pointer to kernel memory source.
 * @param n Number of bytes to copy.
 * @return 0 on success, or -EFAULT if a memory fault occurred.
 */
int copy_to_user(void *to, const void *from, size_t n);

/**
 * @brief Copies a NUL-terminated string from user space.
 *
 * @param to Kernel buffer receiving the string.
 * @param from User-space string pointer.
 * @param max_len Size of @p to, including the trailing NUL byte.
 * @return E_OK on success, E_FAULT for an invalid user address, or E_NAMETOOLONG.
 */
int copy_string_from_user(char *to, const char *from, size_t max_len);

/**
 * @brief Records whether SMAP is active in CR4.
 *
 * User-memory copies use STAC/CLAC only after the x86 feature was enabled.
 */
void uaccess_set_smap_enabled(int enabled);

#endif
