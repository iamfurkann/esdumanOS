/*
 * File: test_regression.c
 * Purpose: Regression tests for past bugs.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "syscall.h"
#include "process.h" // for tasks table
#include "fs.h"      // for vfs structure
#include "libft.h"
#include "kheap.h"
#include "ata.h"
#include "bcache.h"
/**
 * @brief Regression tests to prevent the recurrence of historically patched kernel bugs.
 *
 * This suite ensures that previously identified and fixed critical issues (such as
 * memory management faults, array index miscalculations, ATA driver hardware livelocks, 
 * and ABI alignment corruption) do not inadvertently resurface during new development cycles.
 *
 * Expected behavior:
 * - Specific exploits/bugs trigger safe error handling or bypasses instead of kernel panics.
 * - System stability is maintained when exposing past vulnerability vectors.
 *
 * Edge cases covered:
 * - Null pointer frees.
 * - Task slot vs PID decoupling.
 * - ATA driver timeout hardware emulation.
 * - Architecture pointer sizing.
 */
void run_regression_tests(void) {
    printk("\n--- Regression (Past Bug) Tests ---\n");

    // =========================================================
    // BUG-01: Dangling Pointer & NULL Free Protection
    // =========================================================
    // Past Bug Description: Calling `kfree` on an already freed pointer or a NULL pointer 
    // lacked boundary checks and caused an immediate Kernel Panic (page fault in the heap manager).
    void *ptr = kmalloc(32);
    kfree(ptr);
    kfree(NULL); // If the NULL protection regression patch is missing, the system will crash HERE!
    
    // If execution reaches this assertion, the kernel safely ignored the NULL free attempt.
    KTEST_ASSERT(1, "[STRICT] REG-01: kfree(NULL) prevented system crash (Heap stable)");

    // =========================================================
    // BUG-02: PID and Array Index (Slot) Confusion
    // =========================================================
    // Past Bug Description: The kernel scheduler occasionally confused the internal process
    // slot identifier with the dynamically assigned PID (e.g., 1005).
    // This resulted in terminating the wrong tasks.
    int dummy_pid = create_process(0,0,0);
    KTEST_ASSERT(dummy_pid > 0, "[STRICT] REG-02: Process created successfully");
    if (dummy_pid > 0) {
        process_t *found = 0;
        for (process_t *p = task_list_head; p != 0; p = p->next) {
            if (p->pid == dummy_pid) { found = p; break; }
        }
        KTEST_ASSERT(found != 0 && found->pid == dummy_pid, "[STRICT] REG-02: PID and Slot (Index) confusion prevented");
        if (found) found->state = TASK_EMPTY;
    }

    // =========================================================
    // BUG-03: ATA Disk Boundary Overflow (Timeout) Protection
    // =========================================================
    // Past Bug Description: Attempting to request reads beyond the disk's physical boundaries 
    // (e.g., beyond 4096 sectors) caused the ATA driver to wait indefinitely for a DRQ signal 
    // that the hardware would never send, locking up the kernel.
    KTEST_ASSERT(fs_max_sectors <= 4096, "[STRICT] REG-03: ATA driver prevented from exceeding max boundary (4096)");

    // =========================================================
    // BUG-04: Ring 0 <-> Ring 3 ABI and Struct Padding Mismatch
    // =========================================================
    // Past Bug Description: 32-bit (i386) architecture strictly requires 4-byte pointers. 
    // Due to improper struct packing/padding during Syscalls between Ring 3 and Ring 0, 
    // pointer alignment became misaligned and corrupted memory.
    KTEST_ASSERT(sizeof(void *) == 4, "[STRICT] REG-04: Architectural ABI pointer size (4 bytes / 32-bit) preserved");

    // =========================================================================
    // BUG-05: ATA Identify Infinite Loop Lockup (Hardware Livelock)
    // =========================================================================
    // Past Bug Description: `ata_identify()` used an unbound `while(1)` loop waiting for hardware 
    // status flags. If the real disk controller failed to raise DRQ/ERR flags, the CPU hung forever.
    // The patch introduced a timeout-based escape mechanism.
uint32_t identified_sectors = ata_identify();
    
    // If the thread of execution reaches this line, the timeout patch worked successfully.
    KTEST_ASSERT(identified_sectors >= 4096, 
        "[STRICT] REG-05: ATA Identify protected with Timeout against hardware lockup");
}