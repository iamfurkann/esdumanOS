/*
 * File: ktest_crash.c
 * Purpose: A Ring 3 program that faults on purpose, for the crash-teardown test.
 *
 * Nothing else in the image can produce a user-mode page fault: every /bin tool
 * exits cleanly, and the syscalls validate their arguments and return an errno
 * rather than faulting. So the path that runs when a user program crashes -
 * which until v0.4.2 left the parent blocked forever and leaked the whole
 * address space - had no way to be exercised at all.
 *
 * Built and embedded only into $(TEST_BIN), like the ktest_user payload. It is
 * not part of the production image; a program whose only purpose is to crash has
 * no business shipping in /bin.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscall.h"

static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Announces itself, then takes a user-mode page fault.
 *
 * The write goes to address 0. In a user address space the first page directory
 * entry is not present - clone_page_directory() zeroes entries 0..767 - so this
 * is a write to a non-present page from CPL 3: error code 6 (write, user), which
 * is exactly the case page_fault_handler() treats as a segfault.
 *
 * There is deliberately no exit() below it. If the fault ever stopped happening
 * the program would run off the end of main() into whatever follows, and the
 * test asserting on status 139 would fail rather than quietly pass.
 */
void main(void) {
    const char *msg = "[CRASH] about to fault on purpose\n";
    int len = 0;
    while (msg[len]) len++;
    syscall(SYSCALL_WRITE, 1, (int)msg, len);

    *(volatile int *)0 = 0x1;

    /* Unreachable. */
    while (1) { }
}
