/*
 * File: test_pipe.c
 * Purpose: IPC (Pipe) unit and integration tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "pipe.h"
#include "errno.h"
#include "syscall.h" // For int 0x80 numbers

extern void ft_strcpy(char *dest, const char *src);

// =========================================================
// Kernel Internal (Ring 0) Syscall Trigger Bridge
// =========================================================
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Tests Inter-Process Communication (IPC) via pipes.
 *
 * This test suite validates the internal logic and syscall-level integration
 * of the kernel's pipe implementation. It verifies proper creation, data passing,
 * closure signaling, and boundary condition handling.
 *
 * Expected Behavior:
 * - Direct pipe creation and memory allocations execute successfully.
 * - Syscall interfaces accurately map to file descriptors (FDs) representing the pipe.
 * - Writes correctly propagate to readers, and closing the write end signals EOF.
 * - Attempts to write beyond the maximum pipe buffer size are truncated or blocked gracefully.
 *
 * Edge Cases Covered:
 * - Reading from an empty pipe (EAGAIN condition).
 * - Buffer overflow attempt writing beyond the 4096-byte pipe limit.
 */
void run_pipe_tests(void) {
    printk("\n--- IPC (Pipe) Unit and Integration Tests ---\n");
    serial_print("\n--- IPC (Pipe) Unit and Integration Tests ---\n");
    
    // ---------------------------------------------------------
    // PART 1: UNIT TEST - Internal Logic and Blocking
    // ---------------------------------------------------------
    pipe_t *p = create_pipe();
    KTEST_ASSERT(p != 0, "[STRICT] create_pipe generated pipe from static pool (p != NULL)");

    uint8_t buffer[10];
    int eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == E_AGAIN, "[STRICT] Direct read from empty pipe returned EAGAIN (-11)");

    p->write_refs = 0; // Manually close writer
    eof_check = pipe_read(p, buffer, 5);
    KTEST_ASSERT(eof_check == 0, "[STRICT] Pipe with closed writer returned EOF (0)");
    destroy_pipe(p);

    // ---------------------------------------------------------
    // PART 2: END-TO-END INTEGRATION TEST (Syscall & FD Table)
    // ---------------------------------------------------------
    
    // Prepare memory addresses simulating user-space pointers.
    int *u_fds = (int *)0x500700;
    char *u_write_buf = (char *)0x500800;
    char *u_read_buf = (char *)0x500900;
    ft_strcpy(u_write_buf, "42KFS");

    int pipe_sys = ktest_syscall(SYSCALL_PIPE, (int)u_fds, 0, 0);
    KTEST_ASSERT(pipe_sys == 0, "[STRICT] SYSCALL_PIPE executed successfully (res == 0)");
    KTEST_ASSERT(u_fds[0] >= 3 && u_fds[1] >= 3, "[STRICT] SYSCALL_PIPE returned valid FDs (FD >= 3)");

    int w_res = ktest_syscall(SYSCALL_WRITE, u_fds[1], (int)u_write_buf, 5);
    KTEST_ASSERT(w_res == 5, "[STRICT] SYSCALL_WRITE wrote 5 bytes to pipe from User-Space buffer");

    int r_res = ktest_syscall(SYSCALL_READ, u_fds[0], (int)u_read_buf, 5);
    KTEST_ASSERT(r_res == 5, "[STRICT] SYSCALL_READ read 5 bytes from pipe to User-Space buffer");

    int c_res1 = ktest_syscall(SYSCALL_CLOSE, u_fds[1], 0, 0);
    KTEST_ASSERT(c_res1 == 0, "[STRICT] SYSCALL_CLOSE successfully closed writer FD");

    // Ensure reading after writer close propagates EOF natively.
    int r_eof = ktest_syscall(SYSCALL_READ, u_fds[0], (int)u_read_buf, 5);
    KTEST_ASSERT(r_eof == 0, "[STRICT] SYSCALL_READ read EOF (0) from closed pipe");

    int c_res2 = ktest_syscall(SYSCALL_CLOSE, u_fds[0], 0, 0);
    KTEST_ASSERT(c_res2 == 0, "[STRICT] SYSCALL_CLOSE successfully closed reader FD");

    pipe_sys = ktest_syscall(SYSCALL_PIPE, (int)u_fds, 0, 0);
    if (pipe_sys == 0) {
        // Attempt to write 5000 bytes to force a buffer overflow and test the kernel's 
        // boundary checks on the 4096-byte pipe limit.
        int overflow_w = ktest_syscall(SYSCALL_WRITE, u_fds[1], (int)u_write_buf, 5000);
        KTEST_ASSERT(overflow_w <= 4096, "[SECURITY] PIPE_SIZE (4096) overflow successfully prevented");
        ktest_syscall(SYSCALL_CLOSE, u_fds[0], 0, 0);
        ktest_syscall(SYSCALL_CLOSE, u_fds[1], 0, 0);
    }
}