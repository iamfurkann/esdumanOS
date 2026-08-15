/*
 * File: ktest_signal.c
 * Purpose: A Ring 3 program that signals itself fatally, for the default-action test.
 *
 * The self-signalled half of the SIG_KILL default action cannot be reached from
 * the kernel-mode suite. send_user_signal() reaps any target that is not the
 * running task on the spot, but a task that signals itself is left with the bit
 * pending and terminated by apply_default_signal_action(), which is called from
 * exactly one place: the end of syscall_handler(), on the way back out to user
 * mode. The kernel-mode modules run against a synthetic task and never return
 * through syscall_handler() at all, so nothing there can exercise that path.
 *
 * A real Ring 3 process can, and this is it - the same shape as ktest_crash.c,
 * which exists for the same kind of reason: a path that only a genuine user-mode
 * process can walk into.
 *
 * Built and embedded only into $(TEST_BIN). A program whose only purpose is to
 * kill itself has no business shipping in /bin.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscall.h"

/** Terminates a process that has not registered a handler. Mirrors signal.h. */
#define SIG_KILL 9

static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Announces itself, then sends itself SIG_KILL.
 *
 * Death happens inside the kill syscall rather than after it. send_user_signal()
 * records the signal and declines to reap the caller - it is running on this
 * task's kernel stack - so the bit is still pending when syscall_handler()
 * finishes its bookkeeping and calls apply_default_signal_action(), which calls
 * exit_current_process(). The instruction after the int 0x80 is never reached.
 *
 * There is deliberately no exit() below, for the same reason ktest_crash.c has
 * none: if the default action ever stopped firing, this would run off the end of
 * main() into whatever follows rather than quietly reporting a clean status, and
 * the parent's assertion on 137 would fail instead of accidentally passing.
 */
void main(void) {
    const char *msg = "[SIGNAL] about to kill myself on purpose\n";
    int len = 0;
    while (msg[len]) len++;
    syscall(SYSCALL_WRITE, 1, (int)msg, len);

    int my_pid = syscall(SYSCALL_GETPID, 0, 0, 0);
    syscall(SYSCALL_KILL, my_pid, SIG_KILL, 0);

    /* Unreachable. */
    while (1) { }
}
