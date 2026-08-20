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
#include "fs.h"
/**
 * @brief Whether the log currently holds a piece of text.
 *
 * Reads in overlapping windows, because a marker that straddles a window
 * boundary would otherwise be missed and the test would claim the log had
 * dropped something it still held.
 */
static int klog_contains(const char *needle) {
    char win[128];
    int nlen = 0;
    while (needle[nlen]) nlen++;
    if (nlen <= 0 || nlen > (int)sizeof(win)) return 0;

    int off = 0, got;
    while ((got = klog_read(win, (int)sizeof(win), off)) > 0) {
        for (int i = 0; i + nlen <= got; i++) {
            int match = 1;
            for (int j = 0; j < nlen; j++) {
                if (win[i + j] != needle[j]) { match = 0; break; }
            }
            if (match) return 1;
        }
        if (got < nlen) break;
        off += got - (nlen - 1);
    }
    return 0;
}

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

    // =========================================================================
    // 7. The log is a ring, and a record is one line
    // =========================================================================
    /*
     * It was a fill-once buffer that stopped accepting at 8191 and dropped
     * everything after, while calling itself a ring in both this source and the
     * README. A log that discards the newest records is the opposite of a log,
     * and nothing noticed because nothing had ever filled it on purpose.
     */
    klog(LOG_LEVEL_INFO, "KTEST", "ringmarker-oldest");
    KTEST_ASSERT(klog_contains("ringmarker-oldest"),
                 "[KLOG] a record just written is in the log");

    /* Push a whole buffer through, which must evict the marker above. */
    for (uint32_t i = 0; i < KLOG_BUF_SIZE + 256; i++) klog_write_char('.');

    klog(LOG_LEVEL_INFO, "KTEST", "ringmarker-newest");

    KTEST_ASSERT(klog_contains("ringmarker-newest"),
                 "[STRICT] [KLOG] the ring keeps the newest record after wrapping");
    KTEST_ASSERT(!klog_contains("ringmarker-oldest"),
                 "[STRICT] [KLOG] and the oldest has been overwritten, not preserved");

    /* Whatever is held is capped by the ring, however much was written. */
    int total = 0, chunk2;
    char scan[128];
    while ((chunk2 = klog_read(scan, (int)sizeof(scan), total)) > 0) total += chunk2;
    KTEST_ASSERT(total > 0 && total <= (int)KLOG_BUF_SIZE,
                 "[STRICT] [KLOG] a full read never exceeds the size of the ring");

    /*
     * The value belongs on the same line as its message. klog_int() called
     * klog(), which had already ended the line, so every value in the log sat
     * orphaned on a line of its own beneath the text it belonged to.
     */
    klog_int(LOG_LEVEL_INFO, "KTEST", "valuemarker", 4243);
    KTEST_ASSERT(klog_contains("valuemarker 4243"),
                 "[STRICT] [KLOG] klog_int puts the value on the message's line");
    KTEST_ASSERT(!klog_contains("valuemarker\n"),
                 "[STRICT] [KLOG] and does not end the line before it");

    klog_hex(LOG_LEVEL_INFO, "KTEST", "hexmarker", 0x2A);
    KTEST_ASSERT(klog_contains("hexmarker 0x2A"),
                 "[STRICT] [KLOG] klog_hex does the same");

    /*
     * klog_record() reaches the log and not the screen. Only half of that can be
     * asserted from here - there is nothing to read the screen back from - so
     * this checks the half that can be, and the boot milestones are what exercise
     * the other half in practice.
     */
    klog_record(LOG_LEVEL_INFO, "KTEST", "recordmarker");
    KTEST_ASSERT(klog_contains("recordmarker"),
                 "[KLOG] klog_record reaches the log");

    // =========================================================================
    // 8. The log survives the machine
    // =========================================================================
    /*
     * /var/log has existed since the FHS hierarchy was created and has been empty
     * ever since. Written whole rather than appended to, because a file here is
     * one AES-CBC blob authenticated over its entire plaintext.
     */
    KTEST_ASSERT(klog_persist() == E_OK,
                 "[STRICT] [KLOG] the log is written out to /var/log/kern.log");

    int var_idx = fs_get_entry_idx("var", 0);
    int log_idx = (var_idx >= 0) ? fs_get_entry_idx("log", (uint8_t)var_idx) : -1;
    KTEST_ASSERT(log_idx >= 0, "[KLOG] /var/log exists to write into");

    if (log_idx >= 0) {
        vfs_file_t kern_log;
        int opened = fs_open("kern.log", (uint8_t)log_idx, &kern_log);
        KTEST_ASSERT(opened == E_OK, "[STRICT] [KLOG] and the file it wrote can be opened");

        if (opened == E_OK) {
            uint32_t written = 0;
            KTEST_ASSERT(fs_size(&kern_log, &written) == E_OK && written > 0,
                         "[STRICT] [KLOG] with the log actually in it, not an empty file");
        }

        /* Twice in a row must replace rather than fail on an existing file. */
        KTEST_ASSERT(klog_persist() == E_OK,
                     "[STRICT] [KLOG] persisting again replaces the file rather than failing");
    }
}
