/**
 * @file uaccess.c
 * @brief User space access functions.
 */
#include "uaccess.h"
#include "errno.h"
#include "kernel.h"

/*
 * Address of the fixup label for the copy in progress. page_fault_handler() in
 * isr.c redirects EIP here instead of panicking when a user copy faults.
 *
 * volatile because the only reader is the interrupt handler; without it the
 * compiler is free to sink or drop the store, since nothing it can see ever
 * reads the value back.
 */
volatile uint32_t current_fault_handler = 0;

/*
 * Set by page_fault_handler() when it redirects a copy to its fixup label.
 *
 * The copy helpers report failure from this flag rather than from which exit
 * path they took. Both helpers used to end in two blocks that differed only in
 * their return constant:
 *
 *     ... uaccess_end(); current_fault_handler = 0; return E_OK;
 *   fixup:
 *     ... uaccess_end(); current_fault_handler = 0; return E_FAULT;
 *
 * The compiler has no way to know control can reach `fixup` from inside the
 * copy loop - the label's address only ever leaves the function through a
 * global, and there is no computed goto here - so it treated the block as
 * unreachable and merged the two tails. Jumping to the label then ran the
 * success path, and a genuine fault was reported as E_OK. The helpers now have
 * a single exit, so there is no second tail to merge with, and the verdict
 * comes from a volatile the compiler cannot reason about.
 *
 * Single global, like current_fault_handler: safe only because a copy is never
 * preempted. Both have to become per-task if kernel preemption is added.
 */
static volatile int uaccess_fault_occurred = 0;

void uaccess_note_fault(void) {
    uaccess_fault_occurred = 1;
}

static int smap_enabled = 0;

void uaccess_set_smap_enabled(int enabled) {
    smap_enabled = enabled ? 1 : 0;
}

static void uaccess_begin(void) {
#ifdef ARCH_X86
    if (smap_enabled) {
        asm volatile("stac" ::: "memory");
    }
#endif
}

static void uaccess_end(void) {
#ifdef ARCH_X86
    if (smap_enabled) {
        asm volatile("clac" ::: "memory");
    }
#endif
}

static int user_range_is_valid(const void *ptr, size_t n) {
    uint32_t start = (uint32_t)ptr;

    if (n == 0) {
        return 1;
    }
    if (start < 0x400000 || start >= 0xC0000000) {
        return 0;
    }
    if (n - 1 > 0xBFFFFFFF - start) {
        return 0;
    }
    return 1;
}

/**
 * @brief Copy data from user space to kernel space.
 * 
 * @param to Pointer to the destination buffer in kernel space.
 * @param from Pointer to the source buffer in user space.
 * @param n Number of bytes to copy.
 * @return 0 on success, or -E_FAULT on error.
 */
int copy_from_user(void *to, const void *from, size_t n) {
    __label__ fixup;

    if (!to || !user_range_is_valid(from, n)) return E_FAULT;

    char *d = (char *)to;
    const char *s = (const char *)from;

    uaccess_fault_occurred = 0;
    current_fault_handler = (uint32_t)&&fixup;
    uaccess_begin();

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    /* Single exit: reached by falling out of the loop, or by the page fault
     * handler redirecting EIP here mid-copy. */
fixup:
    uaccess_end();
    asm volatile("" ::: "memory");
    current_fault_handler = 0;
    return uaccess_fault_occurred ? E_FAULT : E_OK;
}

/**
 * @brief Copy data from kernel space to user space.
 * 
 * @param to Pointer to the destination buffer in user space.
 * @param from Pointer to the source buffer in kernel space.
 * @param n Number of bytes to copy.
 * @return 0 on success, or -E_FAULT on error.
 */
int copy_to_user(void *to, const void *from, size_t n) {
    __label__ fixup;

    if (!from || !user_range_is_valid(to, n)) return E_FAULT;

    char *d = (char *)to;
    const char *s = (const char *)from;

    uaccess_fault_occurred = 0;
    current_fault_handler = (uint32_t)&&fixup;
    uaccess_begin();

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    /* Single exit: reached by falling out of the loop, or by the page fault
     * handler redirecting EIP here mid-copy. */
fixup:
    uaccess_end();
    asm volatile("" ::: "memory");
    current_fault_handler = 0;
    return uaccess_fault_occurred ? E_FAULT : E_OK;
}

int copy_string_from_user(char *to, const char *from, size_t max_len) {
    if (!to || !from || max_len == 0) return E_FAULT;

    for (size_t i = 0; i < max_len; i++) {
        if (copy_from_user(&to[i], from + i, 1) != E_OK) {
            return E_FAULT;
        }
        if (to[i] == '\0') {
            return E_OK;
        }
    }

    to[max_len - 1] = '\0';
    return E_NAMETOOLONG;
}
