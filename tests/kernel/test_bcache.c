/*
 * File: test_bcache.c
 * Purpose: Write-back admission, persistence and failure isolation.
 *
 * Test data lives only in these RAM devices. No scratch sector is assumed to be
 * unused by the mounted file system, and no test resets the global cache or
 * changes the root device. Flush both fake devices before resetting their state.
 */
#include "ktest.h"
#include "types.h"
#include "bcache.h"
#include "blockdev.h"
#include "errno.h"
#include "libft.h"
#include "rtc.h"
#include "syscall.h"
#include "isr.h"
#include "process.h"
#include "fs.h"

#define BC_TEST_SECTORS (BCACHE_SIZE + 2)
#define BC_NO_SECTOR 0xFFFFFFFFu

typedef struct {
    uint8_t sectors[BC_TEST_SECTORS][512];
    int read_result;
    int write_result;
    uint32_t write_ok_lba;
    uint32_t reads;
    uint32_t writes;
} bc_test_device_t;

static bc_test_device_t stores[2];

static int test_read(void *ctx, uint32_t lba, uint8_t *buf) {
    bc_test_device_t *d = ctx;
    d->reads++;
    if (lba >= BC_TEST_SECTORS) return E_INVAL;
    if (d->read_result != E_OK) return d->read_result;
    ft_memcpy(buf, d->sectors[lba], 512);
    return E_OK;
}

static int test_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    bc_test_device_t *d = ctx;
    d->writes++;
    if (lba >= BC_TEST_SECTORS) return E_INVAL;
    if (d->write_result != E_OK && lba != d->write_ok_lba) return d->write_result;
    ft_memcpy(d->sectors[lba], buf, 512);
    return E_OK;
}

static blockdev_t devs[2] = {
    { "bc-test-a", 512, BC_TEST_SECTORS, test_read, test_write, &stores[0] },
    { "bc-test-b", 512, BC_TEST_SECTORS, test_read, test_write, &stores[1] }
};

static int bytes_are(const uint8_t *buf, uint8_t value) {
    for (int i = 0; i < 512; i++) if (buf[i] != value) return 0;
    return 1;
}

static void reset_devices(void) {
    int clean = 1;
    for (int i = 0; i < 2; i++) {
        devs[i].sector_size = 512;
        devs[i].sector_count = BC_TEST_SECTORS;
        devs[i].read = test_read;
        devs[i].write = test_write;
        stores[i].write_result = E_OK;
        if (bcache_flush_dev(&devs[i]) != E_OK) clean = 0;
    }
    KTEST_ASSERT(clean, "[STRICT] [BCACHE] fake devices flush and detach before reuse");
    ft_bzero(stores, sizeof(stores));
    stores[0].write_ok_lba = stores[1].write_ok_lba = BC_NO_SECTOR;
}

static void run_policy_assertions(void) {
    uint8_t buf[512];
    uint8_t readback[512];
    reset_devices();
    ft_memset(buf, 0x31, sizeof(buf));

    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_OK &&
                 stores[0].writes == 0 && bcache_dirty_count() == 1,
                 "[BCACHE] an admitted write is deferred and counted once");
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_OK &&
                 bcache_dirty_count() == 1,
                 "[BCACHE] overwriting one dirty sector does not count it twice");
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, readback) == E_OK &&
                 bytes_are(readback, 0x31) && stores[0].reads == 0,
                 "[STRICT] [BCACHE] a cache hit returns all 512 accepted bytes");
    KTEST_ASSERT(!bcache_flush_is_due(), "[BCACHE] fresh data is not due immediately");

    uint32_t start = timer_get_ticks();
    while (!bcache_flush_is_due() &&
           (uint32_t)(timer_get_ticks() - start) < BCACHE_FLUSH_INTERVAL_TICKS * 2) {
        asm volatile("hlt");
    }
    KTEST_ASSERT(bcache_flush_is_due(), "[BCACHE] dirty data becomes due at the deadline");
    bcache_flush_if_due();
    KTEST_ASSERT(bcache_dirty_count() == 0 && bytes_are(stores[0].sectors[0], 0x31),
                 "[STRICT] [BCACHE] the due flush actually persists all bytes");
    KTEST_ASSERT(!bcache_flush_is_due(), "[BCACHE] clean data has no flush deadline");

    int accepted = 1;
    uint32_t peak = 0;
    for (uint32_t i = 0; i < BCACHE_DIRTY_HIGH_WATER; i++) {
        if (bcache_write_sector(&devs[0], i, buf) != E_OK) accepted = 0;
        if (bcache_dirty_count() > peak) peak = bcache_dirty_count();
    }
    int persisted = 1;
    for (uint32_t i = 0; i < BCACHE_DIRTY_HIGH_WATER; i++) {
        if (!bytes_are(stores[0].sectors[i], 0x31)) persisted = 0;
    }
    KTEST_ASSERT(accepted && persisted && bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] high water writes every sector without an explicit flush");
    KTEST_ASSERT(peak == BCACHE_DIRTY_HIGH_WATER - 1,
                 "[BCACHE] write-behind batches writes until the threshold");
}

static void run_admission_assertions(void) {
    uint8_t buf[512];
    uint8_t readback[512];
    reset_devices();
    ft_memset(buf, 0x5A, sizeof(buf));

    KTEST_ASSERT(bcache_write_sector(&devs[0], BC_TEST_SECTORS, buf) == E_INVAL &&
                 bcache_write_sector(&devs[0], BC_NO_SECTOR, buf) == E_INVAL &&
                 bcache_read_sector(&devs[0], BC_TEST_SECTORS, buf) == E_INVAL,
                 "[STRICT] [BCACHE] boundary and maximum LBAs are rejected at admission");
    KTEST_ASSERT(bcache_write_sector(0, 0, buf) == E_NODEV &&
                 bcache_read_sector(0, 0, buf) == E_NODEV,
                 "[BCACHE] a missing device is rejected");
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, 0) == E_INVAL &&
                 bcache_read_sector(&devs[0], 0, 0) == E_INVAL,
                 "[STRICT] [BCACHE] null buffers on cache misses are rejected");
    devs[0].sector_size = 4096;
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_INVAL &&
                 bcache_read_sector(&devs[0], 0, buf) == E_INVAL,
                 "[STRICT] [BCACHE] oversized sectors never reach a 512-byte cache slot");
    devs[0].sector_size = 512;
    devs[0].write = 0;
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_ROFS,
                 "[STRICT] [BCACHE] a read-only device refuses writes before admission");
    devs[0].write = test_write;
    devs[0].read = 0;
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, buf) == E_NODEV,
                 "[BCACHE] a missing read handler is rejected without a null call");
    devs[0].read = test_read;
    KTEST_ASSERT(stores[0].reads == 0 && stores[0].writes == 0 &&
                 bcache_dirty_count() == 0 && bytes_are(buf, 0x5A),
                 "[STRICT] [BCACHE] rejected requests touch neither driver, buffer nor dirty state");

    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_OK,
                 "[BCACHE] a valid request still succeeds after rejection");
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, 0) == E_INVAL &&
                 bcache_write_sector(&devs[0], 0, 0) == E_INVAL,
                 "[STRICT] [BCACHE] null buffers are checked on cache hits too");
    devs[0].write = 0;
    ft_memset(buf, 0xA5, sizeof(buf));
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_ROFS,
                 "[BCACHE] cached data does not bypass write capability checks");
    devs[0].write = test_write;
    devs[0].sector_count = 0;
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, buf) == E_INVAL &&
                 bcache_write_sector(&devs[0], 0, buf) == E_INVAL,
                 "[BCACHE] cached data does not bypass current capacity checks");
    devs[0].sector_count = BC_TEST_SECTORS;
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, readback) == E_OK &&
                 bytes_are(readback, 0x5A) && bcache_dirty_count() == 1,
                 "[STRICT] [BCACHE] rejected overwrites preserve the original bytes");
}

static void run_failure_assertions(void) {
    uint8_t buf[512];
    reset_devices();
    stores[0].read_result = E_IO;
    ft_memset(buf, 0xA5, sizeof(buf));
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, buf) == E_IO && bytes_are(buf, 0),
                 "[BCACHE] a driver read error is reported and zeroes the output");
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, buf) == E_IO && stores[0].reads == 2,
                 "[STRICT] [BCACHE] failed reads are retried, never cached");
    stores[0].read_result = E_OK;

    /* These were all treated as permission to discard accepted data. Test
     * global flush and per-device flush separately, then recover every byte. */
    const int errors[] = { E_IO, E_NODEV, E_ROFS, E_NXIO, E_INVAL };
    for (uint32_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        ft_memset(buf, (int)(0x40 + i), sizeof(buf));
        KTEST_ASSERT(bcache_write_sector(&devs[0], i, buf) == E_OK,
                     "[BCACHE] data is accepted before the injected device failure");
        stores[0].write_result = errors[i];
        uint32_t writes = stores[0].writes;
        KTEST_ASSERT(bcache_flush() == E_IO && bcache_dirty_count() == 1 &&
                     stores[0].writes == writes + 1,
                     "[STRICT] [BCACHE] global flush preserves dirty data for every error class");
        KTEST_ASSERT(bcache_flush_dev(&devs[0]) == E_IO && bcache_dirty_count() == 1 &&
                     stores[0].writes == writes + 2,
                     "[STRICT] [BCACHE] failed detach retries and preserves the same sector");
        KTEST_ASSERT(bcache_read_sector(&devs[0], i, buf) == E_OK &&
                     bytes_are(buf, (uint8_t)(0x40 + i)),
                     "[STRICT] [BCACHE] failed writes leave the complete data readable");
        stores[0].write_result = E_OK;
        KTEST_ASSERT(bcache_flush_dev(&devs[0]) == E_OK && bcache_dirty_count() == 0 &&
                     bytes_are(stores[0].sectors[i], (uint8_t)(0x40 + i)),
                     "[STRICT] [BCACHE] recovery persists exactly the data that was retained");
    }
}

static void run_device_isolation_assertions(void) {
    uint8_t buf[512];
    reset_devices();
    ft_memset(buf, 0xA1, sizeof(buf));
    int a = bcache_write_sector(&devs[0], 7, buf);
    ft_memset(buf, 0xB2, sizeof(buf));
    int b = bcache_write_sector(&devs[1], 7, buf);
    KTEST_ASSERT(a == E_OK && b == E_OK && bcache_dirty_count() == 2,
                 "[BCACHE] the same LBA on two devices occupies distinct dirty slots");
    KTEST_ASSERT(bcache_read_sector(&devs[0], 7, buf) == E_OK && bytes_are(buf, 0xA1),
                 "[BCACHE] first device reads only its own bytes");
    KTEST_ASSERT(bcache_read_sector(&devs[1], 7, buf) == E_OK && bytes_are(buf, 0xB2),
                 "[BCACHE] second device reads only its own bytes");
    stores[0].write_result = E_NODEV;
    KTEST_ASSERT(bcache_flush_dev(&devs[1]) == E_OK && bcache_dirty_count() == 1 &&
                 stores[0].writes == 0 && bytes_are(stores[1].sectors[7], 0xB2),
                 "[STRICT] [BCACHE] flushing one device cannot write or forget the other");
    KTEST_ASSERT(bcache_flush() == E_IO && bcache_dirty_count() == 1,
                 "[BCACHE] a detached device's data remains pending");
    stores[0].write_result = E_OK;
    KTEST_ASSERT(bcache_flush_dev(&devs[0]) == E_OK &&
                 bytes_are(stores[0].sectors[7], 0xA1) &&
                 bytes_are(stores[1].sectors[7], 0xB2),
                 "[STRICT] [BCACHE] recovery writes both patterns to the correct device");
}

static void run_exhaustion_assertions(void) {
    uint8_t buf[512];
    reset_devices();
    stores[0].write_result = E_IO;
    ft_memset(buf, 0xCC, sizeof(buf));
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_OK,
                 "[BCACHE] the oldest slot can hold an unwritable sector");
    int served = 1;
    for (uint32_t i = 0; i < BCACHE_SIZE + 1; i++) {
        if (bcache_read_sector(&devs[1], i, buf) != E_OK) served = 0;
    }
    KTEST_ASSERT(served && bcache_dirty_count() == 1 && stores[0].writes == 0,
                 "[STRICT] [BCACHE] clean slots remain usable behind an unwritable oldest slot");

    reset_devices();
    stores[0].write_result = E_IO;
    int accepted = 1;
    for (uint32_t i = 0; i < BCACHE_SIZE; i++) {
        ft_memset(buf, (int)(i + 1), sizeof(buf));
        if (bcache_write_sector(&devs[0], i, buf) != E_OK) accepted = 0;
    }
    KTEST_ASSERT(accepted && bcache_dirty_count() == BCACHE_SIZE,
                 "[BCACHE] failed high-water flushes retain every accepted sector");
    stores[0].writes = 0;
    KTEST_ASSERT(bcache_write_sector(&devs[1], 0, buf) == E_IO &&
                 stores[0].writes == BCACHE_SIZE && bcache_dirty_count() == BCACHE_SIZE,
                 "[STRICT] [BCACHE] exhaustion attempts each slot once and refuses new data");
    stores[0].writes = 0;
    KTEST_ASSERT(bcache_read_sector(&devs[1], 0, buf) == E_IO &&
                 stores[0].writes == BCACHE_SIZE && stores[1].reads == 0,
                 "[STRICT] [BCACHE] an exhausted read miss is bounded and never reaches its driver");

    /* Keep the oldest sector failing, but allow the next one to reach storage. */
    stores[0].write_ok_lba = 1;
    stores[0].writes = 0;
    KTEST_ASSERT(bcache_read_sector(&devs[1], 0, buf) == E_OK &&
                 stores[0].writes == 2 && bcache_dirty_count() == BCACHE_SIZE - 1 &&
                 bytes_are(stores[0].sectors[1], 2),
                 "[STRICT] [BCACHE] a later writable dirty slot is tried after a failed oldest slot");
    int intact = 1;
    for (uint32_t i = 0; i < BCACHE_SIZE; i++) {
        if (i == 1) continue; /* This one is now on disk, not in cache. */
        if (bcache_read_sector(&devs[0], i, buf) != E_OK ||
            !bytes_are(buf, (uint8_t)(i + 1))) intact = 0;
    }
    KTEST_ASSERT(intact, "[STRICT] [BCACHE] all failed eviction candidates retain all their bytes");
    stores[0].write_result = E_OK;
    int flushed = bcache_flush_dev(&devs[0]);
    int persisted = 1;
    for (uint32_t i = 0; i < BCACHE_SIZE; i++) {
        if (!bytes_are(stores[0].sectors[i], (uint8_t)(i + 1))) persisted = 0;
    }
    KTEST_ASSERT(flushed == E_OK && persisted && bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] every accepted byte survives exhaustion and recovery");
}

static int dispatch(uint32_t number, uint32_t arg) {
    arch_regs_t regs;
    ft_bzero(&regs, sizeof(regs));
    regs.eax = number;
    regs.ebx = arg;
    syscall_handler(&regs);
    return (int)regs.eax;
}

static void run_syscall_failure_assertions(void) {
    uint8_t buf[512];
    reset_devices();
    blockdev_t *saved_root = blockdev_root();
    int saved_mounted = fs_mounted;
    uint32_t saved_uid = current_task->uid;
    current_task->uid = 0;
    ft_memset(buf, 0xD3, sizeof(buf));
    KTEST_ASSERT(bcache_write_sector(&devs[0], 0, buf) == E_OK,
                 "[BCACHE] syscall failure test starts with accepted data");
    stores[0].write_result = E_NODEV;
    KTEST_ASSERT(dispatch(SYSCALL_SYNC, 0) == E_IO,
                 "[STRICT] [BCACHE] sync reports a device write failure to its caller");
    KTEST_ASSERT(dispatch(SYSCALL_REBOOT, 0) == E_IO,
                 "[STRICT] [BCACHE] reboot refuses to discard data after a failed flush");
    KTEST_ASSERT(dispatch(SYSCALL_HALT, 0) == E_IO,
                 "[STRICT] [BCACHE] halt refuses to discard data after a failed flush");
    KTEST_ASSERT(dispatch(SYSCALL_UMOUNT, 0) == E_IO &&
                 blockdev_root() == saved_root && fs_mounted == saved_mounted,
                 "[STRICT] [BCACHE] unmount preserves the mount when any device has dirty data");

    /* The runner maps this user scratch page for syscall arguments. Save it so
     * this module does not depend on what another module left there. */
    char *user_name = (char *)0x500000;
    char saved_name[32];
    ft_memcpy(saved_name, user_name, sizeof(saved_name));
    ft_strcpy(user_name, "usb0");
    KTEST_ASSERT(dispatch(SYSCALL_MOUNT, (uint32_t)user_name) == E_IO &&
                 blockdev_root() == saved_root && fs_mounted == saved_mounted,
                 "[STRICT] [BCACHE] switching disks cannot reset a cache with failed writes");
    ft_memcpy(user_name, saved_name, sizeof(saved_name));
    current_task->uid = saved_uid;
    KTEST_ASSERT(bcache_read_sector(&devs[0], 0, buf) == E_OK && bytes_are(buf, 0xD3),
                 "[STRICT] [BCACHE] refused lifecycle operations leave the data recoverable");
    stores[0].write_result = E_OK;
    KTEST_ASSERT(dispatch(SYSCALL_SYNC, 0) == E_OK &&
                 bytes_are(stores[0].sectors[0], 0xD3) && bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] sync succeeds and persists the data after recovery");
}

void run_bcache_tests(void) {
    printk("\n--- Block Cache (bcache) Tests ---\n");
    blockdev_t *saved_root = blockdev_root();
    KTEST_ASSERT(bcache_flush() == E_OK, "[BCACHE] pre-existing filesystem writes are flushed");
    run_policy_assertions();
    run_failure_assertions();
    run_device_isolation_assertions();
    run_exhaustion_assertions();
    run_admission_assertions();
    run_syscall_failure_assertions();
    reset_devices();
    KTEST_ASSERT(blockdev_root() == saved_root && bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] the root is unchanged and no fake dirty data survives");
}
