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

/**
 * @brief The fault-fixup state belonging to one copy in progress.
 */
typedef struct {
    uint32_t fault_handler;  /**< Fixup label of the interrupted copy. */
    int fault_occurred;      /**< Whether that copy had already faulted. */
} uaccess_state_t;

/**
 * @brief Saves and clears the fixup state of the copy currently in progress.
 *
 * The fixup label and the fault flag are single globals, which is safe only
 * because a copy is never preempted - but a copy *can* be interrupted by a page
 * fault that the kernel resolves and returns from, and copy-on-write introduced
 * exactly that. Resolving the fault runs a second copy, whose exit clears both
 * globals; the interrupted copy then resumes with no fixup registered, and its
 * next fault panics the kernel instead of returning E_FAULT.
 *
 * Anything that copies user memory from inside a fault handler brackets itself
 * with this pair.
 *
 * @param out Receives the state to hand back to uaccess_restore_state().
 */
void uaccess_save_state(uaccess_state_t *out);

/**
 * @brief Restores fixup state saved by uaccess_save_state().
 *
 * @param in State to reinstate.
 */
void uaccess_restore_state(const uaccess_state_t *in);

#endif
