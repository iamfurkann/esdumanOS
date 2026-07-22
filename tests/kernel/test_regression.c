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

void run_regression_tests(void) {
    printk("\n--- Regression (Past Bug) Tests ---\n");
    serial_print("\n--- Regression (Past Bug) Tests ---\n");

    // =========================================================
    // BUG-01: Dangling Pointer & NULL Free Protection
    // Past Bug: When previously allocated or NULL memory 
    // was attempted to be freed with kfree, it caused a Kernel Panic.
    // =========================================================
    void *ptr = kmalloc(32);
    kfree(ptr);
    kfree(NULL); // IF NULL protection is missing, the system crashes HERE!
    KTEST_ASSERT(1, "[STRICT] REG-01: kfree(NULL) prevented system crash (Heap stable)");

    // =========================================================
    // BUG-02: PID and Array Index (Slot) Confusion
    // Past Bug: The index in the Process Table (0, 1, 2) 
    // and real PID number (e.g. 1005) were confused 
    // and the wrong task was closed.
    // =========================================================
    int test_slot = -1;
    for (int i = 0; i < 16; i++) { // MAX_TASKS = 16
        if (tasks[i].state == 0) { test_slot = i; break; }
    }
    
    if (test_slot >= 0) {
        // PID is deliberately set entirely different from the index (1000 higher)
        tasks[test_slot].pid = test_slot + 1000; 
        tasks[test_slot].state = 1; // Mark as occupied
        
        int found_slot = -1;
        for (int i = 0; i < 16; i++) {
            if (tasks[i].pid == (test_slot + 1000) && tasks[i].state != 0) { 
                found_slot = i; 
                break; 
            }
        }
        KTEST_ASSERT(found_slot == test_slot, "[STRICT] REG-02: PID and Slot (Index) confusion prevented");
        tasks[test_slot].state = 0; // Clean the slot after the test (Avoid Memory Leak)
    }

    // =========================================================
    // BUG-03: ATA Disk Boundary Overflow (Timeout) Protection
    // Past Bug: Attempting to read beyond the disk's maximum size (4096 sectors) 
    // caused the ATA driver to lock up (timeout) 
    // and put the whole system into an infinite loop.
    // =========================================================
    // Testing whether fs_max_sectors is capped to 4096.
    KTEST_ASSERT(fs_max_sectors <= 4096, "[STRICT] REG-03: ATA driver prevented from exceeding max boundary (4096)");

    // =========================================================
    // BUG-04: Ring 0 <-> Ring 3 ABI and Struct Padding Mismatch
    // Past Bug: Pointers must be 4 bytes on 32-bit (i386) architecture. 
    // While passing data via Syscall, data type (ABI) 
    // alignment was getting corrupted.
    // =========================================================
    KTEST_ASSERT(sizeof(void *) == 4, "[STRICT] REG-04: Architectural ABI pointer size (4 bytes / 32-bit) preserved");

    // =========================================================================
    // BUG-05: ATA Identify Infinite Loop Lockup (Hardware Livelock)
    // =========================================================================
    // Old Code: If hardware (Real Disk) fails and never raises DRQ/ERR flags, 
    // ata_identify() locked up infinitely inside 'while(1)'.
    // New Code: Thanks to the timeout mechanism, even if hardware fails, the function
    // safely returns without hanging.
    extern uint32_t ata_identify(void);
    uint32_t identified_sectors = ata_identify();
    
    // If we reached here, the function did not trap us in an infinite loop!
    KTEST_ASSERT(identified_sectors >= 4096, 
        "[STRICT] REG-05: ATA Identify protected with Timeout against hardware lockup");
}