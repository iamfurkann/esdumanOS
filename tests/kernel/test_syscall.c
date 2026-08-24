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
    char line[KLOG_LINE_MAX];
    int nlen = 0;
    while (needle[nlen]) nlen++;
    if (nlen <= 0) return 0;

    /*
     * One whole record per read, so there is no sliding window to keep. The byte
     * form of this needed one: a needle straddling two chunks would have been
     * missed, so each read had to overlap the last by nlen-1 bytes. A record
     * cannot straddle itself.
     */
    for (int index = 0; ; index++) {
        int got = klog_read(line, (int)sizeof(line), index);
        if (got <= 0) break;

        for (int i = 0; i + nlen <= got; i++) {
            int match = 1;
            for (int j = 0; j < nlen; j++) {
                if (line[i + j] != needle[j]) { match = 0; break; }
            }
            if (match) return 1;
        }
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
/**
 * @brief Verifies that the diagnostics render into the caller's buffer.
 *
 * Expected behavior:
 * - A call reports how many bytes it wrote and writes them where it was told.
 * - A buffer too small is filled and not overrun.
 * - A buffer that cannot be written to is refused rather than written to.
 *
 * These printed straight to the screen until v0.9.2, so `meminfo > file` wrote an
 * empty file and reported success. What that made untestable is the point: there
 * was no return value to assert on and no buffer to look in. There is now, and
 * the one failure that would bring the old behaviour back without anybody
 * noticing - a call that returns a length but writes nothing - is exactly what
 * the length-and-contents assertion catches.
 */
static void test_diag_to_buffer(void) {
    arch_regs_t regs;
    int n;

    /*
     * A user-space address, not a static in this file. The call validates that
     * the buffer belongs to the caller before it writes a byte into it, so a
     * kernel address is refused with E_FAULT - correctly, and it took three
     * failing assertions to notice the test had been handing it one.
     *
     * It has to be inside the window run_all_selftests() maps, which is four
     * pages at 0x500000 and not a byte more. The second page is the one no other
     * module uses: the first holds their scratch strings and the third and
     * fourth are taken by the integration and stress modules.
     */
    char *buf = (char *)0x501000;

    /* A guard past the end. Nothing may touch it, whatever capacity is claimed. */
    #define DIAG_CAP 200
    buf[DIAG_CAP] = (char)0xA5;

    for (int i = 0; i < DIAG_CAP; i++) buf[i] = 0;

    regs.eax = SYSCALL_MEMINFO;
    regs.ebx = (uint32_t)buf;
    regs.ecx = DIAG_CAP;
    regs.edx = 0;
    syscall_handler(&regs);
    n = (int)regs.eax;

    KTEST_ASSERT(n > 0, "[SYSCALL] meminfo reports how much it wrote");
    KTEST_ASSERT(n < DIAG_CAP && buf[0] != 0,
                 "[STRICT] [SYSCALL] and actually wrote it into the caller's buffer");
    KTEST_ASSERT(buf[DIAG_CAP] == (char)0xA5,
                 "[STRICT] [SYSCALL] without touching the byte past the capacity");

    /*
     * A capacity of one. The rendering is far longer, so this is the truncation
     * path - and the byte after the buffer is where an off-by-one would land.
     */
    buf[1] = (char)0x5A;
    regs.eax = SYSCALL_MEMINFO;
    regs.ebx = (uint32_t)buf;
    regs.ecx = 1;
    regs.edx = 0;
    syscall_handler(&regs);

    KTEST_ASSERT((int)regs.eax == 1,
                 "[STRICT] [SYSCALL] a buffer of one byte takes one byte");
    KTEST_ASSERT(buf[1] == (char)0x5A,
                 "[STRICT] [SYSCALL] and the byte after it is left alone");

    /* Nowhere to write is refused rather than written to. */
    regs.eax = SYSCALL_MEMINFO;
    regs.ebx = 0xD0000000;
    regs.ecx = 64;
    regs.edx = 0;
    syscall_handler(&regs);
    KTEST_ASSERT((int)regs.eax == E_FAULT,
                 "[STRICT] [SYSCALL] a buffer the caller does not own is refused");

    regs.eax = SYSCALL_MEMINFO;
    regs.ebx = (uint32_t)buf;
    regs.ecx = 0;
    regs.edx = 0;
    syscall_handler(&regs);
    KTEST_ASSERT((int)regs.eax == E_INVAL,
                 "[SYSCALL] and a capacity of zero is a bad argument, not an empty answer");

    #undef DIAG_CAP
}

void run_syscall_tests(void) {
    printk("\n--- Syscall Dispatcher Tests ---\n");

    test_diag_to_buffer();

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

    /*
     * The index addresses a record, not a byte.
     *
     * It used to be a byte offset into a flat ring, which a record ring cannot
     * honour: a record dropped between two reads shifts every byte position and
     * hands the reader a torn line. Two markers written back to back must come
     * back as two consecutive records, in the order they were written.
     */
    klog_record(LOG_LEVEL_INFO, "KTEST", "indexprobe-first");
    klog_record(LOG_LEVEL_INFO, "KTEST", "indexprobe-second");

    uint32_t held_now = klog_held();
    char rec_a[KLOG_LINE_MAX];
    char rec_b[KLOG_LINE_MAX];

    if (held_now >= 2) {
        int an = klog_read(rec_a, (int)sizeof(rec_a), (int)held_now - 2);
        int bn = klog_read(rec_b, (int)sizeof(rec_b), (int)held_now - 1);

        KTEST_ASSERT(an > 0 && bn > 0, "[KLOG] the last two records both read back");

        int differ = (an != bn);
        for (int i = 0; !differ && i < an; i++) if (rec_a[i] != rec_b[i]) differ = 1;
        KTEST_ASSERT(differ,
                     "[STRICT] [KLOG] consecutive indices address different records");

        /* And the same index twice is the same record, which a byte offset into
         * a moving ring could not promise. */
        char again[KLOG_LINE_MAX];
        int cn = klog_read(again, (int)sizeof(again), (int)held_now - 2);
        int same = (cn == an);
        for (int i = 0; same && i < an; i++) if (again[i] != rec_a[i]) same = 0;
        KTEST_ASSERT(same, "[STRICT] [KLOG] reading an index twice gives the same record");
    }

    /* A record carries when it happened. The byte ring could not answer this at
     * all - nothing in it was a time. */
    KTEST_ASSERT(klog_read(probe, (int)sizeof(probe), 0) > 0 && probe[0] == '[',
                 "[KLOG] a rendered record opens with its timestamp");

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

    uint32_t dropped_before = klog_dropped();

    /*
     * Push a whole ring through, which must evict the marker above.
     *
     * klog_record() rather than klog(), because these go to the ring and have no
     * business on the screen: five hundred filler lines would bury the suite's
     * own output. The byte form of this fed characters straight into the buffer,
     * which a record ring has no equivalent for - and should not, since a log
     * that accepts half a record is how the old one came to hold the boot
     * banner.
     */
    for (uint32_t i = 0; i < KLOG_RECORDS + 8; i++) {
        klog_record(LOG_LEVEL_INFO, "KTEST", "ringfiller");
    }

    klog(LOG_LEVEL_INFO, "KTEST", "ringmarker-newest");

    KTEST_ASSERT(klog_contains("ringmarker-newest"),
                 "[STRICT] [KLOG] the ring keeps the newest record after wrapping");
    KTEST_ASSERT(!klog_contains("ringmarker-oldest"),
                 "[STRICT] [KLOG] and the oldest has been overwritten, not preserved");

    /* Whatever is held is capped by the ring, however much was written. */
    KTEST_ASSERT(klog_held() == KLOG_RECORDS,
                 "[STRICT] [KLOG] a full ring holds exactly its capacity, no more");

    /*
     * And it says how many it lost. The byte ring wrapped mid-line and counted
     * nothing, so a reader saw a gap with no way to know there had been one.
     */
    KTEST_ASSERT(klog_dropped() > dropped_before,
                 "[STRICT] [KLOG] the ring counts the records it overwrote");

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
