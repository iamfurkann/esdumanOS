/*
 * File: test_blockdev.c
 * Purpose: The seam between the file system and the device it is stored on.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Everything here goes through blockdev_read() and blockdev_write() against a
 * device made up for the occasion. Nothing touches the block cache, and that is
 * deliberate rather than incidental: the cache is keyed by sector number and the
 * real file system is using those numbers right now, so a read served from a
 * fake device and left in the cache would be handed to the VFS afterwards as
 * though it had come off the disk. The cache's own behaviour is asserted in
 * test_bcache.c, which knows how to clean up after itself.
 *
 * The fake device can be told to fail. That is the point of it, and the reason
 * this module had to exist before the failure handling could be tested at all:
 * QEMU's disk does not fail, so until there was a seam to inject at, "a failed
 * read is not an empty disk" was an assertion with no way to be written.
 */
#include "ktest.h"
#include "blockdev.h"
#include "errno.h"
#include "libft.h"

#define FAKE_SECTORS 8

static uint8_t  fake_store[FAKE_SECTORS][512];
static int      fake_read_result  = E_OK;   /* what the fake read reports */
static int      fake_write_result = E_OK;
static uint32_t fake_last_lba     = 0xFFFFFFFF;
static int      fake_reads        = 0;
static int      fake_writes       = 0;

static int fake_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx;
    fake_last_lba = lba;
    fake_reads++;
    if (fake_read_result != E_OK) return fake_read_result;
    ft_memcpy(buf, fake_store[lba], 512);
    return E_OK;
}

static int fake_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx;
    fake_last_lba = lba;
    fake_writes++;
    if (fake_write_result != E_OK) return fake_write_result;
    ft_memcpy(fake_store[lba], buf, 512);
    return E_OK;
}

static blockdev_t fake_dev = {
    .name         = "fake0",
    .sector_size  = 512,
    .sector_count = FAKE_SECTORS,
    .read         = fake_read,
    .write        = fake_write,
    .ctx          = 0
};

/**
 * @brief Tests what blockdev_set_root() refuses.
 *
 * A device that cannot be read from is not a device, and registering one moves
 * the failure from boot - where it can be reported once, with a name - to the
 * first mount, where it arrives as a call through a null function pointer.
 */
static void run_registration_assertions(void) {
    blockdev_t broken = fake_dev;

    broken.read = 0;
    KTEST_ASSERT(blockdev_set_root(&broken) == E_INVAL,
                 "[STRICT] [BLOCK] a device with no read handler is refused");

    broken = fake_dev;
    broken.sector_count = 0;
    KTEST_ASSERT(blockdev_set_root(&broken) == E_INVAL,
                 "[STRICT] [BLOCK] and so is one that reports no capacity");

    broken = fake_dev;
    broken.sector_size = 0;
    KTEST_ASSERT(blockdev_set_root(&broken) == E_INVAL,
                 "[BLOCK] and one with no sector size");

    /*
     * A refused registration must not have displaced whatever was there. This is
     * the assertion that says blockdev_set_root() validates before it assigns
     * rather than after.
     */
    KTEST_ASSERT(blockdev_root() != &broken,
                 "[STRICT] [BLOCK] a refused device does not become the root device");
}

/**
 * @brief Tests reads and writes against a device that works.
 */
static void run_routing_assertions(void) {
    uint8_t buf[512];
    uint8_t pattern[512];

    for (int i = 0; i < 512; i++) pattern[i] = (uint8_t)(i & 0xFF);

    KTEST_ASSERT(blockdev_set_root(&fake_dev) == E_OK,
                 "[BLOCK] a usable device registers");
    KTEST_ASSERT(blockdev_root() == &fake_dev,
                 "[STRICT] [BLOCK] and is the one reads and writes go to");

    fake_writes = 0;
    KTEST_ASSERT(blockdev_write(3, pattern) == E_OK,
                 "[BLOCK] a write inside the device succeeds");
    KTEST_ASSERT(fake_writes == 1 && fake_last_lba == 3,
                 "[STRICT] [BLOCK] and reaches the device once, at the sector asked for");

    fake_reads = 0;
    ft_memset(buf, 0, sizeof(buf));
    KTEST_ASSERT(blockdev_read(3, buf) == E_OK,
                 "[BLOCK] the sector reads back");

    {
        int same = 1;
        for (int i = 0; i < 512; i++) {
            if (buf[i] != pattern[i]) { same = 0; break; }
        }
        KTEST_ASSERT(same,
                     "[STRICT] [BLOCK] byte for byte, which is the whole contract");
    }
}

/**
 * @brief Tests the bounds check the layer performs on behalf of every driver.
 *
 * This lived inside the ATA driver until v1.2.0. Moving it here is not tidiness:
 * a check that each driver has to remember is a check the second driver omits,
 * and this project has watched that happen with permission bits and with the
 * IMMUTABLE guards.
 */
static void run_bounds_assertions(void) {
    uint8_t buf[512];

    fake_reads = 0;
    fake_writes = 0;

    KTEST_ASSERT(blockdev_read(FAKE_SECTORS, buf) == E_INVAL,
                 "[STRICT] [BLOCK] a read one sector past the end is refused");
    KTEST_ASSERT(blockdev_write(FAKE_SECTORS, buf) == E_INVAL,
                 "[STRICT] [BLOCK] and so is the write");
    KTEST_ASSERT(blockdev_read(0xFFFFFFFF, buf) == E_INVAL,
                 "[BLOCK] and a wildly out of range sector");

    KTEST_ASSERT(fake_reads == 0 && fake_writes == 0,
                 "[STRICT] [BLOCK] none of which reached the driver at all");

    KTEST_ASSERT(blockdev_read(0, 0) == E_INVAL,
                 "[BLOCK] a null buffer is refused");
}

/**
 * @brief Tests that a device's failure arrives at the caller unchanged.
 *
 * The defect this release exists for: the ATA driver reported failures and
 * nothing read them, so a failed read reached the file system as 512 zero bytes.
 * These assertions are what make "the error gets through" a fact rather than an
 * intention.
 */
static void run_failure_assertions(void) {
    uint8_t buf[512];

    fake_read_result = E_IO;
    KTEST_ASSERT(blockdev_read(0, buf) == E_IO,
                 "[STRICT] [BLOCK] the device's read error reaches the caller as itself");

    fake_read_result = E_NODEV;
    KTEST_ASSERT(blockdev_read(0, buf) == E_NODEV,
                 "[STRICT] [BLOCK] and is not flattened into one generic failure");

    fake_read_result = E_OK;

    fake_write_result = E_IO;
    KTEST_ASSERT(blockdev_write(0, buf) == E_IO,
                 "[STRICT] [BLOCK] a write error reaches the caller too");
    fake_write_result = E_OK;

    /*
     * A device with no write handler is a coherent thing - a read-only medium -
     * and it is refused with the errno that says so rather than crashing through
     * a null pointer.
     */
    {
        blockdev_t ro = fake_dev;
        ro.write = 0;
        KTEST_ASSERT(blockdev_set_root(&ro) == E_OK,
                     "[BLOCK] a device with no write handler still registers");
        KTEST_ASSERT(blockdev_write(0, buf) == E_ROFS,
                     "[STRICT] [BLOCK] and refuses writes with E_ROFS, not a null call");
        blockdev_set_root(&fake_dev);
    }
}

/**
 * @brief Tests that with nothing registered, nothing is attempted.
 */
static void run_detached_assertions(void) {
    uint8_t buf[512];

    blockdev_set_root(0);
    KTEST_ASSERT(blockdev_root() == 0,
                 "[BLOCK] the root device can be detached");
    KTEST_ASSERT(blockdev_read(0, buf) == E_NODEV,
                 "[STRICT] [BLOCK] a read with no device is E_NODEV, not a crash");
    KTEST_ASSERT(blockdev_write(0, buf) == E_NODEV,
                 "[STRICT] [BLOCK] and so is a write");
}

/**
 * @brief Tests the block device layer.
 *
 * Expected Behavior:
 * - Reads and writes reach the registered device, at the sector asked for.
 * - A sector past the device's capacity is refused without reaching the driver.
 * - A driver's errno arrives at the caller unchanged.
 * - With no device registered, both entry points answer E_NODEV.
 *
 * Edge Cases Covered:
 * - A device that cannot be read from, or reports no capacity, is refused.
 * - A read-only device answers E_ROFS rather than calling a null handler.
 *
 * The real disk is put back at the end. Every module that runs after this one
 * mounts it, so leaving a fake device registered would not fail here - it would
 * fail somewhere else, which is the worst way for a test to break something.
 */
void run_blockdev_tests(void) {
    blockdev_t *saved = blockdev_root();

    printk("\n--- Block Device Layer Tests ---\n");

    ft_memset(fake_store, 0, sizeof(fake_store));
    fake_read_result = E_OK;
    fake_write_result = E_OK;

    run_registration_assertions();
    run_routing_assertions();
    run_bounds_assertions();
    run_failure_assertions();
    run_detached_assertions();

    blockdev_set_root(saved);
    KTEST_ASSERT(blockdev_root() == saved,
                 "[STRICT] [BLOCK] and the real disk is back where the next module expects it");
}
