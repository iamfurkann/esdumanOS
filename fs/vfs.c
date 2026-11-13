/*
 * File: vfs.c
 * Purpose: Virtual File System implementation handling file operations and FAT.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "fs.h"
#include "blockdev.h"
#include "libft.h"
#include "stdio.h"
#include "errno.h"
#include "klog.h"
#include "security.h"
#include "crypto.h"
#include "process.h"
#include "bcache.h"
#include "kheap.h"
#include "esdtime.h"
#include "rtc.h"
#include "entropy.h"
fs_volume_t fs_root_volume;
static fs_volume_t secondary_volume;

/* A handle from a detached volume must never resolve against its successor. */
fs_volume_t *fs_volume_for(fs_id_t id) {
    fs_id_t prefix = id & 0xffff0000u;
    if (prefix == 0) return &fs_root_volume;
    if (secondary_volume.mounted && secondary_volume.prefix == prefix)
        return &secondary_volume;
    return 0;
}

/**
 * @brief Releases the tables a previous mount allocated.
 *
 * init_fs() is callable more than once - test_vfs.c calls it twice to prove that
 * a disk which cannot be read is refused - so a mount that allocated without
 * releasing would leak the better part of two megabytes each time. That the leak
 * would have been introduced here at all was caught by a test written for
 * something else entirely.
 */
static void fs_tables_free(fs_volume_t *volume) {
    if (volume->entries)             { kfree(volume->entries);             volume->entries = 0; }
    if (volume->fat) { kfree(volume->fat); volume->fat = 0; }
    if (volume->locks)           { kfree(volume->locks);           volume->locks = 0; }
    volume->entry_count = 0;
}

/**
 * @brief Allocates the directory table, the allocation table and the inode locks.
 *
 * All three or none: a mount holding two of them is a mount that will read past
 * the end of the third. inode_locks is easy to forget because nothing outside
 * this file names it, and it has to be exactly as long as dir_table - the index
 * into one is the index into the other.
 *
 * @param entries  Directory entries to make room for.
 * @param clusters Allocation table entries.
 * @return E_OK, or E_NOMEM with nothing allocated.
 */
static int fs_tables_alloc(fs_volume_t *volume, uint32_t entries, uint32_t clusters) {
    fs_tables_free(volume);

    if (entries == 0 || clusters == 0) return E_INVAL;
    if (entries > FS_MAX_ENTRIES_CAP || clusters > FS_MAX_CLUSTERS_CAP) return E_INVAL;

    volume->entries             = (disk_file_entry_t *)kmalloc(entries * sizeof(disk_file_entry_t));
    volume->fat = (uint32_t *)kmalloc(clusters * sizeof(uint32_t));
    volume->locks           = (rwlock_t *)kmalloc(entries * sizeof(rwlock_t));

    if (!volume->entries || !volume->fat || !volume->locks) {
        klog(LOG_LEVEL_ERROR, "VFS", "Not enough kernel memory for the file system tables.");
        fs_tables_free(volume);
        return E_NOMEM;
    }

    ft_memset(volume->entries, 0, entries * sizeof(disk_file_entry_t));
    ft_memset(volume->fat, 0, clusters * sizeof(uint32_t));

    for (uint32_t i = 0; i < entries; i++) {
        rwlock_init(&volume->locks[i]);
    }

    volume->entry_count = (int)entries;

    klog_int(LOG_LEVEL_INFO, "VFS", "Directory entries this disk holds", (int)entries);
    klog_int(LOG_LEVEL_INFO, "VFS", "Clusters this disk holds", (int)clusters);
    return E_OK;
}

int fs_get_entry_idx(const char *name, fs_id_t parent_id);

/**
 * @brief Reads a sector named relative to the partition.
 *
 * Everything in this file addresses the file system's own sectors: the
 * superblock is sector 0 *of the partition*, the directory table starts at 1,
 * and so on. Exactly one place knows where the partition is, and it is here.
 *
 * The block cache is deliberately not called directly anywhere else in the file
 * system any more. A single missed offset would read the right sector of the
 * wrong place - which is a silent wrong answer of the kind this project has
 * spent three releases removing - and the only defence that scales is having one
 * door.
 *
 * The MBR itself is the one thing outside this: it lives at absolute sector 0,
 * belongs to the disk rather than to any file system, and is read and written
 * through the cache directly, with a comment saying so at each of the two sites.
 */
int fs_volume_read_sector(fs_volume_t *volume, uint32_t rel_sector, uint8_t *buffer) {
    if (!volume || !volume->device) return E_NODEV;
    if (rel_sector >= volume->sectors) return E_NXIO;
    return bcache_read_sector(volume->device, volume->part_start + rel_sector, buffer);
}

/** @brief Writes a sector named relative to the partition. See fs_read_sector(). */
int fs_volume_write_sector(fs_volume_t *volume, uint32_t rel_sector, uint8_t *buffer) {
    if (!volume || !volume->device) return E_NODEV;
    if (rel_sector >= volume->sectors) return E_NXIO;
    return bcache_write_sector(volume->device, volume->part_start + rel_sector, buffer);
}

/**
 * @brief The current time, as the on-disk format records it.
 *
 * Seconds since the Unix epoch, from the RTC's UTC reading. A file's timestamp
 * exists to be compared with another file's, so it is stored in the one form
 * that compares with a subtraction - see the note in esdtime.h on why the epoch
 * arrived with this format rather than earlier.
 *
 * @return Seconds since 1970-01-01 UTC.
 */
static uint32_t fs_now(void) {
    esd_time_t now;

    rtc_read_utc(&now);
    return esd_time_to_epoch(&now);
}

/**
 * @brief Checks that a parent id names a directory that actually exists.
 *
 * Entry ids arrive from user space, and fs_mkdir()/fs_create_file_raw() store
 * whatever they are given. Accepting an arbitrary id let a process create an
 * entry whose parent is itself, and the parent walk in check_vfs_access() then
 * never terminated.
 *
 * @param parent_id Candidate parent entry id. 0 is the root, which has no
 *                  directory table entry of its own.
 * @return 1 when the id names an existing directory, 0 otherwise.
 */
int fs_dir_exists(fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return 0;

    if ((uint16_t)parent_id == 0) return 1;

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used &&
            volume->entries[i].file_type == FT_DIR &&
            volume->entries[i].entry_id == (uint16_t)parent_id) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief safe_strcpy
 * @param dest
 * @param src
 * @param max_len
 * @return static void
 */
/* Disk names are untrusted fixed-width fields, not C strings until checked. */
static int valid_entry_name(const char *name) {
    if (!name || !name[0]) return 0;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) return 0;
    for (unsigned i = 0; i < MAX_FILENAME; i++) {
        if (!name[i]) return 1;
        if (name[i] == '/') return 0;
    }
    return 0;
}

static void safe_strcpy(char *dest, const char *src, size_t max_len) {
    if (max_len == 0) return;
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) { dest[i] = src[i]; }
    dest[i] = '\0';
}

static mutex_t vfs_mutex;
static int vfs_lock_owner = -1;
static int vfs_lock_count = 0;

/**
 * @brief vfs_lock
 * @return static void
 */
static void vfs_lock(void) {
    if (!multitasking_enabled || current_task == 0) return;
    int my_pid = current_task->pid;
    if (vfs_lock_owner == my_pid) {
        vfs_lock_count++;
        return;
    }
    mutex_lock(&vfs_mutex, 0);
    vfs_lock_owner = my_pid;
    vfs_lock_count = 1;
}

/**
 * @brief vfs_unlock
 * @return static void
 */
static void vfs_unlock(void) {
    if (!multitasking_enabled || current_task == 0) return;
    int my_pid = current_task->pid;
    if (vfs_lock_owner == my_pid) {
        vfs_lock_count--;
        if (vfs_lock_count == 0) {
            vfs_lock_owner = -1;
            mutex_unlock(&vfs_mutex);
        }
    }
}

/* --- FAT (FILE ALLOCATION TABLE) MANAGEMENT --- */

/**
 * @brief Writes the allocation table out.
 *
 * One uint32_t per *cluster* as of the v2 format, where it was one per sector
 * before. That is what lifted the 2 MB ceiling: the array is the same 16 KB and
 * now describes eight times as much disk.
 */
static int write_table(fs_volume_t *volume, uint32_t first, const void *table, uint32_t bytes) {
    const uint8_t *src = table;
    uint8_t sector[512];
    while (bytes) {
        uint32_t count = bytes < sizeof(sector) ? bytes : sizeof(sector);
        ft_memset(sector, 0, sizeof(sector));
        ft_memcpy(sector, src, count);
        int rc = fs_volume_write_sector(volume, first++, sector);
        if (rc != E_OK) return rc;
        src += count;
        bytes -= count;
    }
    return E_OK;
}

static int read_table(fs_volume_t *volume, uint32_t first, void *table, uint32_t bytes) {
    uint8_t *dst = table;
    uint8_t sector[512];
    while (bytes) {
        uint32_t count = bytes < sizeof(sector) ? bytes : sizeof(sector);
        int rc = fs_volume_read_sector(volume, first++, sector);
        if (rc != E_OK) return rc;
        ft_memcpy(dst, sector, count);
        dst += count;
        bytes -= count;
    }
    return E_OK;
}

static int save_fat_to_disk(fs_volume_t *volume) {
    int rc = write_table(volume, volume->super.fat_start, volume->fat,
                       volume->clusters * sizeof(*volume->fat));
    volume->fat_pending = rc != E_OK;
    return rc;
}

/**
 * @brief Reserves a chain of clusters and links them.
 *
 * The search starts at FS_FIRST_DATA_CLUSTER rather than at 0, and that is not
 * an optimisation: clusters 0 and 1 are reserved so that a start_cluster of 0
 * can mean "this entry has no data" without colliding with a real allocation.
 * Directories and empty files rely on it.
 *
 * @param count Clusters wanted.
 * @return First cluster of the chain, or FAT_FREE when the disk cannot hold it.
 */
static uint32_t allocate_fat_chain(fs_volume_t *volume, uint32_t count) {
    if (count == 0) return FAT_EOF;

    uint32_t start_cluster = FAT_EOF;
    uint32_t current_cluster = FAT_EOF;
    uint32_t allocated = 0;

    for (uint32_t i = FS_FIRST_DATA_CLUSTER; i < volume->clusters && allocated < count; i++) {
        if (volume->fat[i] == FAT_FREE) {
            if (start_cluster == FAT_EOF) {
                start_cluster = i;
            } else {
                volume->fat[current_cluster] = i;
            }

            current_cluster = i;
            volume->fat[current_cluster] = FAT_EOF;
            allocated++;
        }
    }

    if (allocated < count) {
        uint32_t rollback = start_cluster;
        while (rollback != FAT_EOF) {
            uint32_t next = volume->fat[rollback];
            volume->fat[rollback] = FAT_FREE;
            rollback = next;
        }
        return FAT_FREE;
    }

    return start_cluster;
}

/**
 * @brief save_directory_to_disk
 * @return static void
 */
static int save_directory_to_disk(fs_volume_t *volume) {
    int rc = write_table(volume, volume->super.dir_start, volume->entries,
                       volume->entry_count * sizeof(*volume->entries));
    volume->directory_pending = rc != E_OK;
    return rc;
}

/* --- FILE SYSTEM INITIALIZATION --- */

/**
 * @brief Sectors the directory table occupied before v0.9.0.
 *
 * Only used to decide whether a disk that has no superblock is blank or is one
 * of the old ones. Nothing reads an old table; recognising it is the whole of
 * what this is for.
 */
#define FS_LEGACY_DIR_SECTORS 136

/**
 * @brief Writes the superblock to sector 0.
 *
 * The tail of the sector is zeroed rather than left as whatever was there, so
 * that a field added to the struct later reads as zero on a disk written now and
 * zero can be given a meaning.
 */
static int save_superblock(fs_volume_t *volume) {
    uint8_t sec_buf[512];
    uint8_t *src = (uint8_t *)&volume->super;

    ft_memset(sec_buf, 0, 512);
    for (uint32_t i = 0; i < sizeof(disk_superblock_t); i++) sec_buf[i] = src[i];
    return fs_volume_write_sector(volume, FS_SUPER_SECTOR, sec_buf);
}

/**
 * @brief Whether every byte of a run of disk sectors is zero.
 *
 * Absolute sectors rather than partition-relative ones, which is the whole
 * reason this is the only one of its kind: deciding whether a disk is blank has
 * to happen before there is a partition to be relative to. The partition-
 * relative twin that used to sit beside this had no callers left once the mount
 * path was rewritten around the MBR, so it went with them.
 *
 * Three answers, not two, as of v1.2.0. It used to have two, and the missing one
 * was the dangerous one: a sector it could not read came back zeroed from the
 * block cache, counted as blank, and the caller formatted the disk. Every failure
 * path in the ATA driver zeroes its buffer, so "could not read" and "really is
 * zero" were the same answer - on a disk that had not been identified, on an LBA
 * past the end of it, on an IRQ timeout, and on a drive reporting a hardware
 * error. QEMU's disk never fails, which is why this survived to be found by
 * reading rather than by breaking.
 *
 * @param start First sector, from the start of the disk.
 * @param count How many.
 * @return 1 when the whole run is zero, 0 when any byte is not, and a negative
 *         errno when the disk could not be read - which is not blankness and
 *         must never be treated as it.
 */
static int disk_region_is_blank(fs_volume_t *volume, uint32_t start, uint32_t count) {
    uint8_t sec_buf[512];

    for (uint32_t s = 0; s < count; s++) {
        int res = bcache_read_sector(volume->device, start + s, sec_buf);
        if (res != E_OK) {
            klog_int(LOG_LEVEL_ERROR, "VFS",
                     "Could not read this sector while deciding whether the disk is blank",
                     (int)(start + s));
            return res;
        }
        for (int i = 0; i < 512; i++) {
            if (sec_buf[i] != 0) {
                return 0;
            }
        }
    }
    return 1;
}

/**
 * @brief Writes the partition table onto a blank disk.
 *
 * One partition, spanning everything after FS_PART_START_LBA. The other three
 * entries are left zeroed, which is what an unused entry is, and the kernel
 * reads exactly one of them - the second partition this makes room for needs
 * mount() to be useful and mount() is not in this release.
 *
 * Sector 0 is the disk's, not the file system's, so this is one of the two
 * places that call the block cache without going through fs_write_sector().
 */
static int write_mbr(fs_volume_t *volume, uint32_t disk_sectors) {
    uint8_t sec_buf[512];
    mbr_t mbr;

    ft_memset(&mbr, 0, sizeof(mbr));
    mbr.partitions[0].status       = 0x00;   /* Not bootable: GRUB loads us. */
    mbr.partitions[0].type         = FS_PART_TYPE;
    mbr.partitions[0].lba_first    = FS_PART_START_LBA;
    mbr.partitions[0].sector_count = disk_sectors - FS_PART_START_LBA;
    mbr.signature                  = MBR_SIGNATURE;

    ft_memset(sec_buf, 0, 512);
    ft_memcpy(sec_buf, &mbr, sizeof(mbr));
    return bcache_write_sector(volume->device, 0, sec_buf);
}

/**
 * @brief Lays a fresh file system into the partition.
 *
 * The geometry is chosen from the size of the device and then written down in the
 * superblock, which is what everything afterwards reads. It came from fixed
 * FS_ constants until v1.3.0, which is why a 16 GB disk and a 2 MB one were laid
 * out identically and both held 512 entries. Changing what this picks changes
 * what new disks are formatted as and cannot change how an existing one is read
 * - a disk written by an older release still says 512 in its own superblock, and
 * that is what it is mounted with.
 *
 * Clusters 0 and 1 are marked in use and never handed out. That is what lets a
 * start_cluster of 0 mean "no data at all" - which is what a directory and a
 * freshly created empty file both record - without colliding with a real
 * allocation. Every FAT ever written reserves the same two for the same reason.
 */
/**
 * @brief Sectors a directory table of @p entries occupies.
 */
static uint32_t dir_sectors_for(uint32_t entries) {
    uint32_t bytes = entries * (uint32_t)sizeof(disk_file_entry_t);
    return (bytes + 511) / 512;
}

/**
 * @brief How many entries a partition of this size should be given.
 *
 * Doubling from the old fixed 512 for as long as the table stays under a
 * twentieth of the partition. The bound is a share rather than a number because
 * the cost of guessing wrong is asymmetric: a table too small caps the file
 * system, and a table too large is space the user cannot get back - on the 2 MB
 * image the test suite uses, the cap of 8192 entries would be 1536 sectors of a
 * 4032-sector partition, well over a third of the disk spent on a table that
 * could never be filled.
 *
 * A disk written before v1.3.0 keeps whatever its own superblock says.
 */
static uint32_t entries_for_partition(uint32_t partition_sectors) {
    uint32_t entries = 512;

    while (entries * 2 <= FS_MAX_ENTRIES_CAP &&
           dir_sectors_for(entries * 2) * 20 <= partition_sectors) {
        entries *= 2;
    }
    return entries;
}

static int format_disk(fs_volume_t *volume, const keyslot_t *slot) {

    klog(LOG_LEVEL_INFO, "VFS", "Blank disk; writing a partition table and a new file system.");

    /*
     * Entries, then the table, then the allocation table, then where the data
     * can start - in that order, because each depends on the one before it. The
     * count of clusters depends on data_start, which depends on how many sectors
     * the allocation table needs, which depends on the count of clusters; the
     * loop below settles that rather than solving it, and settles in two passes
     * because shrinking the cluster count can only shrink the table.
     */
    uint32_t entries    = entries_for_partition(volume->sectors);
    uint32_t dir_start  = FS_DIR_START_SECTOR;
    uint32_t dir_secs   = dir_sectors_for(entries);
    uint32_t fat_start  = dir_start + dir_secs;
    uint32_t clusters   = volume->clusters;
    uint32_t fat_secs   = 0;
    uint32_t data_start = 0;

    /*
     * Capped before the loop, not after. The caller already clamps, so this is
     * belt and braces - but applying it afterwards would size the allocation
     * table for a count larger than the one recorded, and an invariant that
     * holds only because of what the caller happens to do is not an invariant.
     */
    if (clusters > FS_MAX_CLUSTERS_CAP) clusters = FS_MAX_CLUSTERS_CAP;

    for (int pass = 0; pass < 2; pass++) {
        fat_secs   = (clusters * 4 + 511) / 512;
        data_start = fat_start + fat_secs;

        uint32_t usable = (volume->sectors > data_start) ? (volume->sectors - data_start) : 0;
        uint32_t fit    = usable / FS_CLUSTER_SECTORS;

        if (fit < clusters) clusters = fit;
    }

    volume->clusters = clusters;

    if (fs_tables_alloc(volume, entries, clusters) != E_OK) {
        klog(LOG_LEVEL_ERROR, "VFS", "Format abandoned: the tables would not fit in memory.");
        return E_NOMEM;
    }

    ft_memset(&volume->super, 0, sizeof(volume->super));
    volume->super.magic = FS_SUPER_MAGIC;
    volume->super.format_version = FS_FORMAT_VERSION;
    volume->super.sector_size = 512;
    volume->super.total_sectors = volume->sectors;
    volume->super.dir_start = dir_start;
    volume->super.dir_sectors = dir_secs;
    volume->super.max_entries = entries;
    volume->super.entry_size = (uint16_t)sizeof(disk_file_entry_t);
    volume->super.cluster_sectors = FS_CLUSTER_SECTORS;
    volume->super.fat_start = fat_start;
    volume->super.data_start = data_start;
    volume->super.total_clusters = volume->clusters;
    volume->super.flags = 0;
    if (slot) volume->super.keyslot = *slot;

    for (uint32_t i = 0; i < FS_FIRST_DATA_CLUSTER && i < volume->clusters; i++) {
        volume->fat[i] = FAT_EOF;
    }

    /* Publish a recognizable superblock only after its tables reached the device. */
    int rc = save_directory_to_disk(volume);
    if (rc == E_OK) rc = save_fat_to_disk(volume);
    if (rc == E_OK) rc = bcache_flush_dev(volume->device);
    if (rc == E_OK) rc = save_superblock(volume);
    if (rc == E_OK) rc = bcache_flush_dev(volume->device);
    if (rc != E_OK) return rc;

    /*
     * Set last, and only here. The superblock this wrote carries a zeroed key
     * slot, and this flag is what tells the boot path that the zero means "new
     * disk" rather than "slot lost".
     */
    volume->formatted = 1;
    return E_OK;
}

/**
 * @brief Reads sector 0 and decides whether it describes a file system we can read.
 *
 * Checks the geometry as well as the magic. A superblock that says the directory
 * table holds more entries than dir_table has room for, or entries of a size this
 * build does not agree with, describes a disk this kernel would misread - and
 * misreading it silently is how a file system eats itself.
 *
 * @return 1 when fs_super has been filled with a usable geometry.
 */
static int load_superblock(fs_volume_t *volume) {
    uint8_t sec_buf[512];
    uint8_t *dst = (uint8_t *)&volume->super;

    if (fs_volume_read_sector(volume, FS_SUPER_SECTOR, sec_buf) != E_OK) {
        klog(LOG_LEVEL_ERROR, "VFS", "The superblock sector could not be read.");
        return 0;
    }
    for (uint32_t i = 0; i < sizeof(disk_superblock_t); i++) dst[i] = sec_buf[i];

    if (volume->super.magic != FS_SUPER_MAGIC) return 0;

    if (volume->super.format_version != FS_FORMAT_VERSION) {
        klog_int(LOG_LEVEL_ERROR, "VFS",
                 "Disk was written by a different format version", volume->super.format_version);
        return 0;
    }
    if (volume->super.sector_size != 512 ||
        volume->super.entry_size != (uint16_t)sizeof(disk_file_entry_t) ||
        volume->super.max_entries == 0 || volume->super.max_entries > FS_MAX_ENTRIES_CAP ||
        volume->super.cluster_sectors != FS_CLUSTER_SECTORS ||
        volume->super.total_clusters <= FS_FIRST_DATA_CLUSTER ||
        volume->super.total_clusters > FS_MAX_CLUSTERS_CAP ||
        volume->super.dir_start == 0 ||
        volume->super.fat_start <= volume->super.dir_start ||
        volume->super.data_start <= volume->super.fat_start) {
        klog(LOG_LEVEL_ERROR, "VFS", "Superblock describes a geometry this kernel cannot read.");
        return 0;
    }

    /*
     * The cluster count has to fit the partition as well as the array. A
     * superblock claiming more clusters than the partition holds would send the
     * last chain off the end of it, into whatever follows on the disk.
     */
    uint32_t dir_sectors = dir_sectors_for(volume->super.max_entries);
    uint32_t fat_sectors = (volume->super.total_clusters * sizeof(uint32_t) + 511) / 512;
    if (volume->super.total_sectors != volume->sectors ||
        volume->super.dir_sectors != dir_sectors ||
        dir_sectors > volume->super.fat_start - volume->super.dir_start ||
        fat_sectors > volume->super.data_start - volume->super.fat_start ||
        volume->super.data_start >= volume->sectors ||
        volume->super.total_clusters > (volume->sectors - volume->super.data_start) / FS_CLUSTER_SECTORS) {
        klog(LOG_LEVEL_ERROR, "VFS", "Superblock claims more clusters than the partition holds.");
        return 0;
    }

    volume->clusters = volume->super.total_clusters;
    return 1;
}

/**
 * @brief Reads the partition table and locates the file system's partition.
 *
 * Sector 0 belongs to the disk rather than to any file system, so this is the
 * second and last place that calls the block cache without the partition offset.
 *
 * @param disk_sectors Sectors the device reports.
 * @return 1 when fs_part_start and fs_max_sectors describe a usable partition.
 */
static int load_partition(fs_volume_t *volume, uint32_t disk_sectors) {
    uint8_t sec_buf[512];
    mbr_t mbr;

    /*
     * A sector 0 that could not be read is not a sector 0 without a partition
     * table. Both used to arrive here as a zeroed buffer and leave as "return 0",
     * which sends the caller down the path that decides whether to format.
     */
    if (bcache_read_sector(volume->device, 0, sec_buf) != E_OK) return -1;

    ft_memcpy(&mbr, sec_buf, sizeof(mbr));

    if (mbr.signature != MBR_SIGNATURE) return 0;

    for (int i = 0; i < MBR_PART_COUNT; i++) {
        if (mbr.partitions[i].type != FS_PART_TYPE) continue;

        uint32_t start = mbr.partitions[i].lba_first;
        uint32_t count = mbr.partitions[i].sector_count;

        /*
         * Checked against the device rather than taken on trust. The partition
         * table is as attacker-controlled as the rest of the image, and a
         * partition that claims to start past the end of the disk - or to be
         * longer than what is left - would put every sector address this file
         * system computes somewhere that is not the file system.
         */
        if (start == 0 || count == 0) continue;
        if (start >= disk_sectors) continue;
        if (count > disk_sectors - start) continue;
        if (count <= FS_DATA_START_SECTOR) continue;

        volume->part_start  = start;
        volume->sectors = count;
        return 1;
    }

    klog(LOG_LEVEL_ERROR, "VFS", "A partition table with no partition this kernel owns.");
    return 0;
}

/**
 * @brief Checks the directory table before anything is allowed to believe it.
 *
 * The table is read off the disk and, until v0.10.0, used exactly as it came.
 * That was a documented gap - SECURITY.md carried it as an open contribution for
 * several releases - and the reason it mattered is that the whole tree rests on
 * invariants nothing enforced:
 *
 *   entry_id equals the slot the entry occupies. Every lookup in this file
 *   assumes it, and a crafted image that broke it would make fs_get_entry_idx()
 *   and slot_of_entry() disagree about what a given id names.
 *
 *   The parent chain terminates. check_vfs_access() and sys_getcwd() both walk
 *   it, and both are bounded by the table size *because* of this - a bound is a
 *   containment measure, not a check.
 *
 *   A start cluster is inside the file system. fs_read_raw() and fs_delete()
 *   guard theirs individually; this is the same question asked once, at the door.
 *
 * Refused rather than repaired. A table that is not what it claims to be cannot
 * be corrected into what it should have been - the information to do that is
 * exactly the information in doubt - and quietly rewriting somebody's directory
 * tree is a worse answer than declining to mount.
 *
 * @return 1 when every used entry is consistent.
 */
static int volume_validate_directory(fs_volume_t *volume) {
    /*
     * One bound, not two. This read "i < fs_super.max_entries && i < the cap",
     * which was a guard against a superblock claiming more entries than the
     * static array could hold. The table is allocated to exactly what the
     * superblock asked for now - load_superblock(volume) refuses a count above the cap
     * before anything is allocated - so fs_max_entries is that number and the
     * second test was the same test written twice.
     */
    for (int i = 0; i < volume->entry_count; i++) {
        if (!volume->entries[i].is_used) continue;
        if (i == 0 || volume->entries[i].is_used != 1 ||
            !valid_entry_name(volume->entries[i].filename)) {
            klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: invalid name or reserved slot", i);
            return 0;
        }

        if (volume->entries[i].entry_id != (fs_id_t)i) {
            klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: entry id does not match its slot", (int)i);
            return 0;
        }
        if (volume->entries[i].file_type != FT_REGULAR && volume->entries[i].file_type != FT_DIR) {
            klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: entry has no valid type", (int)i);
            return 0;
        }
        for (int j = 1; j < i; j++) {
            if (volume->entries[j].is_used &&
                volume->entries[j].parent_id == volume->entries[i].parent_id &&
                ft_strcmp(volume->entries[j].filename, volume->entries[i].filename) == 0) {
                klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: duplicate sibling name", i);
                return 0;
            }
        }
        if (volume->entries[i].start_cluster != 0 &&
            !(volume->entries[i].file_size == 0 && volume->entries[i].start_cluster == FAT_EOF) &&
            (volume->entries[i].start_cluster < FS_FIRST_DATA_CLUSTER ||
             volume->entries[i].start_cluster >= volume->clusters)) {
            klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: start cluster is outside the file system", (int)i);
            return 0;
        }

        /*
         * The parent has to exist, be a directory, and lead to the root. The hop
         * count is what turns a cycle into a refusal instead of a hang: a chain
         * longer than the table cannot be acyclic.
         */
        int curr = (int)volume->entries[i].parent_id;
        int hops = 0;

        while (curr != 0) {
            if (++hops > volume->entry_count) {
                klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: parent chain does not reach the root", (int)i);
                return 0;
            }
            if (curr < 0 || curr >= volume->entry_count || !volume->entries[curr].is_used ||
                volume->entries[curr].entry_id != (fs_id_t)curr ||
                volume->entries[curr].file_type != FT_DIR) {
                klog_int(LOG_LEVEL_ERROR, "VFS", "Directory table: parent is not a directory that exists", (int)i);
                return 0;
            }
            curr = volume->entries[curr].parent_id;
        }
    }
    return 1;
}

/* Reject cross-linked, cyclic, truncated and orphaned chains without repair.
 * One bit per cluster bounds both the memory cost and the number of visits. */
static int volume_validate_allocation(fs_volume_t *v) {
    uint32_t bytes = (v->clusters + 7u) / 8u;
    uint8_t *seen = kmalloc(bytes);
    if (!seen) return E_NOMEM;
    ft_memset(seen, 0, bytes);
    int rc = E_INVAL;
    if (v->fat[0] != FAT_EOF || v->fat[1] != FAT_EOF) goto done;
    for (int i = 1; i < v->entry_count; i++) {
        const disk_file_entry_t *entry = &v->entries[i];
        if (!entry->is_used) continue;
        uint32_t cluster = entry->start_cluster;
        if (entry->file_type == FT_DIR) {
            if (entry->file_size || cluster) goto done;
            continue;
        }
        /* Existing v3 writers allocate one cluster even for an empty raw
         * file. Also accept the no-allocation representation. */
        if (!entry->file_size && (cluster == FAT_FREE || cluster == FAT_EOF)) continue;
        uint32_t count = entry->file_size ?
            1 + (entry->file_size - 1) / (512u * FS_CLUSTER_SECTORS) : 1;
        if (count > v->clusters - FS_FIRST_DATA_CLUSTER) goto done;
        for (uint32_t n = 0; n < count; n++) {
            if (cluster < FS_FIRST_DATA_CLUSTER || cluster >= v->clusters ||
                (seen[cluster / 8] & (1u << (cluster % 8)))) goto done;
            seen[cluster / 8] |= 1u << (cluster % 8);
            cluster = v->fat[cluster];
        }
        if (cluster != FAT_EOF) goto done;
    }
    for (uint32_t c = FS_FIRST_DATA_CLUSTER; c < v->clusters; c++) {
        if (!(seen[c / 8] & (1u << (c % 8))) && v->fat[c] != FAT_FREE) goto done;
    }
    rc = E_OK;
done:
    kfree(seen);
    if (rc != E_OK) klog(LOG_LEVEL_ERROR, "VFS", "Allocation table does not match file ownership and lengths.");
    return rc;
}

/* Load a v3 filesystem without issuing writes. Its directory, FAT and device
 * are private to this mount, including while probing an untrusted candidate. */
static int volume_load(fs_volume_t *volume, blockdev_t *device) {
    volume->device = device;
    if (!device || device->sector_size != 512) return E_NODEV;
    int part = load_partition(volume, device->sector_count);
    if (part < 0) return E_IO;
    if (!part || !load_superblock(volume)) return E_INVAL;
    int rc = fs_tables_alloc(volume, volume->super.max_entries, volume->clusters);
    if (rc != E_OK) return rc;
    rc = read_table(volume, volume->super.dir_start, volume->entries,
                    volume->entry_count * sizeof(*volume->entries));
    if (rc == E_OK)
        rc = read_table(volume, volume->super.fat_start, volume->fat,
                        volume->clusters * sizeof(*volume->fat));
    if (rc == E_OK && !volume_validate_directory(volume)) rc = E_INVAL;
    if (rc == E_OK) rc = volume_validate_allocation(volume);
    if (rc != E_OK) {
        fs_tables_free(volume);
        return rc;
    }
    volume->mounted = 1;
    return E_OK;
}

static int format_blank(blockdev_t *device, uint32_t expected_sectors,
                        const keyslot_t *slot) {
    if (!device || !device->write) return E_ROFS;
    if (device->sector_size != 512 || device->sector_count != expected_sectors ||
        expected_sectors <= FS_PART_START_LBA + FS_DATA_START_SECTOR) return E_INVAL;
    if ((fs_root_volume.mounted && device == fs_root_volume.device) ||
        (secondary_volume.mounted && device == secondary_volume.device)) return E_BUSY;
    fs_volume_t fresh = { .device = device, .part_start = FS_PART_START_LBA,
                          .sectors = expected_sectors - FS_PART_START_LBA };
    fresh.clusters = (fresh.sectors - FS_DATA_START_SECTOR) / FS_CLUSTER_SECTORS;
    if (fresh.clusters <= FS_FIRST_DATA_CLUSTER) return E_INVAL;

    int blank = disk_region_is_blank(&fresh, 0, expected_sectors);
    if (blank < 0) return blank;
    if (!blank) return E_EXIST;
    int rc = format_disk(&fresh, slot);
    if (rc == E_OK) rc = write_mbr(&fresh, expected_sectors);
    if (rc == E_OK) rc = bcache_flush_dev(device);
    fs_tables_free(&fresh);
    return rc;
}

int fs_format_data_device(blockdev_t *device, uint32_t sectors, const char *pass) {
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) return E_ROFS;
    if (!pass || !pass[0]) return E_INVAL;
    uint8_t key[KEYSLOT_KEY_LEN];
    keyslot_t slot;
    int rc = generate_random_bytes(key, sizeof(key)) == ENTROPY_FAIL ? E_IO :
             keyslot_create(&slot, pass, key);
    if (rc == E_OK) {
        vfs_lock();
        rc = format_blank(device, sectors, &slot);
        vfs_unlock();
    }
    volatile uint8_t *secret = key;
    for (unsigned i = 0; i < sizeof(key); i++) secret[i] = 0;
    return rc;
}

void init_fs(void) {
    static int initialized;
    if (!initialized) { mutex_init(&vfs_mutex); initialized = 1; }
    fs_tables_free(&fs_root_volume);
    ft_memset(&fs_root_volume, 0, sizeof(fs_root_volume));
    volume_load(&fs_root_volume, blockdev_root());
}

/* Called only for the exact device explicitly selected at boot. */
int fs_prepare_first_boot(blockdev_t *device) {
    if (!device || device != blockdev_root()) return E_NODEV;
    int rc = format_blank(device, device->sector_count, 0);
    if (rc != E_OK) return rc;
    init_fs();
    if (!fs_mounted) return E_IO;
    fs_was_formatted = 1;
    return E_OK;
}

static fs_id_t next_prefix = 0x10000u;

static void wipe_bytes(uint8_t *buf, uint32_t size) {
    volatile uint8_t *p = buf;
    while (size--) *p++ = 0;
}

void fs_forget_secondary_key(void) {
    secondary_volume.key_ready = 0;
    wipe_bytes(secondary_volume.data_key, sizeof(secondary_volume.data_key));
}

static const uint8_t *volume_key(fs_volume_t *volume) {
    if (volume == &fs_root_volume)
        return crypto_fs_key_is_usable() ? kernel_master_key : 0;
    return volume->key_ready ? volume->data_key : 0;
}

blockdev_t *fs_secondary_device(void) {
    return secondary_volume.mounted ? secondary_volume.device : 0;
}

static int uses_volume(const process_t *p, fs_id_t prefix) {
    if (!p) return 0;
    if ((p->cwd_id & 0xffff0000u) == prefix) return 1;
    for (uint32_t i = 0; p->fd_table && i < p->fd_table_size; i++) {
        const file_descriptor_t *fd = &p->fd_table[i];
        if (fd->type == FD_TYPE_FILE && fd->ptr &&
            ((vfs_file_t *)fd->ptr)->volume_prefix == prefix) return 1;
    }
    return 0;
}

static int attach_locked(blockdev_t *device, const char *passphrase) {
    if (!fs_mounted || !device) return E_NODEV;
    if (device == fs_root_volume.device || device == fs_secondary_device()) return E_OK;
    if (secondary_volume.mounted || !next_prefix || next_prefix > 0x7fff0000u) return E_BUSY;
    if (current_sec_level >= SEC_LEVEL_LOCKDOWN) return E_ACCES;
    int idx = fs_get_entry_idx("mnt", 0);
    if (idx <= 0 || dir_table[idx].file_type != FT_DIR) return E_NOTDIR;
    for (int i = 0; i < fs_max_entries; i++)
        if (dir_table[i].is_used && dir_table[i].parent_id == idx) return E_NOTEMPTY;
    if (current_task && current_task->cwd_id == (fs_id_t)idx) return E_BUSY;
    for (process_t *p = task_list_head; p; p = p->next)
        if (p->cwd_id == (fs_id_t)idx) return E_BUSY;

    fs_volume_t candidate = {0};
    int rc = volume_load(&candidate, device);
    if (rc == E_OK) {
        if (!keyslot_is_present(&candidate.super.keyslot) || !passphrase || !*passphrase)
            rc = E_ACCES;
        else rc = keyslot_unwrap(&candidate.super.keyslot, passphrase, candidate.data_key);
    }
    if (rc == E_OK) {
        candidate.prefix = next_prefix;
        next_prefix += 0x10000u;
        candidate.covered = (fs_id_t)idx;
        candidate.key_ready = 1;
        secondary_volume = candidate;
    } else {
        fs_tables_free(&candidate);
    }
    wipe_bytes(candidate.data_key, sizeof(candidate.data_key));
    return rc;
}

int fs_attach(blockdev_t *device, const char *passphrase) {
    vfs_lock();
    int rc = attach_locked(device, passphrase);
    vfs_unlock();
    return rc;
}

/* A cache admission failure may leave a changed table in RAM. Sync and
 * detach retry these tables before considering the device flushed. */
static int retry_metadata(fs_volume_t *v) {
    int rc = E_OK;
    if (v->mounted && v->fat_pending) rc = save_fat_to_disk(v);
    if (rc == E_OK && v->mounted && v->directory_pending)
        rc = save_directory_to_disk(v);
    return rc;
}

int fs_sync_all(void) {
    vfs_lock();
    int primary = retry_metadata(&fs_root_volume);
    int secondary = retry_metadata(&secondary_volume);
    int cache = bcache_flush();
    vfs_unlock();
    return primary != E_OK ? primary : secondary != E_OK ? secondary : cache;
}

int fs_detach(void) {
    vfs_lock();
    int rc = E_NODEV;
    if (secondary_volume.mounted) {
        int busy = uses_volume(current_task, secondary_volume.prefix);
        for (process_t *p = task_list_head; !busy && p; p = p->next)
            busy = uses_volume(p, secondary_volume.prefix);
        rc = busy ? E_BUSY : retry_metadata(&secondary_volume);
        if (rc == E_OK) rc = bcache_flush_dev(secondary_volume.device);
        if (rc == E_OK) {
            fs_forget_secondary_key();
            fs_tables_free(&secondary_volume);
            ft_memset(&secondary_volume, 0, sizeof(secondary_volume));
        }
    }
    vfs_unlock();
    return rc;
}

/* The covered directory supplies the secondary root's name and permissions. */
const disk_file_entry_t *fs_entry(fs_id_t id) {
    fs_volume_t *v = fs_volume_for(id);
    if (!v || !v->mounted || !id) return 0;
    if (id == v->prefix) {
        id = v->covered;
        v = &fs_root_volume;
    }
    unsigned index = (uint16_t)id;
    if (!index || index >= (unsigned)v->entry_count || !v->entries[index].is_used ||
        v->entries[index].entry_id != index) return 0;
    return &v->entries[index];
}

fs_id_t fs_follow_mount(fs_id_t id) {
    if (secondary_volume.mounted && id == secondary_volume.covered) return secondary_volume.prefix;
    return id;
}

int fs_parent_dir(fs_id_t id) {
    if (id == 0) return 0;
    const disk_file_entry_t *entry = fs_entry(id);
    if (!entry || entry->file_type != FT_DIR) return E_NOTDIR;
    if (id == secondary_volume.prefix) return entry->parent_id;
    return (int)((id & 0xffff0000u) | entry->parent_id);
}

int fs_child_dir(fs_id_t parent, const char *name) {
    int idx = fs_get_entry_idx(name, parent);
    if (idx < 0) return E_NOENT;
    fs_id_t id = (parent & 0xffff0000u) | (uint32_t)idx;
    const disk_file_entry_t *entry = fs_entry(id);
    if (!entry || entry->file_type != FT_DIR) return E_NOTDIR;
    return (int)fs_follow_mount(id);
}

int fs_validate_directory_table(void) {
    return volume_validate_directory(&fs_root_volume);
}

int fs_read_sector(uint32_t sector, uint8_t *buf) {
    return fs_volume_read_sector(&fs_root_volume, sector, buf);
}

int fs_write_sector(uint32_t sector, uint8_t *buf) {
    return fs_volume_write_sector(&fs_root_volume, sector, buf);
}

/* --- DISK KEY SLOT --- *
 *
 * The passphrase never reaches this file as anything but a string, and the data
 * key never leaves it as anything at all: it goes straight to
 * install_master_key(). Everything below is the disk half of the arrangement -
 * reading and writing the slot, and deciding what a missing one means. The
 * cryptography is in crypto/keyslot.c and the prompt is in kernel/core/kernel.c,
 * which is the only place that should know how to ask a human for anything.
 */

/**
 * @brief Whether the mounted disk carries a key slot.
 */
int fs_keyslot_present(void) {
    return fs_mounted && keyslot_is_present(&fs_super.keyslot);
}

/**
 * @brief Creates this disk's data key and locks it behind a passphrase.
 *
 * For a disk that has just been formatted, and only then - the caller checks
 * fs_was_formatted first. The data key is generated here rather than derived
 * from anything, so that changing the passphrase later can leave it alone.
 *
 * @param passphrase What the user typed.
 * @return E_OK, or a negative errno with nothing written to the disk.
 */
int fs_keyslot_install(const char *passphrase) {
    uint8_t dek[KEYSLOT_KEY_LEN];
    int res;

    if (!fs_mounted) return E_NODEV;

    /*
     * A weak pool is survivable and a dead one is not. generate_random_bytes()
     * reports ENTROPY_WEAK when it could not reach RDRAND, and what it still
     * guarantees in that case is uniqueness - which is enough for this key to be
     * different on every install, and is written down in entropy.h as not being
     * enough for it to be unpredictable. ENTROPY_FAIL means the pool was never
     * seeded, and a data key from an unseeded pool is not a key.
     */
    if (generate_random_bytes(dek, KEYSLOT_KEY_LEN) == ENTROPY_FAIL) {
        klog(LOG_LEVEL_ERROR, "VFS", "Refusing to create a data key: entropy pool unavailable.");
        return E_IO;
    }

    res = keyslot_create(&fs_super.keyslot, passphrase, dek);
    if (res != E_OK) {
        volatile uint8_t *p = dek;
        for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) p[i] = 0;
        return res;
    }

    res = save_superblock(&fs_root_volume);
    if (res == E_OK) res = bcache_flush_dev(fs_root_volume.device);
    if (res == E_OK) install_master_key(dek);

    {
        volatile uint8_t *p = dek;
        for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) p[i] = 0;
    }
    return res;
}

/**
 * @brief Opens the slot with a passphrase and hands the data key to the kernel.
 *
 * @param passphrase What the user typed.
 * @return E_OK, E_ACCES when the passphrase is wrong, E_INVAL for a slot this
 *         build will not work with, or E_NODEV when nothing is mounted.
 */
int fs_keyslot_unlock(const char *passphrase) {
    uint8_t dek[KEYSLOT_KEY_LEN];
    int res;

    if (!fs_mounted) return E_NODEV;
    if (!keyslot_is_present(&fs_super.keyslot)) return E_INVAL;

    res = keyslot_unwrap(&fs_super.keyslot, passphrase, dek);
    if (res == E_OK) install_master_key(dek);

    {
        volatile uint8_t *p = dek;
        for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) p[i] = 0;
    }
    return res;
}

/**
 * @brief Moves the slot to a new passphrase, leaving the data key alone.
 *
 * This is the whole reason the slot wraps a key instead of being one. The disk
 * is not touched beyond the superblock: every file stays encrypted under the
 * same data key it always was, and a passphrase change is one sector rather than
 * a pass over the file system that cannot be made atomic.
 *
 * The old passphrase is verified by unwrapping rather than by comparing anything
 * - there is nothing stored to compare against, and that is deliberate.
 *
 * Its caller is the SETKEY syscall, which arrives in the next commit. It is
 * written here because it belongs beside the two functions above and because
 * splitting it out would put this file in two commits.
 *
 * @param old_pass The current passphrase.
 * @param new_pass What to replace it with.
 * @return E_OK, E_ACCES when the old passphrase is wrong, or E_INVAL when the
 *         new one is unusable - in which case the old slot is left intact.
 */
int fs_keyslot_rewrap(const char *old_pass, const char *new_pass) {
    uint8_t dek[KEYSLOT_KEY_LEN];
    keyslot_t fresh;
    int res;

    if (!fs_mounted) return E_NODEV;

    /*
     * IMMUTABLE means the disk is read-only, and this writes sector 0.
     *
     * The guard is here rather than in sys_setkey() because this is where the
     * other eight live, and because a rule enforced at the syscall layer is a
     * rule the next in-kernel caller does not get. ">=" rather than "==" for the
     * reason v0.10.1 converted seven of those eight: IMMUTABLE is the last level
     * today, and an "==" test is a door that opens by itself the day somebody
     * appends a stricter one to the enum.
     */
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) return E_ROFS;

    if (!keyslot_is_present(&fs_super.keyslot)) return E_INVAL;

    res = keyslot_unwrap(&fs_super.keyslot, old_pass, dek);
    if (res != E_OK) return res;

    /*
     * Built somewhere else and copied in only once it is complete. Writing
     * straight into fs_super.keyslot would leave the live slot half-rewritten if
     * the new passphrase turned out to be unusable, and the disk would then hold
     * a slot that neither passphrase opens.
     */
    res = keyslot_create(&fresh, new_pass, dek);

    if (res == E_OK) {
        ft_memcpy(&fs_super.keyslot, &fresh, sizeof(keyslot_t));
        res = save_superblock(&fs_root_volume);
        if (res == E_OK) res = bcache_flush_dev(fs_root_volume.device);
    }

    {
        volatile uint8_t *p = dek;
        for (uint32_t i = 0; i < KEYSLOT_KEY_LEN; i++) p[i] = 0;
        ft_memset(&fresh, 0, sizeof(fresh));
    }
    return res;
}

/* --- VFS API (READ / WRITE / DELETE) --- */

/**
 * @brief fs_list_files
 */
void fs_list_files(void) {
    vfs_lock();
    printk("File List:\n");
    printk("----------------------------------------\n");
    int found = 0;
    for (int i = 0; i < fs_max_entries; i++) {
        if (dir_table[i].is_used == 1) {
            printk("    %s \t\t Size: %d Byte \t Cluster: %d\n",
            dir_table[i].filename, dir_table[i].file_size, dir_table[i].start_cluster);
            found = 1;
        }
    }
    if (!found) printk(" (File system is empty or directory not found)\n");
    printk("----------------------------------------\n");
    vfs_unlock();
}

/**
 * @brief fs_read_raw
 * @param file
 * @param buffer
 * @param size
 * @return int
 */
int fs_read_raw(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    if (!file) return E_INVAL;
    fs_volume_t *volume = fs_volume_for(file->volume_prefix);
    if (!volume) return E_NODEV;

    if (file->inode_idx >= 0 && file->inode_idx < volume->entry_count) {
        /*
         * No interrupt frame is passed. This runs several frames below the
         * syscall entry and does not have the live one; handing over
         * &current_task->regs instead - a saved copy that merely looks like a
         * frame - let the lock's sleep path write through the task's stored
         * context and desynchronise current_task from CR3. With 0 the lock
         * degrades to spinning, which never happens because the kernel is not
         * preemptible.
         */
        rwlock_acquire_read(&volume->locks[file->inode_idx], 0);
    }
    if (file->current_offset >= file->file_size) {
        if (file->inode_idx >= 0 && file->inode_idx < volume->entry_count) rwlock_release_read(&volume->locks[file->inode_idx]);
        return 0;
    }

    if (size > file->file_size - file->current_offset)
        size = file->file_size - file->current_offset;

    /*
     * The walk is in clusters and the copying is in sectors, which is the whole
     * of what the v2 format changed here. A cluster is FS_CLUSTER_SECTORS
     * sectors, so the chain is followed a cluster at a time and each one is read
     * a sector at a time - the block cache and the ATA driver both work in
     * sectors, and a "cluster read" that was anything other than that loop would
     * be a second I/O path to keep correct.
     */
    uint32_t cluster_bytes = 512u * volume->super.cluster_sectors;
    uint32_t bytes_read = 0;
    uint32_t current_cluster = file->start_cluster;
    uint32_t clusters_to_skip = file->current_offset / cluster_bytes;

    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        if (current_cluster == FAT_EOF || current_cluster >= volume->clusters) {
            if (file->inode_idx >= 0 && file->inode_idx < volume->entry_count)
                rwlock_release_read(&volume->locks[file->inode_idx]);
            return 0;
        }
        current_cluster = volume->fat[current_cluster];
    }

    uint32_t offset_in_cluster = file->current_offset % cluster_bytes;

    while (bytes_read < size && current_cluster != FAT_EOF) {
        if (current_cluster >= volume->clusters) { break; }

        uint32_t base = fs_volume_cluster_sector(volume, current_cluster);

        while (offset_in_cluster < cluster_bytes && bytes_read < size) {
            uint8_t sector_buf[512];
            uint32_t sector_in_cluster = offset_in_cluster / 512;
            uint32_t offset_in_sector = offset_in_cluster % 512;

            int rc = fs_volume_read_sector(volume, base + sector_in_cluster, sector_buf);
            if (rc != E_OK) {
                if (file->inode_idx >= 0 && file->inode_idx < volume->entry_count)
                    rwlock_release_read(&volume->locks[file->inode_idx]);
                return bytes_read ? (int)bytes_read : rc;
            }

            uint32_t bytes_to_copy = 512 - offset_in_sector;
            if (bytes_to_copy > (size - bytes_read)) {
                bytes_to_copy = size - bytes_read;
            }

            for (uint32_t i = 0; i < bytes_to_copy; i++) {
                buffer[bytes_read + i] = sector_buf[offset_in_sector + i];
            }

            file->current_offset += bytes_to_copy;
            bytes_read += bytes_to_copy;
            offset_in_cluster += bytes_to_copy;
        }

        current_cluster = volume->fat[current_cluster];
        offset_in_cluster = 0;
    }
    if (file->inode_idx >= 0 && file->inode_idx < volume->entry_count) {
        rwlock_release_read(&volume->locks[file->inode_idx]);
    }
    return bytes_read;
}

/**
 * @brief fs_read
 * @param file
 * @param buffer
 * @param size
 * @return int
 */
int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    if (!file) return E_INVAL;
    fs_volume_t *volume = fs_volume_for(file->volume_prefix);
    if (!volume) return E_NODEV;

    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        /* LOCKDOWN destroys the key but outranks CRYPTO_ENFORCED, so this
         * branch is still taken afterwards. Refuse rather than decrypt with
         * zeroes and hand the caller plausible-looking garbage. */
        const uint8_t *key = volume_key(volume);
        if (!key) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted read refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_read_encrypted(file, buffer, size, key);
    }
    return fs_read_raw(file, buffer, size);
}

/**
 * @brief fs_write_buffered
 *
 * Accumulates a write instead of performing it. See the note on vfs_file_t for
 * why there is no streaming path: the stored form of a file is one AES-CBC blob
 * authenticated over its whole plaintext, so it can only be replaced, never
 * extended. fs_commit_writes() does the replacing, once.
 *
 * The buffer doubles rather than growing by the request size, so a program
 * writing in small chunks - /bin/cp uses 64 bytes - does not re-copy the whole
 * file on every call.
 *
 * @param file Open file, opened for writing.
 * @param data Bytes to append.
 * @param size Number of bytes.
 * @return The count written, or a negative error code.
 */
int fs_write_buffered(vfs_file_t *file, const uint8_t *data, uint32_t size) {
    if (!file || !data) return E_INVAL;
    if (size == 0) return 0;

    if (size > MAX_FILE_WRITE_BYTES || file->write_len > MAX_FILE_WRITE_BYTES - size) {
        klog(LOG_LEVEL_WARN, "VFS", "Refusing a write past the maximum file size.");
        return E_FBIG;
    }

    if (file->write_len + size > file->write_cap) {
        uint32_t new_cap = file->write_cap ? file->write_cap : 512;
        while (new_cap < file->write_len + size) new_cap *= 2;
        if (new_cap > MAX_FILE_WRITE_BYTES) new_cap = MAX_FILE_WRITE_BYTES;

        uint8_t *grown = (uint8_t *)krealloc(file->write_buf, new_cap);
        if (!grown) return E_NOMEM;

        file->write_buf = grown;
        file->write_cap = new_cap;
    }

    for (uint32_t i = 0; i < size; i++) {
        file->write_buf[file->write_len + i] = data[i];
    }
    file->write_len += size;
    file->dirty = 1;

    return (int)size;
}

/**
 * @brief fs_commit_writes
 *
 * Writes the accumulated buffer out as the file's new contents and releases it.
 * fs_atomic_update() handles the encryption and the .tmp/swap dance, and refuses
 * outright when the master key has been destroyed rather than writing something
 * nobody can read back.
 *
 * A file opened for writing but never written to arrives here with dirty set and
 * no buffer, and commits zero bytes - that is what makes opening for writing a
 * truncation.
 *
 * @param file Open file; may have nothing to commit.
 * @return E_OK, or whatever fs_atomic_update() reported.
 */
int fs_commit_writes(vfs_file_t *file) {
    if (!file) return E_INVAL;
    if (!file->dirty) return E_OK;
    fs_volume_t *volume = fs_volume_for(file->volume_prefix);

    int res = E_OK;

    if (!volume || !volume->mounted) {
        res = E_NODEV;
    } else if (file->inode_idx < 0 || file->inode_idx >= volume->entry_count) {
        res = E_BADF;
    } else {
        fs_id_t parent_id = volume->prefix | volume->entries[file->inode_idx].parent_id;
        /* An empty commit still needs a valid pointer; fs_create_file() is happy
         * with a zero length, which is how /bin/touch already creates files. */
        const uint8_t *content = file->write_buf ? file->write_buf : (const uint8_t *)"";

        res = fs_atomic_update(file->filename, content, file->write_len, parent_id);
        if (res == E_OK) file->file_size = file->write_len;
    }

    if (file->write_buf) {
        kfree(file->write_buf);
        file->write_buf = 0;
    }
    file->write_cap = 0;
    file->write_len = 0;
    file->dirty = 0;

    return res;
}

/**
 * @brief fs_size
 *
 * Reports the number of bytes a caller can actually read out of an open file,
 * which is not the same thing as the size recorded in the directory table.
 * Under SEC_LEVEL_CRYPTO_ENFORCED - the default - what is on the disk is
 * ciphertext: an IV, a header, and the plaintext padded to an AES block. A
 * stat() that handed back dir_table's file_size would therefore be wrong for
 * every regular file in a normally configured system, and an lseek(SEEK_END)
 * built on it would land past the end of the data every time.
 *
 * The branch mirrors fs_read() deliberately, including the refusal after
 * LOCKDOWN: with the master key destroyed the encrypted path is still selected,
 * and reporting the raw ciphertext length instead of failing would be answering
 * a question we can no longer answer.
 *
 * @param file Open file to measure.
 * @param out_size Receives the readable byte count on success.
 * @return E_OK on success, or a negative error code.
 */
int fs_size(vfs_file_t *file, uint32_t *out_size) {
    if (!file) return E_INVAL;
    fs_volume_t *volume = fs_volume_for(file->volume_prefix);
    if (!volume) return E_NODEV;

    if (!out_size) return E_INVAL;

    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        const uint8_t *key = volume_key(volume);
        if (!key) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted size query refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_size_encrypted(file, key, out_size);
    }

    *out_size = file->file_size;
    return E_OK;
}

/**
 * @brief fs_create_file_raw
 * @param name
 * @param content
 * @param size
 * @param parent_id
 * @return int
 */
int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;

    vfs_lock();

    /*
     * A name too long to store is refused rather than shortened. safe_strcpy()
     * truncates, which was academic while a name could be 255 bytes and is not
     * now that it is 63: a file created under a name the user did not choose is
     * a file they cannot find again, and two long names that share a prefix
     * become the same file.
     */
    if (!valid_entry_name(name)) {
        vfs_unlock(); return E_INVAL;
    }

    if (!fs_dir_exists(parent_id)) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Rejected file creation under a non-existent parent", parent_id);
        vfs_unlock(); return E_NOENT;
    }

    uint32_t c_uid = (current_task != 0) ? current_task->uid : 0;
    uint32_t c_gid = (current_task != 0) ? current_task->gid : 0;
    uint32_t now = fs_now();

    /*
     * The name comparison that used to sit here is gone.
     *
     * It refused any non-root creation of a file called "passwd" - anywhere, so
     * `touch /tmp/passwd` was denied while `touch /tmp/shadow` was not, which is
     * the shape of every rule that decides by name rather than by mode. What it
     * was protecting is protected properly now: /etc is 0755 and owned by root,
     * so check_vfs_access() refuses a user write to it before this is reached,
     * and /etc/passwd carries 0644 root-owned, so check_file_access() refuses
     * opening it for writing.
     *
     * v0.9.1 retired the comparisons in check_vfs_access(). These three, down
     * here in the VFS, survived it because nobody was looking below the syscall
     * layer.
     */

    int existing_idx = -1;
    int free_idx = -1;
    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used && volume->entries[i].parent_id == (uint16_t)parent_id && ft_strcmp(volume->entries[i].filename, name) == 0) {
            existing_idx = i;
        }
        if (i > 0 && volume->entries[i].is_used == 0 && free_idx == -1) {
            free_idx = i;
        }
    }

    if (existing_idx != -1) { vfs_unlock(); return E_EXIST; }
    if (free_idx == -1) {
        klog(LOG_LEVEL_ERROR, "VFS", "File system (Directory table) full!");
        vfs_unlock(); return E_NFILE;
    }

    uint32_t cluster_bytes = 512u * volume->super.cluster_sectors;
    uint32_t clusters_needed = (size == 0) ? 1 : (1 + (size - 1) / cluster_bytes);
    uint32_t new_cluster = allocate_fat_chain(volume, clusters_needed);

    if (new_cluster == FAT_FREE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Disk is completely full or not enough space!");
        vfs_unlock(); return E_NOSPC;
    }

    uint32_t bytes_written = 0;
    uint32_t current_cluster = new_cluster;

    for (uint32_t c = 0; c < clusters_needed; c++) {
        if (current_cluster == FAT_EOF || current_cluster >= volume->clusters) break;

        uint32_t base = fs_volume_cluster_sector(volume, current_cluster);

        /*
         * Every sector of the cluster is written, not only the ones the file
         * reaches into. A cluster handed out from the free list may hold what a
         * deleted file left in it, and a short file that wrote only its first
         * sector would leave the rest of its own storage carrying somebody
         * else's bytes - readable through nothing today, and one lseek past the
         * end away from being readable tomorrow.
         */
        for (uint32_t s = 0; s < volume->super.cluster_sectors; s++) {
            uint8_t sector_buf[512];
            ft_memset(sector_buf, 0, 512);

            uint32_t chunk_size = 0;
            if (bytes_written < size) {
                chunk_size = (size - bytes_written > 512) ? 512 : (size - bytes_written);
                for (uint32_t i = 0; i < chunk_size; i++) {
                    sector_buf[i] = content[bytes_written + i];
                }
            }

            int rc = fs_volume_write_sector(volume, base + s, sector_buf);
            if (rc != E_OK) {
                /* This chain has not been named in the directory or submitted
                 * to the on-disk FAT. Release it locally so a retry can reuse it. */
                uint32_t cluster = new_cluster;
                while (cluster >= FS_FIRST_DATA_CLUSTER && cluster < volume->clusters) {
                    uint32_t next = volume->fat[cluster];
                    volume->fat[cluster] = FAT_FREE;
                    cluster = next;
                }
                vfs_unlock();
                return rc;
            }
            bytes_written += chunk_size;
        }

        current_cluster = volume->fat[current_cluster];
    }

    ft_memset(volume->entries[free_idx].filename, 0, MAX_FILENAME);
    safe_strcpy(volume->entries[free_idx].filename, name, MAX_FILENAME);

    volume->entries[free_idx].start_cluster = new_cluster;
    volume->entries[free_idx].file_size = size;
    volume->entries[free_idx].is_used = 1;
    volume->entries[free_idx].file_type = FT_REGULAR;
    volume->entries[free_idx].entry_id = (fs_id_t)free_idx;
    volume->entries[free_idx].parent_id = (uint16_t)parent_id;
    volume->entries[free_idx].owner_uid = c_uid;
    volume->entries[free_idx].owner_gid = c_gid;
    volume->entries[free_idx].mode = FS_MODE_DEFAULT_FILE;
    volume->entries[free_idx].ctime = now;
    volume->entries[free_idx].mtime = now;

    volume->directory_pending = 1;
    int rc = save_fat_to_disk(volume);
    if (rc == E_OK) rc = save_directory_to_disk(volume);
    vfs_unlock();
    return rc;
}

/**
 * @brief fs_create_file
 * @param name
 * @param content
 * @param size
 * @param parent_id
 * @return int
 */
int fs_create_file(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File creation blocked!");
        return E_ROFS;
    }
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        /* Writing with a zeroed key would produce a file nobody can ever read
         * back - a silent, permanent loss. Refuse instead. */
        const uint8_t *key = volume_key(volume);
        if (!key) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted write refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_create_encrypted(name, content, size, key, parent_id);
    }
    return fs_create_file_raw(name, content, size, parent_id);
}

/**
 * @brief fs_open
 * @param name
 * @param parent_id
 * @param file
 * @return int
 */
int fs_open(const char *name, fs_id_t parent_id, vfs_file_t *file) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;

    vfs_lock();
    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used == 1 &&
            volume->entries[i].parent_id == (uint16_t)parent_id &&
            ft_strcmp(volume->entries[i].filename, name) == 0) {
            if (volume->entries[i].file_type == FT_DIR) { vfs_unlock(); return E_ISDIR; }
            safe_strcpy(file->filename, volume->entries[i].filename, MAX_FILENAME);
            file->start_cluster = volume->entries[i].start_cluster;
            file->file_size = volume->entries[i].file_size;
            file->current_offset = 0;
            file->ref_count = 1;
            file->inode_idx = i;
            file->volume_prefix = volume->prefix;

            /*
             * The write-buffer fields are initialised here because this is the
             * only constructor a vfs_file_t has, and its callers do not zero it:
             * sys_open() hands over raw kmalloc'd memory and sys_cat_raw() a
             * bare stack local. Left as they came, a garbage `dirty` would make
             * fs_commit_writes() free a garbage pointer on the first close.
             */
            file->write_buf = 0;
            file->write_len = 0;
            file->write_cap = 0;
            file->dirty = 0;

            vfs_unlock();
            return E_OK;
        }
    }
    vfs_unlock();
    return E_NOENT;
}

/**
 * @brief fs_delete
 * @param name
 * @param parent_id
 * @return int
 */
int fs_delete(const char *name, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;

    vfs_lock();
    if (parent_id == 0 && secondary_volume.mounted &&
        ft_strcmp(name, "mnt") == 0) {
        vfs_unlock();
        return E_BUSY;
    }
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File deletion blocked!");
        vfs_unlock(); return E_ROFS;
    }
    /* The "passwd" name comparison is gone; see fs_create_file_raw() for why.
     * Deleting from /etc needs write permission on /etc, which is 0755 and
     * root's, and the syscall layer asks that before this is reached. */

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used == 1 &&
            volume->entries[i].parent_id == (uint16_t)parent_id &&
            ft_strcmp(volume->entries[i].filename, name) == 0) {
            /*
             * A directory that still holds something is refused.
             *
             * A child records its parent's entry id, and deleting the parent
             * used to clear that slot and stop there. The children stayed behind
             * pointing at an id that no longer described anything, unreachable
             * through any path - and visible again the moment the slot was
             * reused, as somebody else's contents.
             *
             * An empty directory still deletes, which is what makes this
             * rmdir(2) rather than a refusal to remove directories at all:
             * there is no separate rmdir in this system, so refusing outright
             * would leave a directory with no way to remove it.
             *
             * Entry id 0 is the root and is never allocated to a real entry -
             * fs_mkdir() starts its search at 1 - so scanning for children by
             * entry id cannot confuse a directory's children with the root's.
             *
             * The comparison used to be against the loop index narrowed to a
             * byte, which was two mistakes that cancelled below 256 and stopped
             * cancelling above it. An entry id and a slot index happen to be
             * equal here (entry_id is assigned the slot it lands in), so the
             * confusion was invisible; the byte was not. A directory in slot 256
             * looked for children whose parent_id was 0, found the root's
             * instead, and either refused a delete it should have allowed or -
             * with an empty root - deleted a directory that still held files and
             * orphaned exactly what this check exists to protect.
             */
            if (volume->entries[i].file_type == FT_DIR) {
                for (int c = 0; c < volume->entry_count; c++) {
                    if (volume->entries[c].is_used == 1 && volume->entries[c].parent_id == volume->entries[i].entry_id) {
                        klog(LOG_LEVEL_WARN, "VFS", "Refusing to delete a directory that is not empty.");
                        vfs_unlock(); return E_NOTEMPTY;
                    }
                }
            }

            /*
             * The index is bounded before every use.
             *
             * start_cluster comes off the disk, and validate_directory_table()
             * checks it at mount as of v0.10.0 - but the bound stays here. The
             * table is checked once at the door and walked a great many times
             * afterwards, and a guard that costs a comparison is not worth
             * removing on the strength of a check that ran minutes ago.
             *
             * Unbounded, this loop turned "rm <file>" into a four-byte kernel
             * write at an offset the image chose, and then followed whatever it
             * read there.
             */
            uint32_t cluster_to_free = volume->entries[i].start_cluster;
            while (cluster_to_free != FAT_EOF && cluster_to_free != FAT_FREE) {
                if (cluster_to_free >= volume->clusters) {
                    klog_int(LOG_LEVEL_ERROR, "VFS",
                             "Refusing to follow an out-of-range FAT entry while deleting", (int)cluster_to_free);
                    break;
                }
                uint32_t next_cluster = volume->fat[cluster_to_free];
                volume->fat[cluster_to_free] = FAT_FREE;
                cluster_to_free = next_cluster;
            }
            volume->fat_pending = 1;
            volume->entries[i].is_used = 0;
            ft_memset(volume->entries[i].filename, 0, MAX_FILENAME);
            int rc = save_directory_to_disk(volume);
            if (rc == E_OK) rc = save_fat_to_disk(volume);
            vfs_unlock(); return rc;
        }
    }
    vfs_unlock();
    return E_NOENT;
}

/**
 * @brief fs_rename
 * @param old_name
 * @param new_name
 * @param parent_id
 * @return int
 */
int fs_rename(const char *old_name, const char *new_name, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;
    if (!valid_entry_name(new_name)) return E_INVAL;

    vfs_lock();
    if (parent_id == 0 && secondary_volume.mounted &&
        ft_strcmp(old_name, "mnt") == 0) {
        vfs_unlock();
        return E_BUSY;
    }
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File renaming blocked!");
        vfs_unlock(); return E_ROFS;
    }
    /* The "passwd" name comparison is gone; see fs_create_file_raw() for why.
     * This one also refused renaming anything *to* "passwd", which made the name
     * itself reserved system-wide - a user could not call their own file that in
     * their own home directory. */

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used == 1 && volume->entries[i].parent_id == (uint16_t)parent_id && ft_strcmp(volume->entries[i].filename, new_name) == 0) {
            klog(LOG_LEVEL_WARN, "VFS", "Rename failed: File with target name already exists!");
            vfs_unlock(); return E_EXIST;
        }
    }

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used == 1 &&
            volume->entries[i].parent_id == (uint16_t)parent_id &&
            ft_strcmp(volume->entries[i].filename, old_name) == 0) {

            ft_memset(volume->entries[i].filename, 0, MAX_FILENAME);
            safe_strcpy(volume->entries[i].filename, new_name, MAX_FILENAME);

            /* The entry changed, the contents did not, so this is a ctime and
             * not an mtime - the distinction the two fields exist to make. */
            volume->entries[i].ctime = fs_now();

            int rc = save_directory_to_disk(volume);
            vfs_unlock(); return rc;
        }
    }
    vfs_unlock();
    return E_NOENT;
}

/**
 * @brief fs_atomic_update
 * @param name
 * @param content
 * @param size
 * @param parent_id
 * @return int
 */
int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;

    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_WARN, "VFS", "fs_atomic_update: Blocked by IMMUTABLE security level");
        return E_ROFS;
    }
    // CRYPTO_ENFORCED: atomic updates also go through encrypted path
    // This is handled by using fs_create_file() instead of fs_create_file_raw()
    // (see Fix 2 above - already addressed by calling fs_create_file)
    vfs_lock();

    int orig_idx = -1;
    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used && volume->entries[i].parent_id == (uint16_t)parent_id && ft_strcmp(volume->entries[i].filename, name) == 0) {
            orig_idx = i; break;
        }
    }

    if (orig_idx == -1) {
        vfs_unlock();
        return fs_create_file(name, content, size, parent_id);
    }

    if (volume->entries[orig_idx].file_type != FT_REGULAR) {
        vfs_unlock();
        return E_ISDIR;
    }
    char tmp_name[MAX_FILENAME];
    int chosen = 0;
    for (int n = 0; n < volume->entry_count; n++) {
        kbprintf(tmp_name, sizeof(tmp_name), 0, ".update-%x", n);
        if (fs_get_entry_idx(tmp_name, parent_id) < 0) { chosen = 1; break; }
    }
    if (!chosen) { vfs_unlock(); return E_NOSPC; }
    int res = fs_create_file(tmp_name, content, size, parent_id);
    if (res != E_OK) { vfs_unlock(); return res; }
    int tmp_idx = fs_get_entry_idx(tmp_name, parent_id);
    if (tmp_idx < 0) { vfs_unlock(); return E_IO; }
    uint32_t old_cluster = volume->entries[orig_idx].start_cluster;
    uint32_t old_size = volume->entries[orig_idx].file_size;
    {
        /* No live interrupt frame here either; see fs_read_raw(). */
        rwlock_acquire_write(&volume->locks[orig_idx], 0);

        volume->entries[orig_idx].start_cluster = volume->entries[tmp_idx].start_cluster;
        volume->entries[orig_idx].file_size = volume->entries[tmp_idx].file_size;

        /*
         * The contents changed, so the modification time does. Everything else
         * about the entry stays: this is the same file with new bytes in it, and
         * taking the owner or the creation time from the temporary would make a
         * write look like a new file to anything that asked.
         */
        volume->entries[orig_idx].mtime = fs_now();

        volume->entries[tmp_idx].start_cluster = old_cluster;
        volume->entries[tmp_idx].file_size = old_size;

        rwlock_release_write(&volume->locks[orig_idx]);
    }

    res = save_directory_to_disk(volume);
    if (res == E_OK) res = fs_delete(tmp_name, parent_id);
    vfs_unlock();
    return res;
}

/**
 * @brief Finds the directory table slot an entry id occupies.
 *
 * @param entry_id Entry to find.
 * @return Slot index, or -1.
 */
static int fs_slot_of(fs_id_t entry_id) {
    fs_volume_t *volume = fs_volume_for(entry_id);
    if (!volume) return -1;

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used && volume->entries[i].entry_id == (uint16_t)entry_id) return i;
    }
    return -1;
}

/**
 * @brief Sets an entry's permission bits.
 *
 * @param entry_id Entry to change.
 * @param mode New permission bits.
 * @return E_OK, or a negative errno.
 */
int fs_chmod(fs_id_t entry_id, uint16_t mode) {
    fs_volume_t *volume = fs_volume_for(entry_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) return E_ROFS;

    vfs_lock();

    int idx = fs_slot_of(entry_id);
    if (idx < 0) { vfs_unlock(); return E_NOENT; }

    volume->entries[idx].mode = (uint16_t)(mode & FS_MODE_PERM_MASK);
    /* The entry changed and the contents did not, which is the distinction the
     * two timestamps exist to make. */
    volume->entries[idx].ctime = fs_now();

    int rc = save_directory_to_disk(volume);
    vfs_unlock();
    return rc;
}

/**
 * @brief Sets an entry's owner and group.
 *
 * @param entry_id Entry to change.
 * @param uid New owner.
 * @param gid New group.
 * @return E_OK, or a negative errno.
 */
int fs_chown(fs_id_t entry_id, uint32_t uid, uint32_t gid) {
    fs_volume_t *volume = fs_volume_for(entry_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) return E_ROFS;

    vfs_lock();

    int idx = fs_slot_of(entry_id);
    if (idx < 0) { vfs_unlock(); return E_NOENT; }

    volume->entries[idx].owner_uid = uid;
    volume->entries[idx].owner_gid = gid;
    volume->entries[idx].ctime = fs_now();

    int rc = save_directory_to_disk(volume);
    vfs_unlock();
    return rc;
}

/**
 * @brief fs_get_entry_idx
 * @param name
 * @param parent_id
 * @return int
 */
int fs_get_entry_idx(const char *name, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return -1;

    vfs_lock();
    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used &&
            volume->entries[i].parent_id == (uint16_t)parent_id &&
            ft_strcmp(volume->entries[i].filename, name) == 0) {
            vfs_unlock(); return i;
        }
    }
    vfs_unlock();
    return -1;
}

/**
 * @brief fs_mkdir
 * @param name
 * @param parent_id
 * @return int
 */
int fs_mkdir(const char *name, fs_id_t parent_id) {
    fs_volume_t *volume = fs_volume_for(parent_id);
    if (!volume) return E_NODEV;

    if (!volume->mounted) return E_NODEV;

    vfs_lock();
    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) { vfs_unlock(); return E_ROFS; }

    /* Refused rather than truncated, as in fs_create_file_raw(). */
    if (!valid_entry_name(name)) { vfs_unlock(); return E_INVAL; }

    if (!fs_dir_exists(parent_id)) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Rejected mkdir under a non-existent parent", parent_id);
        vfs_unlock(); return E_NOENT;
    }

    for (int i = 0; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used && volume->entries[i].parent_id == (uint16_t)parent_id && ft_strcmp(volume->entries[i].filename, name) == 0) {
            vfs_unlock(); return E_EXIST;
        }
    }

    int free_idx = -1;
    for (int i = 1; i < volume->entry_count; i++) {
        if (volume->entries[i].is_used == 0) {
            free_idx = i;
            break;
        }
    }
    if (free_idx == -1) {
        klog(LOG_LEVEL_ERROR, "VFS", "Directory table full, cannot create new directory.");
        vfs_unlock(); return E_NFILE;
    }

    ft_memset(volume->entries[free_idx].filename, 0, MAX_FILENAME);
    safe_strcpy(volume->entries[free_idx].filename, name, MAX_FILENAME);

    uint32_t current_uid = (current_task != 0) ? current_task->uid : 0;
    uint32_t current_gid = (current_task != 0) ? current_task->gid : 0;
    uint32_t now = fs_now();

    volume->entries[free_idx].file_size = 0;
    volume->entries[free_idx].start_cluster = 0;
    volume->entries[free_idx].file_type = FT_DIR;
    volume->entries[free_idx].entry_id = (fs_id_t)free_idx;
    volume->entries[free_idx].parent_id = (uint16_t)parent_id;
    volume->entries[free_idx].is_used = 1;
    volume->entries[free_idx].owner_uid = current_uid;
    volume->entries[free_idx].owner_gid = current_gid;
    volume->entries[free_idx].mode = FS_MODE_DEFAULT_DIR;
    volume->entries[free_idx].ctime = now;
    volume->entries[free_idx].mtime = now;

    int rc = save_directory_to_disk(volume);
    vfs_unlock(); return rc;
}
