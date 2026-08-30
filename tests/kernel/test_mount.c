/*
 * File: test_mount.c
 * Purpose: Two disks, and the cache that has to tell them apart.
 *
 * This file is part of the esdumanOS test suite.
 *
 * The dangerous part of this release is not the registry and not the syscall -
 * it is that the block cache keyed a slot on the sector number alone. That was
 * correct while there was one disk and silently wrong the moment there were two:
 * sector 4000 of the root and sector 4000 of the stick would land in the same
 * slot, and the second reader would be handed the first one's data. Write-back
 * makes it worse than a bad read, because the slot is eventually written out -
 * to whichever device the code believed it belonged to.
 *
 * A plumbing fault is loud. That one is silent, and it is what the middle of
 * this module exists to catch.
 *
 * This touches shared state - the root device, and cache slots on two real disks
 * - so it puts both back and says so in an assertion rather than in a comment.
 * test_blockdev.c has kept that discipline since v1.2.0.
 */
#include "ktest.h"
#include "blockdev.h"
#include "bcache.h"
#include "usbmsc.h"
#include "errno.h"
#include "libft.h"

/*
 * High enough to be past anything the file system uses on either disk, and low
 * enough to exist on both of them - which is the half that was missing. This was
 * 4200, chosen as "past everything", and 4200 is past the end of the test images
 * as well: the root is 4096 sectors and the stick was 2048. Both writes were
 * refused by the block layer's bounds check, both slots stayed dirty forever,
 * and because a slot nothing can write is also never touched again it became the
 * least recently used one - so every eviction for the rest of the boot picked it,
 * failed, and returned no slot at all. Two sectors nobody needed stopped the
 * whole cache.
 *
 * 4050 sits inside both images and clear of the 4000-4032 range test_bcache.c
 * dirties, in the same region that file documents as untouched by a boot. The
 * assertion below is what keeps it true if an image is ever resized.
 */
#define MT_TEST_SECTOR 4050

/**
 * @brief The table, and what it refuses.
 */
static void run_registry_assertions(void) {
    KTEST_ASSERT(blockdev_count() >= 2 && blockdev_count() <= BLOCKDEV_MAX,
                 "[STRICT] [MOUNT] this machine has registered more than one disk");

    blockdev_t *root = blockdev_root();

    KTEST_ASSERT(root != 0 && blockdev_find(root->name) == root,
                 "[STRICT] [MOUNT] the mounted disk is findable by the name it published");

    KTEST_ASSERT(blockdev_find("no-such-disk") == 0 && blockdev_find("") == 0 &&
                 blockdev_find(0) == 0,
                 "[STRICT] [MOUNT] a name nothing answers to finds nothing, and so does no name");

    /*
     * A second device may not take a name that is already spoken for. That is
     * not tidiness: the name is how mount() is told which disk to use, and two
     * of them would make the instruction ambiguous - resolved by whichever
     * happened to register first, which is not a rule anybody could rely on.
     */
    blockdev_t clash = *root;

    KTEST_ASSERT(blockdev_register(&clash) == E_EXIST,
                 "[STRICT] [MOUNT] and a second device may not take a name already in use");

    /*
     * Registering the same device twice is not the same thing and must not fail:
     * a driver that is asked to come up again should not have to remember
     * whether it already did.
     */
    KTEST_ASSERT(blockdev_register(root) == E_OK,
                 "[STRICT] [MOUNT] while registering the same device again is simply nothing to do");

    blockdev_t broken = *root;

    broken.name = "broken0";
    broken.read = 0;
    KTEST_ASSERT(blockdev_register(&broken) == E_INVAL,
                 "[STRICT] [MOUNT] a device that cannot read is refused at the table rather than at the first mount");
}

/**
 * @brief The assertion this release turns on.
 *
 * The same sector number on two devices must be two different sectors. Before
 * the cache carried a device it was one, and nothing anywhere would have said
 * so.
 */
static void run_cache_separation_assertions(void) {
    blockdev_t *root = blockdev_root();
    blockdev_t *usb = blockdev_find("usb0");

    if (root == 0 || usb == 0 || usb == root) {
        KTEST_ASSERT(0, "[STRICT] [MOUNT] the sector these assertions use exists on both disks");
        KTEST_ASSERT(0, "[STRICT] [BCACHE] the same sector on two devices holds two different things");
        KTEST_ASSERT(0, "[STRICT] [BCACHE] and each reads back what was written to it, not to the other");
        return;
    }

    /*
     * Asked before anything is written, and asked of the devices rather than of
     * the Makefile. A sector past the end of either one is refused by the block
     * layer, which leaves a dirty slot the cache can never place - and the
     * symptom of that is not a failed assertion here but an eviction path that
     * stops working for every module after this one. The cache no longer hangs
     * on it, but a test whose sector does not exist proves nothing either way,
     * so it is caught at the top rather than diagnosed at the bottom.
     */
    KTEST_ASSERT(root->sector_count > MT_TEST_SECTOR &&
                 usb->sector_count > MT_TEST_SECTOR,
                 "[STRICT] [MOUNT] the sector these assertions use exists on both disks");

    static uint8_t wa[512];
    static uint8_t wb[512];
    static uint8_t ra[512];
    static uint8_t rb[512];

    ft_memset(wa, 0xA1, sizeof(wa));
    ft_memset(wb, 0xB2, sizeof(wb));

    bcache_write_sector(root, MT_TEST_SECTOR, wa);
    bcache_write_sector(usb, MT_TEST_SECTOR, wb);

    ft_memset(ra, 0, sizeof(ra));
    ft_memset(rb, 0, sizeof(rb));

    bcache_read_sector(root, MT_TEST_SECTOR, ra);
    bcache_read_sector(usb, MT_TEST_SECTOR, rb);

    KTEST_ASSERT(ra[0] != rb[0],
                 "[STRICT] [BCACHE] the same sector on two devices holds two different things");

    KTEST_ASSERT(ra[0] == 0xA1 && ra[511] == 0xA1 &&
                 rb[0] == 0xB2 && rb[511] == 0xB2,
                 "[STRICT] [BCACHE] and each reads back what was written to it, not to the other");

    /*
     * Written out and dropped, on both. Leaving two dirty slots behind would
     * hand the next module a flush it did not ask for, and leaving them valid
     * would leave this module's pattern in the cache for whatever reads those
     * sectors next.
     */
    bcache_flush_dev(root);
    bcache_flush_dev(usb);
}

/**
 * @brief Changing which disk is in use, and putting it back.
 *
 * The root is moved and restored without init_fs() being called on either side.
 * Re-mounting mid-suite would tear down the file system every module after this
 * one is standing on - the full cycle is a manual test, and saying so is better
 * than a test that proves it by breaking the run.
 */
static void run_root_switch_assertions(void) {
    blockdev_t *saved = blockdev_root();
    blockdev_t *usb = blockdev_find("usb0");

    if (saved == 0 || usb == 0) {
        KTEST_ASSERT(0, "[STRICT] [MOUNT] the root can be moved to another registered disk");
        KTEST_ASSERT(0, "[STRICT] [MOUNT] and detached entirely, which is what umount leaves behind");
        KTEST_ASSERT(0, "[STRICT] [MOUNT] and the disk the suite was using is back where it was");
        return;
    }

    KTEST_ASSERT(blockdev_set_root(usb) == E_OK && blockdev_root() == usb,
                 "[STRICT] [MOUNT] the root can be moved to another registered disk");

    KTEST_ASSERT(blockdev_set_root(0) == E_OK && blockdev_root() == 0,
                 "[STRICT] [MOUNT] and detached entirely, which is what umount leaves behind");

    blockdev_set_root(saved);

    KTEST_ASSERT(blockdev_root() == saved,
                 "[STRICT] [MOUNT] and the disk the suite was using is back where it was");
}

void run_mount_tests(void) {
    printk("\n--- Mount and Multi-Device Tests ---\n");

    run_registry_assertions();
    run_cache_separation_assertions();
    run_root_switch_assertions();
}
