/*
 * File: test_syscall.c
 * Purpose: Syscall Dispatcher and boundary tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "registers.h"

extern void syscall_handler(arch_regs_t *regs);

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
    serial_print("\n--- Syscall Dispatcher Tests ---\n");

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
}
