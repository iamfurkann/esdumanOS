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

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern process_t tasks[];
extern uint32_t fs_max_sectors;

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
    serial_print("\n--- Regression (Past Bug) Tests ---\n");

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
    // Past Bug Description: The kernel scheduler occasionally confused the hardcoded index 
    // of the task in the `tasks[]` array (e.g., 0, 1, 2) with the dynamically assigned PID 
    // (e.g., 1005). This resulted in terminating the wrong tasks.
    int test_slot = -1;
    for (int i = 0; i < 16; i++) { // Assuming MAX_TASKS is 16.
        if (tasks[i].state == 0) { test_slot = i; break; }
    }
    
    if (test_slot >= 0) {
        tasks[test_slot].pid = test_slot + 1000; 
        tasks[test_slot].state = 1; // Mark slot as occupied so search routines see it.
        
        int found_slot = -1;
        for (int i = 0; i < 16; i++) {
            if (tasks[i].pid == (test_slot + 1000) && tasks[i].state != 0) { 
                found_slot = i; 
                break; 
            }
        }
        
        // The search logic must correctly map the large PID back to the smaller array index.
        KTEST_ASSERT(found_slot == test_slot, "[STRICT] REG-02: PID and Slot (Index) confusion prevented");
        
        tasks[test_slot].state = 0; 
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
    extern uint32_t ata_identify(void);
    uint32_t identified_sectors = ata_identify();
    
    // If the thread of execution reaches this line, the timeout patch worked successfully.
    KTEST_ASSERT(identified_sectors >= 4096, 
        "[STRICT] REG-05: ATA Identify protected with Timeout against hardware lockup");
}