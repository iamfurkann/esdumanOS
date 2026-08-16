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
#include "libft.h"
#include "process.h"
#include "keyboard.h"
// =========================================================
// Kernel Internal (Ring 0) Syscall Trigger Bridge
// =========================================================
static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/*
 * The keyboard ring buffer, reached directly so the console end-of-file can be
 * tested without a keystroke. keyboard.h deliberately does not export these -
 * nothing but the driver has any business writing to them - but a test that
 * cannot produce an IRQ1 has no other way to put a byte where get_keyboard_char()
 * will find it.
 */
extern volatile char kbd_buffer[];
extern volatile int kbd_head;
extern volatile int kbd_tail;

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
    volatile int *u_fds = (volatile int *)0x500700;
    char *u_write_buf = (char *)0x500800;
    char *u_read_buf = (char *)0x500900;
    ft_strcpy(u_write_buf, "42KFS");

    int pipe_sys = ktest_syscall(SYSCALL_PIPE, (int)u_fds, 0, 0);
    KTEST_ASSERT(pipe_sys == 0, "[STRICT] SYSCALL_PIPE executed successfully (res == 0)");
    if (!(u_fds[0] >= 3 && u_fds[1] >= 3)) { printk("u_fds: %d, %d\n", u_fds[0], u_fds[1]); } KTEST_ASSERT(u_fds[0] >= 3 && u_fds[1] >= 3, "[STRICT] SYSCALL_PIPE returned valid FDs (FD >= 3)");

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

    // ---------------------------------------------------------
    // PART 3: THE BROKEN PIPE
    // ---------------------------------------------------------
    /*
     * pipe_write() has refused a write to a reader-less pipe since v0.5.2 and
     * nothing tested it - the release that fixed the behaviour left no coverage
     * behind, so a regression would have been silent.
     *
     * Driven through pipe_write() directly rather than SYSCALL_WRITE. The syscall
     * now raises SIG_PIPE against the writer, and the writer here is the task
     * running this suite: signalling it would end the run, and a run that ends
     * early still prints everything it got through. The signal half is tested
     * from Ring 3, where a child can be allowed to die.
     */
    pipe_t *bp = create_pipe();
    KTEST_ASSERT(bp != 0, "[STRICT] create_pipe returned a pipe for the broken-pipe checks");

    if (bp) {
        uint8_t payload[4] = { 'd', 'a', 't', 'a' };

        KTEST_ASSERT(bp->broken_reported == 0,
                     "[PIPE] a freshly allocated pipe starts with its break unreported");

        bp->read_refs = 0; // Every reader has gone; the buffer is still empty.

        int broken = pipe_write(bp, payload, 4);
        KTEST_ASSERT(broken == E_PIPE,
                     "[STRICT] [PIPE] a write to a pipe with no readers is refused with EPIPE");
        KTEST_ASSERT(bp->head == bp->tail,
                     "[STRICT] [PIPE] the refused write left no bytes behind in the buffer");
        KTEST_ASSERT(bp->broken_reported == 1,
                     "[PIPE] the first refusal records that the break has been reported");

        /* Once per pipe, not once per write: a writer that ignores its write
         * results keeps trying, and a line each would bury the log. */
        int broken_again = pipe_write(bp, payload, 4);
        KTEST_ASSERT(broken_again == E_PIPE,
                     "[PIPE] a second write to the same broken pipe is refused as well");
        KTEST_ASSERT(bp->broken_reported == 1,
                     "[PIPE] the break is logged once per pipe, not once per rejected write");

        destroy_pipe(bp);
    }

    /* The pool is reused, so a pipe inheriting a previous occupant's flag would
     * never report its own break. */
    pipe_t *reused = create_pipe();
    KTEST_ASSERT(reused != 0 && reused->broken_reported == 0,
                 "[STRICT] [PIPE] create_pipe clears the break flag on a reused pool slot");
    if (reused) destroy_pipe(reused);

    // ---------------------------------------------------------
    // PART 4: CONSOLE END-OF-FILE (Ctrl-D)
    // ---------------------------------------------------------
    /*
     * The console could not report end-of-file at all: sys_read() either handed
     * back a byte or blocked on WAIT_KBD. Nothing read standard input, so nothing
     * noticed - until grep, head and wc started to, and a read that cannot end
     * takes the only terminal with it.
     *
     * Seeded straight into the ring buffer because a test cannot raise IRQ1. The
     * buffer is empty during a test run, so resetting the indices loses nothing.
     */
    if (current_task != 0 && current_task->fd_table != 0 && current_task->fd_table_size > 0) {
        uint8_t saved_fd0 = current_task->fd_table[0].type;
        current_task->fd_table[0].type = FD_TYPE_CONSOLE;

        kbd_tail = 0;
        kbd_head = 0;
        kbd_buffer[0] = KBD_EOT;
        kbd_head = 1;

        /* A user-space address, like the reads above: sys_read() validates the
         * destination before it looks at the descriptor type, and a kernel stack
         * buffer would be rejected as a bad pointer rather than reaching the
         * console branch at all. */
        u_read_buf[0] = 'x';
        int console_eof = ktest_syscall(SYSCALL_READ, 0, (int)u_read_buf, 1);
        KTEST_ASSERT(console_eof == 0,
                     "[STRICT] [TTY] Ctrl-D on the console reads as end-of-file (0 bytes)");
        KTEST_ASSERT(u_read_buf[0] == 'x',
                     "[TTY] the end-of-file read copied nothing into the caller's buffer");

        /* An ordinary byte still reads as one byte - the EOF branch must not have
         * swallowed the normal path. */
        kbd_tail = 0;
        kbd_head = 0;
        kbd_buffer[0] = 'k';
        kbd_head = 1;

        int console_byte = ktest_syscall(SYSCALL_READ, 0, (int)u_read_buf, 1);
        KTEST_ASSERT(console_byte == 1 && u_read_buf[0] == 'k',
                     "[STRICT] [TTY] an ordinary keystroke still reads as one byte");

        kbd_tail = 0;
        kbd_head = 0;
        current_task->fd_table[0].type = saved_fd0;
    }
}