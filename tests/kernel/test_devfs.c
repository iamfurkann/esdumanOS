/*
 * File: test_devfs.c
 * Purpose: DevFS (Device File System) tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "stdio.h"
#include "types.h"

extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern int get_device_idx(const char *name);

void run_devfs_tests(void) {
    printk("\n--- DevFS (Device File System) Tests ---\n");
    serial_print("\n--- DevFS (Device File System) Tests ---\n");

    // 1. Verify existence of /dev directory on VFS
    int dev_idx = fs_get_entry_idx("dev", 0);
    KTEST_ASSERT(dev_idx != -1, "VFS root has /dev directory");

    // 2. Device Registration Check
    int null_idx = get_device_idx("null");
    KTEST_ASSERT(null_idx != -1, "/dev/null device is registered");

    int rand_idx = get_device_idx("random");
    KTEST_ASSERT(rand_idx != -1, "/dev/random device is registered");

    // 3. Nonexistent Device (Invalid Routing) Protection
    int fake_idx = get_device_idx("nonexistent_device_42");
    KTEST_ASSERT(fake_idx == -1, "Invalid device access request blocked (-1)");

    // 4. /dev/null I/O Tests
    extern int dev_null_read(uint8_t *buf, int size);
    extern int dev_null_write(const uint8_t *buf, int size);
    
    uint8_t dummy_buf[10];
    int null_r = dev_null_read(dummy_buf, 10);
    KTEST_ASSERT(null_r == 0, "[STRICT] /dev/null read always returns 0 (EOF)");
    
    int null_w = dev_null_write(dummy_buf, 10);
    KTEST_ASSERT(null_w == 10, "[STRICT] /dev/null write consumes data and returns size");

    // 5. /dev/random I/O Tests
    extern int dev_random_read(uint8_t *buf, int size);
    extern int dev_random_write(const uint8_t *buf, int size);
    
    uint8_t rand_buf1[4] = {0};
    uint8_t rand_buf2[4] = {0};
    int rand_r1 = dev_random_read(rand_buf1, 4);
    int rand_r2 = dev_random_read(rand_buf2, 4);
    
    KTEST_ASSERT(rand_r1 == 4 && rand_r2 == 4, "[STRICT] /dev/random successfully generated data");
    
    // Randomness check (Low probability of collision in practice)
    int is_diff = 0;
    for(int i=0; i<4; i++) {
        if(rand_buf1[i] != rand_buf2[i]) is_diff = 1;
    }
    KTEST_ASSERT(is_diff == 1, "[STRICT] /dev/random produced different values on consecutive reads");

    int rand_w = dev_random_write(dummy_buf, 10);
    KTEST_ASSERT(rand_w < 0, "[STRICT] /dev/random write operation blocked");
}