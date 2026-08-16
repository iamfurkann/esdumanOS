/*
 * File: test_syscall.c
 * Purpose: Syscall Dispatcher and boundary tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "registers.h"
#include "isr.h"
#include "klog.h"
#include "process.h"
#include "errno.h"
/**
 * @brief Tests the kernel's system call dispatching logic and argument boundary validation.
 *
 * This test ensures the `syscall_handler` correctly sanitizes incoming register values
 * from user space. It aims to prevent vulnerabilities stemming from invalid system call
 * IDs, negative file descriptors, integer overflows, and use-after-close scenarios.
 *
 * Expected behavior:
 * - Invalid system call numbers are rejected and return `-ENOSYS`.
 * - Malformed arguments (like negative FDs or oversized read sizes) trigger boundary
 *   errors and return a negative error code without executing the underlying mechanism.
 * - Stale/closed file descriptors are rejected.
 *
 * Edge cases covered:
 * - Handling unmapped syscall IDs gracefully.
 * - Integer overflow size attacks during read operations.
 * - Reuse of closed file descriptors.
 */
void run_syscall_tests(void) {
    printk("\n--- Syscall Dispatcher Tests ---\n");

    // Allocate an architecture-specific register structure to simulate syscall input.
    arch_regs_t regs;
    
    // =========================================================================
    // 1. Valid Syscall Boundary Check Note
    // =========================================================================
    // We intentionally avoid directly dispatching a blocking valid syscall (like READ) 
    // without proper VFS/keyboard backing because doing so could stall the entire test suite.
    // The focus here is strictly on security validations and negative boundary rejections.
    regs.eax = SYSCALL_READ; // A typical, valid syscall ID.
    regs.ebx = 0; regs.ecx = 0; regs.edx = 0;
    
    // =========================================================================
    // 2. Invalid Syscall ID Test (Out of bounds)
    // =========================================================================
    regs.eax = 9999;
    syscall_handler(&regs); 
    KTEST_ASSERT((int)regs.eax < 0, "Syscall Dispatcher: Undefined (OutOfBounds) numbers rejected (-ENOSYS)");
    
    // =========================================================================
    // 3. Negative File Descriptor (Negative FD) Security Test
    // =========================================================================
    regs.eax = SYSCALL_READ;
    regs.ebx = (uint32_t)-1; // FD = -1, which evaluates to 0xFFFFFFFF unsigned.
    regs.ecx = 0x80000000;   // Arbitrary user-space buffer address.
    regs.edx = 10;           // Read 10 bytes.
    syscall_handler(&regs);
    KTEST_ASSERT((int)regs.eax < 0, "Syscall Security: Negative FD (read(-1)) successfully rejected");
    
    // =========================================================================
    // 4. Integer Overflow Memory Protection Test
    // =========================================================================
    regs.eax = SYSCALL_READ;
    regs.ebx = 0; // Read from STDIN (FD 0).
    regs.ecx = 0x80000000;
    regs.edx = 0xFFFFFFFF; // Extremely large size argument intended to cause overflow.
    syscall_handler(&regs);
    KTEST_ASSERT((int)regs.eax < 0, "Syscall Security: Integer overflow size argument (0xFFFFFFFF) rejected");

    // =========================================================================
    // 5. Use-After-Close FD Security Test
    // =========================================================================
    regs.eax = SYSCALL_CLOSE;
    regs.ebx = 4; // Close a random FD.
    syscall_handler(&regs);
    
    // Next, we attempt to read from that exact same closed FD.
    // The syscall dispatcher must realize this FD is closed/stale and reject the read immediately.
    regs.eax = SYSCALL_READ;
    regs.ebx = 4; // Reuse closed FD.
    regs.ecx = 0x80000000;
    regs.edx = 10;
    syscall_handler(&regs);
    KTEST_ASSERT((int)regs.eax < 0, "Syscall Security: Use-After-Close FD usage rejected");

    // =========================================================================
    // 6. DMESG hands the log back instead of printing it
    // =========================================================================
    /*
     * The log used to be written to the screen from inside the kernel, which put
     * it out of reach of everything the shell can do with output: "dmesg | head"
     * read an empty pipe and "dmesg > boot.log" left the file empty. The buffer
     * form copies a slice out and lets the caller write it.
     *
     * klog_read() is checked first on its own, because the bounds are where this
     * goes wrong quietly - an offset past the end that returned a negative count
     * would turn the shell's read-until-zero loop into an infinite one.
     */
    char probe[64];

    int first = klog_read(probe, (int)sizeof(probe), 0);
    KTEST_ASSERT(first > 0, "[KLOG] klog_read returns bytes from the start of the log");
    KTEST_ASSERT(first <= (int)sizeof(probe),
                 "[STRICT] [KLOG] klog_read never writes past the buffer it was given");

    KTEST_ASSERT(klog_read(probe, (int)sizeof(probe), 1 << 30) == 0,
                 "[STRICT] [KLOG] an offset past the end of the log reads 0, not an error");
    KTEST_ASSERT(klog_read(probe, 0, 0) == 0,
                 "[KLOG] a zero-length read is refused rather than treated as unbounded");
    KTEST_ASSERT(klog_read(0, (int)sizeof(probe), 0) == 0,
                 "[KLOG] a null destination is refused");

    /* Offsets address the same bytes: the tail of one read is the head of the
     * read that starts inside it. */
    char head_slice[16];
    char tail_slice[16];
    int hn = klog_read(head_slice, 16, 0);
    int tn = klog_read(tail_slice, 16, 8);
    if (hn == 16 && tn > 0) {
        KTEST_ASSERT(head_slice[8] == tail_slice[0],
                     "[STRICT] [KLOG] an offset read continues where the caller asked, not at the start");
    }

    /*
     * And through the syscall. Root only, so the uid is borrowed for the check
     * and put back - the suite runs as whatever the test task was created with,
     * and the permission branch is worth proving in both directions.
     */
    if (current_task != 0) {
        uint32_t saved_uid = current_task->uid;

        char *u_klog = (char *)0x500A00;   /* user-space address, as elsewhere */
        u_klog[0] = '\0';

        regs.cs = 0x08;   /* kernel CS: this frame did not come from Ring 3 */

        current_task->uid = 0;
        regs.eax = SYSCALL_DMESG;
        regs.ebx = (uint32_t)u_klog;
        regs.ecx = 32;
        regs.edx = 0;
        syscall_handler(&regs);
        KTEST_ASSERT((int)regs.eax > 0 && (int)regs.eax <= 32,
                     "[STRICT] [KLOG] dmesg() copies the log into the caller's buffer");
        KTEST_ASSERT(u_klog[0] != '\0',
                     "[STRICT] [KLOG] the bytes actually arrive in user space");

        regs.eax = SYSCALL_DMESG;
        regs.ebx = 0;   /* the screen dump, which is what it always did */
        regs.ecx = 0;
        regs.edx = 0;
        syscall_handler(&regs);
        KTEST_ASSERT((int)regs.eax == 0,
                     "[KLOG] a null buffer still asks for the screen dump");

        regs.eax = SYSCALL_DMESG;
        regs.ebx = 0xD0000000;   /* kernel heap */
        regs.ecx = 32;
        regs.edx = 0;
        syscall_handler(&regs);
        KTEST_ASSERT((int)regs.eax == E_FAULT,
                     "[STRICT] [UACCESS] dmesg() refuses a kernel address for its destination");

        regs.eax = SYSCALL_DMESG;
        regs.ebx = (uint32_t)u_klog;
        regs.ecx = 32;
        regs.edx = (uint32_t)-1;   /* negative offset */
        syscall_handler(&regs);
        KTEST_ASSERT((int)regs.eax == E_INVAL,
                     "[STRICT] [KLOG] dmesg() refuses a negative offset");

        current_task->uid = 1000;
        regs.eax = SYSCALL_DMESG;
        regs.ebx = (uint32_t)u_klog;
        regs.ecx = 32;
        regs.edx = 0;
        syscall_handler(&regs);
        KTEST_ASSERT((int)regs.eax == E_PERM,
                     "[STRICT] [SEC] dmesg() is still refused to a non-root caller");

        current_task->uid = saved_uid;
    }
}
