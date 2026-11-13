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

#include "fs.h"

#define RAM_SECTORS 544u
static uint8_t media[RAM_SECTORS][512];
static int fail_read, fail_write, write_count;
static int ram_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx;
    if (fail_read || lba >= RAM_SECTORS) return E_IO;
    ft_memcpy(buf, media[lba], 512);
    return E_OK;
}
static int ram_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx;
    if (fail_write || lba >= RAM_SECTORS) return E_IO;
    ft_memcpy(media[lba], buf, 512);
    write_count++;
    return E_OK;
}
static blockdev_t ram_disk = {
    .name = "ktram14", .sector_size = 512, .sector_count = RAM_SECTORS,
    .read = ram_read, .write = ram_write
};

/* Only test builds register this device. All destructive tests address it
 * by its unique name, never by physical discovery order. */
int ktest_prepare_mount_disk(void) {
    if (fs_secondary_device() == &ram_disk) return E_BUSY;
    fail_read = fail_write = 0;
    if (bcache_flush_dev(&ram_disk) != E_OK) return E_IO;
    ft_memset(media, 0, sizeof(media));
    write_count = 0;
    return blockdev_register(&ram_disk);
}

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

/* Restore a formatted fixture between corruptions. Only clean, detached
 * cache entries are invalidated before touching the RAM backing store. */
static uint8_t pristine[RAM_SECTORS][512];
static void restore_fixture(void) {
    bcache_flush_dev(&ram_disk);
    ft_memcpy(media, pristine, sizeof(media));
}

static int rejects_corruption(const char *pass) {
    int before = write_count;
    int rc = fs_attach(&ram_disk, pass);
    int rejected = rc == E_INVAL && !fs_secondary_device() && write_count == before;
    if (rc == E_OK) fs_detach();
    return rejected;
}

static void run_media_validation(const char *pass) {
    bcache_flush_dev(&ram_disk);
    ft_memcpy(pristine, media, sizeof(media));
    disk_superblock_t *super = (disk_superblock_t *)media[FS_PART_START_LBA];
    uint32_t dir_lba = FS_PART_START_LBA + super->dir_start;
    uint32_t fat_lba = FS_PART_START_LBA + super->fat_start;
    disk_file_entry_t *entries = (disk_file_entry_t *)media[dir_lba];
    uint32_t *fat = (uint32_t *)media[fat_lba];

    super->fat_start = super->dir_start;
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] overlapping directory and FAT are rejected without writes");
    restore_fixture();
    super->total_clusters = FS_MAX_CLUSTERS_CAP;
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] allocation geometry cannot extend beyond the partition");
    restore_fixture();
    super->max_entries = 0;
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] zero directory entries are rejected before allocation");
    restore_fixture();
    entries[1].is_used = 1;
    entries[1].entry_id = 1;
    ft_memset(entries[1].filename, 'x', MAX_FILENAME);
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] unterminated disk names never reach string lookup");
    restore_fixture();
    entries[1].is_used = entries[2].is_used = 1;
    entries[1].entry_id = 1;
    entries[2].entry_id = 2;
    ft_strcpy(entries[1].filename, "duplicate");
    ft_strcpy(entries[2].filename, "duplicate");
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] duplicate names cannot make lookup ambiguous");
    restore_fixture();
    entries[1].is_used = 1;
    entries[1].entry_id = 1;
    entries[1].file_type = FT_DIR;
    entries[1].parent_id = 1;
    ft_strcpy(entries[1].filename, "cycle");
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] cyclic parents leave both namespace and disk unchanged");
    restore_fixture();
    fat[2] = FAT_EOF;
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] an allocated cluster with no owner is rejected");
    restore_fixture();
    entries[1].is_used = 1;
    entries[1].entry_id = 1;
    entries[1].file_size = 4097;
    entries[1].start_cluster = 2;
    ft_strcpy(entries[1].filename, "cycle");
    fat[2] = 2;
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] cyclic file chains are rejected before unlock");
    fat[2] = FAT_EOF;
    bcache_flush_dev(&ram_disk);
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] a file length beyond its chain is rejected");
    entries[1].file_size = 1;
    entries[2] = entries[1];
    entries[2].entry_id = 2;
    ft_strcpy(entries[2].filename, "shared");
    bcache_flush_dev(&ram_disk);
    KTEST_ASSERT(rejects_corruption(pass), "[MOUNT] two files cannot own the same data cluster");
    restore_fixture();
}

static void run_live_volume_assertions(fs_id_t id) {
    const uint8_t primary[] = "root", secondary[] = "data";
    int created = fs_create_file("volume-slot.txt", primary, 4, 0);
    int slot = fs_get_entry_idx("volume-slot.txt", 0);
    fs_volume_t *v = fs_volume_for(id);
    int padded = v && slot > 0 && slot < v->entry_count;
    for (int i = 1; padded && i < slot; i++) {
        char name[32];
        kbprintf(name, sizeof(name), 0, "pad-%d", i);
        if (fs_mkdir(name, id) != E_OK) padded = 0;
    }
    int data = padded ? fs_create_file("volume-slot.txt", secondary, 4, id) : E_IO;
    KTEST_ASSERT(created == E_OK && data == E_OK && fs_get_entry_idx("volume-slot.txt", id) == slot,
                 "[MOUNT] fixture uses exactly the same local file slot on both disks");
    vfs_file_t root_file = {0}, data_file = {0};
    uint8_t a[4] = {0}, b[4] = {0};
    KTEST_ASSERT(fs_open("volume-slot.txt", 0, &root_file) == E_OK &&
                 fs_open("volume-slot.txt", id, &data_file) == E_OK &&
                 fs_read(&root_file, a, sizeof(a)) == 4 && fs_read(&data_file, b, sizeof(b)) == 4 &&
                 ft_memcmp(a, primary, 4) == 0 && ft_memcmp(b, secondary, 4) == 0,
                 "[MOUNT] matching local slots read through independent devices and keys");
    int sentinel = fs_create_file("volume-slot.txt.tmp", secondary, 4, 0);
    int updated = fs_atomic_update("volume-slot.txt", secondary, 4, 0);
    KTEST_ASSERT(sentinel == E_OK && updated == E_OK &&
                 fs_open("volume-slot.txt.tmp", 0, &root_file) == E_OK &&
                 fs_read(&root_file, a, sizeof(a)) == 4 && ft_memcmp(a, secondary, 4) == 0,
                 "[MOUNT] atomic replacement preserves a pre-existing .tmp file");
    fs_delete("volume-slot.txt.tmp", 0);
    fs_delete("volume-slot.txt", 0);

    fail_write = 1;
    int pending = fs_mkdir("retry-dir", id);
    KTEST_ASSERT(pending == E_IO && v && v->directory_pending,
                 "[MOUNT] metadata cache exhaustion reports failure and retains pending data");
    KTEST_ASSERT(fs_sync_all() == E_IO && fs_detach() == E_IO && fs_volume_for(id) == v,
                 "[MOUNT] sync and detach cannot discard metadata rejected by the device");
    fail_write = 0;
    KTEST_ASSERT(fs_sync_all() == E_OK && v && !v->directory_pending &&
                 fs_child_dir(id, "retry-dir") > 0,
                 "[MOUNT] sync retries every pending metadata sector after recovery");
}

static void run_volume_assertions(void) {
    blockdev_t *root = blockdev_root();
    const char *pass = "volume-test";
    KTEST_ASSERT(ktest_prepare_mount_disk() == E_OK, "[MOUNT] disposable RAM device available");
    KTEST_ASSERT(fs_attach(&ram_disk, pass) == E_INVAL && write_count == 0,
                 "[MOUNT] mounting a blank disk leaves it byte-for-byte untouched");
    media[RAM_SECTORS - 1][511] = 0x7e;
    KTEST_ASSERT(fs_format_data_device(&ram_disk, RAM_SECTORS, pass) == E_EXIST &&
                 write_count == 0 && media[RAM_SECTORS - 1][511] == 0x7e,
                 "[MOUNT] data beyond a zero header prevents formatting");
    bcache_flush_dev(&ram_disk);
    media[RAM_SECTORS - 1][511] = 0;
    fail_read = 1;
    KTEST_ASSERT(fs_format_data_device(&ram_disk, RAM_SECTORS, pass) == E_IO && write_count == 0,
                 "[MOUNT] an unreadable device cannot be formatted");
    fail_read = 0;
    KTEST_ASSERT(fs_format_data_device(&ram_disk, RAM_SECTORS - 1, pass) == E_INVAL &&
                 write_count == 0, "[MOUNT] provisioning requires the expected capacity");
    KTEST_ASSERT(fs_format_data_device(&ram_disk, RAM_SECTORS, pass) == E_OK,
                 "[MOUNT] an explicit format prepares a blank RAM disk");
    run_media_validation(pass);
    int before_mount = write_count;
    KTEST_ASSERT(fs_attach(&ram_disk, "incorrect") == E_ACCES &&
                 !fs_secondary_device() && write_count == before_mount,
                 "[MOUNT] rejected key leaves the root and target unchanged");
    int rc = fs_attach(&ram_disk, pass);
    KTEST_ASSERT(rc == E_OK && fs_secondary_device() == &ram_disk &&
                 write_count == before_mount && blockdev_root() == root,
                 "[MOUNT] attaching an existing v3 volume performs no writes");
    if (rc != E_OK) {
        KTEST_ASSERT(0, "[MOUNT] qualified identity requires a successful attach");
        for (int i = 0; i < 6; i++)
            KTEST_ASSERT(0, "[MOUNT] live-volume regression requires a successful attach");
        KTEST_ASSERT(0, "[MOUNT] dirty fixture requires a successful attach");
        KTEST_ASSERT(0, "[MOUNT] failed detach requires a successful attach");
        KTEST_ASSERT(0, "[MOUNT] retry requires a successful attach");
        return;
    }
    int id = fs_child_dir(0, "mnt");
    KTEST_ASSERT(id > 0xffff && fs_parent_dir((fs_id_t)id) == 0,
                 "[MOUNT] mounted root has a qualified ID and a namespace parent");
    run_live_volume_assertions((fs_id_t)id);
    uint8_t marker[512]; ft_memset(marker, 0x5a, sizeof(marker));
    KTEST_ASSERT(bcache_write_sector(&ram_disk, RAM_SECTORS - 1, marker) == E_OK,
                 "[MOUNT] fixture has one accepted dirty sector");
    fail_write = 1;
    KTEST_ASSERT(fs_detach() == E_IO && fs_secondary_device() == &ram_disk &&
                 fs_child_dir(0, "mnt") == id, "[MOUNT] failed detach retains the same live volume");
    fail_write = 0;
    KTEST_ASSERT(fs_detach() == E_OK && !fs_volume_for(id) &&
                 media[RAM_SECTORS - 1][0] == 0x5a && blockdev_root() == root,
                 "[MOUNT] retry persists data, invalidates its handle and preserves the root");
}
void run_mount_tests(void) {
    printk("\n--- Mount and Multi-Device Tests ---\n");
    run_registry_assertions();
    run_volume_assertions();
}
