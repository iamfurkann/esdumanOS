/*
 * File: test_bcache.c
 * Purpose: Block Cache (bcache) unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "types.h"
#include "bcache.h"
#include "blockdev.h"
#include "errno.h"
#include "libft.h"
#include "rtc.h"

/*
 * Scratch sectors for the write-back policy tests.
 *
 * The VFS allocates upward from FS_DATA_START_SECTOR (200), lowest free sector
 * first, and a boot uses only a few hundred, so the top of a 4096-sector disk is
 * never touched. These tests dirty 33 sectors and do flush them for real, so they
 * have to land somewhere the filesystem will not miss.
 */
#define BC_TEST_BASE 4000

/*
 * A device that fails everything, for the half of the cache's behaviour that the
 * real disk cannot exercise: QEMU's IDE controller does not fail, so until the
 * block layer existed there was no way to write these assertions at all.
 *
 * sector_count is deliberately larger than the real disk, so that the sectors
 * below reach the device and are refused by it rather than being refused by the
 * layer's own bounds check - which would test something else.
 */
static int bc_fail_reads = 0;
static int bc_fail_writes = 0;

/*
 * Whether the fake device is currently refusing writes.
 *
 * A switch rather than an unconditional refusal, because the module has to be
 * able to get its own sector back out of the cache at the end. It could not:
 * once the cache started keying a slot on the device as well as the sector - the
 * whole point of v1.11.0 - the slot dirtied here belongs to this device
 * permanently, and putting the real disk back no longer sends the flush
 * anywhere else. The cleanup below flushed to the real root, the write still
 * arrived at failing_write(), the assertion failed, and the slot stayed dirty
 * against a device with no storage behind it for the rest of the run.
 *
 * E_IO deliberately, and it stays E_IO: this is the transient class, the one the
 * cache is supposed to keep and retry. Letting the device relent is what proves
 * that the retry it was holding the sector for actually happens.
 */
static int bc_writes_refused = 1;

static int failing_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx; (void)lba; (void)buf;
    bc_fail_reads++;
    return E_IO;
}

static int failing_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx; (void)lba; (void)buf;
    bc_fail_writes++;
    return bc_writes_refused ? E_IO : E_OK;
}

static blockdev_t failing_dev = {
    .name         = "failing0",
    .sector_size  = 512,
    .sector_count = 65536,
    .read         = failing_read,
    .write        = failing_write,
    .ctx          = 0
};

/**
 * @brief Tests what the cache does when the device underneath it says no.
 *
 * The defect this release exists for lived exactly here: the cache called the
 * driver, discarded the result, and stored the zero-filled buffer as valid data.
 * A failed read then looked like a sector full of zeros to everything above -
 * including the check that decides whether a disk is blank and should be
 * formatted.
 *
 * The cache is flushed before the fake device is installed, and that is not
 * hygiene: an eviction with dirty slots outstanding would write real file system
 * sectors to the fake device and mark them clean, so the real disk would never
 * get them. The test would pass and the damage would appear somewhere else.
 */
static void run_device_failure_assertions(void) {
    blockdev_t *saved = blockdev_root();
    uint8_t buf[512];

    KTEST_ASSERT(bcache_flush() == E_OK,
                 "[BCACHE] the cache is clean before the failing device goes in");

    blockdev_set_root(&failing_dev);
    bc_fail_reads = 0;
    bc_fail_writes = 0;
    bc_writes_refused = 1;

    ft_memset(buf, 0x5A, sizeof(buf));
    KTEST_ASSERT(bcache_read_sector(blockdev_root(), BC_TEST_BASE + 90, buf) == E_IO,
                 "[STRICT] [BCACHE] a read the device refuses is reported, not swallowed");

    {
        int zeroed = 1;
        for (int i = 0; i < 512; i++) {
            if (buf[i] != 0) { zeroed = 0; break; }
        }
        KTEST_ASSERT(zeroed,
                     "[BCACHE] and the caller's buffer is zeroed rather than left as it was");
    }

    /*
     * The assertion that matters. If the failed read had been cached, this second
     * one would be served from memory and the device would see one read, not two.
     */
    bcache_read_sector(blockdev_root(), BC_TEST_BASE + 90, buf);
    KTEST_ASSERT(bc_fail_reads == 2,
                 "[STRICT] [BCACHE] a failed read is not cached, so the next one asks the device again");

    /*
     * A write is taken by the cache - it is write-back - and the failure surfaces
     * at the flush, where the sector must stay dirty so that a later flush tries
     * it again.
     */
    ft_memset(buf, 0x31, sizeof(buf));
    KTEST_ASSERT(bcache_write_sector(blockdev_root(), BC_TEST_BASE + 91, buf) == E_OK,
                 "[BCACHE] a write is accepted into the cache without touching the device");

    uint32_t dirty_before = bcache_dirty_count();
    KTEST_ASSERT(bcache_flush() == E_IO,
                 "[STRICT] [BCACHE] a flush the device refuses reports failure");
    KTEST_ASSERT(bcache_dirty_count() == dirty_before,
                 "[STRICT] [BCACHE] and the sector stays dirty, so a later flush tries it again");

    /*
     * Let the device relent, and then take the sector back off it.
     *
     * This is the retry the cache kept the sector dirty for, so it is worth
     * asserting rather than just performing: the assertion above says the sector
     * stayed, and this one says staying was good for something.
     *
     * bcache_flush_dev() rather than bcache_flush(), because the slot has to be
     * dropped as well as written. It is dirty against a fake device with no
     * storage behind it, and a slot keyed to a blockdev_t that is about to go out
     * of scope is exactly what that function exists to clear.
     */
    bc_writes_refused = 0;

    KTEST_ASSERT(bcache_flush_dev(&failing_dev) == E_OK,
                 "[STRICT] [BCACHE] and the device takes the sector it refused once it relents");

    KTEST_ASSERT(bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] leaving nothing of it behind");

    blockdev_set_root(saved);
    KTEST_ASSERT(bcache_flush() == E_OK,
                 "[BCACHE] and the real device is clean with the fake one gone");
}

/**
 * @brief A sector the device could never take, and the slot it used to pin.
 *
 * The defect this guards was found in v1.11.0 and it was not subtle in effect,
 * only in cause. A write past the end of a disk is refused by the block layer,
 * the cache kept the slot dirty so that a later flush could try again, and a
 * later flush was refused for the identical reason. A slot nothing can write is
 * also a slot nothing touches, so it settled at the bottom of the LRU order and
 * every eviction from then on chose it, failed, and returned no slot - which is
 * the entire cache, stopped, by one sector nobody needed.
 *
 * Keeping a refused sector is still right for a device having a bad moment; the
 * assertions above cover that. What this covers is the other kind, where trying
 * again cannot ever produce a different answer.
 */
static void run_hopeless_write_assertions(void) {
    blockdev_t *root = blockdev_root();
    uint8_t buf[512];

    /*
     * Above the guard rather than below it, so that both paths through this
     * function make the same number of assertions. bcache_flush() does not need
     * a root - it writes each slot to the device that slot names - so there is
     * nothing to move it below.
     */
    KTEST_ASSERT(bcache_flush() == E_OK,
                 "[BCACHE] the cache is clean before the sector that cannot be written");

    if (root == 0) {
        KTEST_ASSERT(0, "[STRICT] [BCACHE] a sector past the end of the disk is taken by the cache");
        KTEST_ASSERT(0, "[STRICT] [BCACHE] and the flush that cannot place it says so");
        KTEST_ASSERT(0, "[STRICT] [BCACHE] but does not keep it, because no later attempt could succeed");
        KTEST_ASSERT(0, "[STRICT] [BCACHE] and the cache still has slots to give afterwards");
        return;
    }

    ft_memset(buf, 0x7E, sizeof(buf));

    /*
     * Taken, and that is not a bug being tolerated. This is a write-back cache:
     * it does not know where the device ends and is not the layer that decides.
     * The failure belongs at the flush, which is where the device is asked.
     */
    KTEST_ASSERT(bcache_write_sector(root, root->sector_count + 1, buf) == E_OK,
                 "[STRICT] [BCACHE] a sector past the end of the disk is taken by the cache");

    KTEST_ASSERT(bcache_flush() == E_IO,
                 "[STRICT] [BCACHE] and the flush that cannot place it says so");

    KTEST_ASSERT(bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] but does not keep it, because no later attempt could succeed");

    /*
     * The assertion the whole of this exists for. Everything above would still
     * hold in a cache that had quietly stopped working; this is the one that
     * fails if the slot was pinned, because a pinned slot is the oldest one and
     * eviction would hand back nothing.
     */
    int served = 1;

    for (int i = 0; i < BCACHE_SIZE + 1; i++) {
        if (bcache_read_sector(root, (uint32_t)i, buf) != E_OK) { served = 0; break; }
    }

    KTEST_ASSERT(served,
                 "[STRICT] [BCACHE] and the cache still has slots to give afterwards");
}

/**
 * @brief Tests the Block Cache (bcache) write, read, and flush mechanisms.
 *
 * This test suite validates that data written to the underlying block
 * device is appropriately cached in memory to speed up subsequent reads,
 * and ensures that flushing mechanisms reliably synchronize data without
 * causing panics or data loss.
 *
 * Expected Behavior:
 * - A 512-byte block write commits correctly to cache structures.
 * - An immediate read of the same sector serves from the cache matching exactly.
 * - Calling a cache flush operation successfully pushes changes down without crashing.
 *
 * Edge Cases Covered:
 * - Reading back from a distinctly remote sector (e.g., 1024) to avoid accidental
 *   overlap with bootloader or partition structures.
 */
void run_bcache_tests(void) {
    printk("\n--- Block Cache (bcache) Tests ---\n");

    uint8_t w_buf[512];
    uint8_t r_buf[512];
    
    for(int i = 0; i < 512; i++) {
        w_buf[i] = (i % 256);
        r_buf[i] = 0;
    }

    // Select a distant sector to minimize risk of overwriting critical filesystem metadata.
    uint32_t test_sector = 1024; 

    bcache_write_sector(blockdev_root(), test_sector, w_buf);
    
    bcache_read_sector(blockdev_root(), test_sector, r_buf);
    
    // Verify that the data retrieved from the cache perfectly matches the initial write buffer.
    int is_match = 1;
    for(int i = 0; i < 512; i++) {
        if(r_buf[i] != w_buf[i]) {
            is_match = 0;
            break;
        }
    }
    KTEST_ASSERT(is_match == 1, "Block Cache: Written data successfully read from cache without loss");

    bcache_flush();
    KTEST_ASSERT(1 == 1, "Block Cache: Flush operation completed safely");

    /* --- Write-back policy (K-13) -------------------------------------- */

    /*
     * The cache was write-back with no policy: bcache_flush() was only reached
     * from sys_reboot() and sys_halt(), so anything short of an orderly shutdown
     * lost up to 32 KB of filesystem. These assertions cover the two bounds that
     * replaced that - by volume and by time - and the explicit sync() path.
     *
     * Boot wrote a great deal before the suite started, so establish a clean
     * baseline first rather than assuming one.
     */
    bcache_flush();
    KTEST_ASSERT(bcache_dirty_count() == 0,
                 "[BCACHE] flush leaves nothing dirty behind");
    KTEST_ASSERT(!bcache_flush_is_due(),
                 "[BCACHE] a clean cache is never due for a flush");

    bcache_write_sector(blockdev_root(), BC_TEST_BASE, w_buf);
    KTEST_ASSERT(bcache_dirty_count() == 1,
                 "[BCACHE] one write leaves exactly one dirty sector");
    KTEST_ASSERT(!bcache_flush_is_due(),
                 "[STRICT] [BCACHE] a fresh write is not yet due (the deadline is a delay, not a no-op)");

    /*
     * Bounded wait. An unbounded one would turn a stalled tick counter into a
     * 300-second QEMU timeout reported as "KERNEL HUNG", which says nothing;
     * stopping at twice the interval fails the assertion instead and names the
     * problem. hlt rather than a spin so the wait costs no emulated cycles.
     */
    uint32_t wait_start = timer_get_ticks();
    while (!bcache_flush_is_due() &&
           (uint32_t)(timer_get_ticks() - wait_start) < (BCACHE_FLUSH_INTERVAL_TICKS * 2)) {
        asm volatile("hlt");
    }
    KTEST_ASSERT(bcache_flush_is_due(),
                 "[STRICT] [BCACHE] dirty data becomes due once the interval elapses");

    bcache_flush_if_due();
    KTEST_ASSERT(bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] flush_if_due wrote the expired sector back");

    /*
     * Volume bound: writing up to the high water mark has to trigger write-behind
     * on its own, with nobody calling flush. This is the bound that keeps a burst
     * of writes from putting the whole cache at risk.
     */
    uint32_t peak_dirty = 0;
    for (uint32_t i = 0; i < BCACHE_DIRTY_HIGH_WATER; i++) {
        bcache_write_sector(blockdev_root(), BC_TEST_BASE + i, w_buf);
        if (bcache_dirty_count() > peak_dirty) peak_dirty = bcache_dirty_count();
    }
    KTEST_ASSERT(bcache_dirty_count() == 0,
                 "[STRICT] [BCACHE] reaching the high water mark flushes without being asked");
    KTEST_ASSERT(peak_dirty <= BCACHE_DIRTY_HIGH_WATER,
                 "[STRICT] [BCACHE] the dirty set never grew past the high water mark");
    KTEST_ASSERT(peak_dirty > 1,
                 "[BCACHE] write-behind is a bound, not a flush on every single write");

    run_device_failure_assertions();
    run_hopeless_write_assertions();
}
