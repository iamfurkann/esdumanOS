/*
 * File: test_devfs.c
 * Purpose: DevFS (Device File System) tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "devfs.h"
#include "ktest.h"
#include "errno.h"
#include "types.h"

extern int fs_get_entry_idx(const char *name, uint8_t parent_id);
extern int get_device_idx(const char *name);

/**
 * @brief Tests Device File System (DevFS) functionality, registration, and I/O.
 *
 * This test suite verifies the presence of fundamental character devices
 * such as /dev/null and /dev/random, ensures proper device registration,
 * and checks their read/write boundaries and behaviors.
 *
 * Expected Behavior:
 * - VFS correctly maps the /dev node.
 * - Standard devices like 'null' and 'random' are accessible and registered.
 * - Nonexistent or malformed device requests are blocked effectively.
 * - Read/write operations on /dev/null safely discard data or return EOF.
 * - Read operations on /dev/random generate sufficient entropy.
 *
 * Edge Cases Covered:
 * - Attempted writing to read-only character devices like /dev/random.
 * - Verification of consecutive pseudo-random reads.
 */
void run_devfs_tests(void) {
    printk("\n--- DevFS (Device File System) Tests ---\n");
    serial_print("\n--- DevFS (Device File System) Tests ---\n");

    int dev_idx = fs_get_entry_idx("dev", 0);
    KTEST_ASSERT(dev_idx != -1, "VFS root has /dev directory");

    int null_idx = get_device_idx("null");
    KTEST_ASSERT(null_idx != -1, "/dev/null device is registered");

    int rand_idx = get_device_idx("random");
    KTEST_ASSERT(rand_idx != -1, "/dev/random device is registered");

    int fake_idx = get_device_idx("fakedevice");
    KTEST_ASSERT(fake_idx == E_NOENT, "Invalid device access request blocked (E_NOENT)");

    extern int dev_null_read(uint8_t *buf, int size);
    extern int dev_null_write(const uint8_t *buf, int size);
    
    uint8_t dummy_buf[10];
    // Attempt to read from /dev/null. Expected behavior is returning 0 (EOF immediately).
    int null_r = dev_null_read(dummy_buf, 10);
    KTEST_ASSERT(null_r == 0, "[STRICT] /dev/null read always returns 0 (EOF)");
    
    // Attempt to write to /dev/null. Expected behavior is accepting the write and returning the exact length.
    int null_w = dev_null_write(dummy_buf, 10);
    KTEST_ASSERT(null_w == 10, "[STRICT] /dev/null write consumes data and returns size");

    extern int dev_random_read(uint8_t *buf, int size);
    extern int dev_random_write(const uint8_t *buf, int size);
    
    uint8_t rand_buf1[4] = {0};
    uint8_t rand_buf2[4] = {0};
    
    // Read from /dev/random to test data generation lengths.
    int rand_r1 = dev_random_read(rand_buf1, 4);
    int rand_r2 = dev_random_read(rand_buf2, 4);
    
    KTEST_ASSERT(rand_r1 == 4 && rand_r2 == 4, "[STRICT] /dev/random successfully generated data");
    
    int is_diff = 0;
    for(int i = 0; i < 4; i++) {
        if(rand_buf1[i] != rand_buf2[i]) is_diff = 1;
    }
    KTEST_ASSERT(is_diff == 1, "[STRICT] /dev/random produced different values on consecutive reads");

    int rand_w = dev_random_write(dummy_buf, 10);
    KTEST_ASSERT(rand_w < 0, "[STRICT] /dev/random write operation blocked");
}