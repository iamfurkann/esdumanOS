#ifndef FS_H
#define FS_H

#include "types.h"
/* The superblock carries the passphrase key slot, so its layout is part of this
 * header's layout. */
#include "keyslot.h"

/**
 * @brief FAT markers indicating end of file chain or free clusters.
 */
#define FAT_EOF 0xFFFFFFFF
#define FAT_FREE 0x00000000

/**
 * @brief Longest name one directory entry can hold, terminator included.
 *
 * It was 256 until v0.9.0, which made the name 94% of a 272-byte entry - the
 * whole of the space the format needed for a mode, a group and two timestamps
 * was sitting in a field nothing had ever filled past a few dozen bytes. 64 pays
 * for all of them and for twice as many entries, and still costs less memory
 * than before.
 */
#define MAX_FILENAME 64

/**
 * @brief Longest path a system call will accept, terminator included.
 *
 * Separate from MAX_FILENAME, and that separation is the point. Both were the
 * same constant until v0.9.0, so shortening the name field would have quietly
 * shortened every path buffer with it - the buffers are filled through
 * copy_user_string() bounded by sizeof, so nothing would have overrun; a path a
 * few directories deep would simply have stopped resolving. A name is one
 * component and a path is all of them, and they have no business sharing a
 * number.
 */
#define MAX_PATH 256

/**
 * @brief Entries in the whole file system.
 *
 * Not per directory, despite the name: dir_table is one flat table and an entry's
 * place in the tree is its parent_id. Raised from 256 to 512 in v0.9.0, which
 * only became possible when the ids stopped being bytes.
 *
 * This was MAX_FILES_IN_DIR until v1.3.0, and the rename is the safety mechanism
 * rather than tidying. The table is allocated at mount now, sized from the disk,
 * so the number that bounds a loop over it is fs_max_entries and not this. Around
 * forty loops used the old name as their bound; had the name survived, every one
 * of them would have kept compiling while reading past the end of an allocation
 * smaller than the cap. Deleting the name is what made the compiler point at all
 * forty. The same mistake in reverse - a widening that compiled everywhere and
 * was wrong in eight places - cost v0.9.0 three releases.
 *
 * 8192 entries is 768 KB of table, plus the same count of inode locks. It is a
 * ceiling on what a format may choose, and appears in exactly two places:
 * format_disk() picking a geometry and load_superblock() refusing one.
 */
#define FS_MAX_ENTRIES_CAP 8192

/**
 * @brief A directory entry's identifier.
 *
 * uint8_t until v0.9.0, which capped the entire file system at 256 entries - not
 * because 256 entries is a sensible limit but because the field was one byte. It
 * has a name now so the next widening is one line rather than eighty-five.
 *
 * It travels through system calls in a 32-bit register either way, so the ABI's
 * shape did not change when this did; only the range of values that are valid.
 */
typedef uint16_t fs_id_t;

/**
 * @brief Default disk layout, used when formatting a blank disk.
 *
 * These are what a new file system is laid out with. They are not what a mounted
 * one is read with - that comes from the superblock, which records the geometry
 * it was formatted with so that changing these constants later cannot make the
 * kernel misread a disk written by an older one.
 *
 * **Every one of these is relative to the start of the partition**, not to the
 * start of the disk. The file system does not know where it lives; fs_part_start
 * holds that and is added on the way to the block cache. Keeping the superblock
 * partition-relative is what makes moving a partition cost nothing.
 *
 * The arithmetic: 512 entries of 96 bytes is 49152 bytes, exactly 96 sectors, so
 * the directory table is sectors 1 to 96. The FAT holds one uint32_t per
 * *cluster* now rather than per sector, so 4096 clusters is 16384 bytes and 32
 * more sectors, 97 to 128. Data starts at 200 as it always has, leaving 129 to
 * 199 spare - the room the next format change gets to grow into without moving a
 * single byte of anybody's file.
 */
#define FS_DIR_START_SECTOR   1
#define FS_DIR_SECTOR_COUNT   96
#define FS_FAT_START_SECTOR   97
#define FS_DATA_START_SECTOR  200

/**
 * @brief Sectors per cluster, and the reason there are clusters at all.
 *
 * Through v0.9.4 the allocation table held one entry per *sector*, which is why
 * the disk was capped at 2 MB: file_allocation_table is a static uint32_t[4096],
 * 4096 sectors, and growing past it meant growing the array - 512 KB of kernel
 * memory for a 64 MB disk. Counting in clusters of 8 sectors covers 16 MB with
 * the same 16 KB array.
 *
 * The bill, stated rather than buried: a one-byte file now occupies 4 KB. With
 * 512 entries the worst case is about 2 MB of slack on a disk that can hold 16.
 * That is the trade, and it is the trade every file system with clusters makes.
 *
 * FS_FIRST_DATA_CLUSTER is 2, as in every FAT ever written, and for the reason
 * FAT has it: 0 is FAT_FREE and an entry with no data at all - a directory, an
 * empty file - records start_cluster 0. If cluster 0 were allocatable, "no data"
 * and "data at the first cluster" would be the same value. Reserving 0 and 1
 * costs 8 KB of disk and removes the ambiguity entirely.
 */
#define FS_CLUSTER_SECTORS    8

/*
 * The most clusters a format may describe, which at FS_CLUSTER_SECTORS is a
 * little under 1 GB of data. Was FS_MAX_CLUSTERS and a fixed 4096 - a 16 MB
 * ceiling - until v1.3.0 sized the allocation table from the device.
 *
 * 262144 entries is 1 MB of kernel memory for the table, and that budget is what
 * picks the number rather than the disk: a 16 GB stick uses its first gigabyte
 * and says so. Raising the ceiling further means variable cluster sizes, which
 * fs_cluster_to_sector() already supports and load_superblock() does not yet
 * accept.
 *
 * Runtime counterpart: fs_total_clusters, which has existed all along.
 */
#define FS_MAX_CLUSTERS_CAP   262144
#define FS_FIRST_DATA_CLUSTER 2

/**
 * @brief Where the file system's partition begins, and what it is called.
 *
 * LBA 64 rather than the 2048 a modern tool would choose: 2048 sectors is 1 MB,
 * which on a disk small enough to be worth partitioning by hand is most of it.
 * 64 is a whole number of clusters and costs 32 KB.
 *
 * Type 0x7F is the byte reserved by convention for individual and experimental
 * use. That cuts both ways on purpose: this kernel will not mistake somebody
 * else's Linux or FAT partition for one of its own, and nothing else will
 * mistake this one for something it can read.
 */
#define FS_PART_START_LBA     64
#define FS_PART_TYPE          0x7F

/**
 * @brief File Type Definitions.
 * FT_REGULAR indicates a standard file.
 * FT_DIR indicates a directory.
 */
#define FT_REGULAR 0
#define FT_DIR     1

/**
 * @brief Permission bits, in the arrangement everyone already knows.
 *
 * Stored and reported as of v0.9.0, enforced as of v0.9.1: check_vfs_access()
 * reads them rather than comparing names. This comment said the enforcement was
 * still to come for two releases after it arrived, which is the failure mode a
 * comment about permissions has - it is read by people deciding whether a bit
 * matters.
 */
#define FS_MODE_DEFAULT_FILE 0644
#define FS_MODE_DEFAULT_DIR  0755
#define FS_MODE_PERM_MASK    07777

/**
 * @brief The sticky bit, in the position every Unix puts it.
 *
 * Set on a directory it means "write here, but what you wrote is yours to
 * remove". Set on anything else it means nothing here and is stored without
 * being consulted, which is the same treatment the set-user-id bits get: the
 * mask has always been 07777, so a `chmod 1777` was already recorded before
 * anything read it back.
 *
 * See fs_sticky_allows_removal() for the rule.
 */
#define FS_MODE_STICKY       01000

/**
 * @brief On-disk file entry structure representing a file or directory.
 * Packed to ensure exact byte alignment on storage media.
 *
 * Exactly 96 bytes, and the test suite asserts it: packed is a request the
 * compiler is expected to honour, and the one place a silent disagreement would
 * show up is a disk written by one build and read by another.
 *
 * Field order groups the four-byte fields first, then the two-byte, then the
 * single bytes, so the layout is the same whatever the compiler would have done
 * with alignment on its own.
 */
typedef struct {
    char     filename[MAX_FILENAME]; /**< Name, null-terminated. */
    uint32_t file_size;              /**< Size of the file in bytes. */
    uint32_t start_cluster;          /**< First cluster of the file's data; 0 when none. */
    uint32_t owner_uid;              /**< User ID of the file's owner. */
    uint32_t owner_gid;              /**< Group ID of the file's owner. */
    uint32_t mtime;                  /**< Seconds since the Unix epoch, last write. */
    uint32_t ctime;                  /**< Seconds since the Unix epoch, creation. */
    fs_id_t  entry_id;               /**< Unique identifier within the file system. */
    fs_id_t  parent_id;              /**< Parent directory's entry ID; 0 is the root. */
    uint16_t mode;                   /**< Permission bits; stored, not yet enforced. */
    uint8_t  file_type;              /**< File type (FT_REGULAR or FT_DIR). */
    uint8_t  is_used;                /**< Flag indicating if the entry is active. */
} __attribute__((packed)) disk_file_entry_t;

/**
 * @brief The first sector: what this disk is.
 *
 * Sector 0 was empty through v0.8.4 - reserved for a partition table that has not
 * arrived - and nothing anywhere on the disk said what format the rest of it was
 * in. init_fs() read the directory table and the FAT straight out of their
 * sectors and used whatever was there, so a kernel could not tell one of its own
 * disks from an older one, from a disk belonging to something else entirely, or
 * from 2 MB of noise.
 *
 * That is the kind of gap that keeps itself open: without a version field, the
 * next format change cannot recognise the disks this one writes either. So the
 * field exists now, and the geometry sits beside it - which means a later layout
 * change does not even need a new version number, only different values here.
 *
 * Padded to a whole sector. The unused tail is written as zeroes, so a field
 * added later reads as zero on a disk written before it, and zero can be made to
 * mean "not recorded".
 */
#define FS_SUPER_SECTOR   0
#define FS_SUPER_MAGIC    0x46647365u   /* "esdF", little-endian */

/**
 * @brief The on-disk format this kernel writes and is willing to mount.
 *
 * 3 as of v1.1.0, which added the passphrase key slot to the superblock. A v2
 * disk is refused by name, the way a v0.9.x disk has been since v0.10.0, and for
 * a sharper reason than geometry: a v2 disk's file contents are encrypted under
 * the key that used to be compiled into the kernel, and mounting one would mean
 * this kernel still had to carry that key. Refusing them is what lets the
 * constant be deleted rather than kept for compatibility - which is the whole
 * point of the release.
 *
 * There is no converter, and the reasoning is the same as it was for the v0.9.x
 * refusal: a conversion that has to decrypt and re-encrypt every file cannot be
 * made atomic, and one that fails halfway leaves a disk in neither format.
 */
#define FS_FORMAT_VERSION 3

/**
 * @brief The master boot record: sector 0 of the disk, not of the partition.
 *
 * Sector 0 held the superblock from v0.9.0 to v0.9.4, which is the reason this
 * arrives as a format change rather than an addition - there is no room for a
 * partition table where the superblock is sitting, and moving the superblock
 * moves every sector address behind it.
 *
 * The layout is the one every PC has used since 1983 and is not negotiable: 446
 * bytes of boot code, four sixteen-byte entries, and 0x55 0xAA. This kernel is
 * loaded by GRUB from the ISO and never executes the boot code, so those 446
 * bytes are written as zeroes - but they are left in place, because a sector 0
 * that is shaped like an MBR is one that other tools can read without being
 * told to.
 *
 * The CHS fields are written as zero. They have been meaningless on any disk
 * this side of 8 GB for thirty years, every modern tool reads the LBA fields,
 * and a fabricated CHS value would be a number that looks authoritative and is
 * not.
 */
#define MBR_SIGNATURE       0xAA55
#define MBR_PART_OFFSET     446
#define MBR_PART_COUNT      4

typedef struct {
    uint8_t  status;        /**< 0x80 bootable, 0x00 otherwise. */
    uint8_t  chs_first[3];  /**< Zero; see above. */
    uint8_t  type;          /**< FS_PART_TYPE for ours; 0 for an unused entry. */
    uint8_t  chs_last[3];   /**< Zero; see above. */
    uint32_t lba_first;     /**< First sector of the partition, from sector 0. */
    uint32_t sector_count;  /**< Sectors the partition spans. */
} __attribute__((packed)) mbr_partition_t;

typedef struct {
    uint8_t         boot_code[MBR_PART_OFFSET]; /**< Zero here; never executed. */
    mbr_partition_t partitions[MBR_PART_COUNT];
    uint16_t        signature;                  /**< MBR_SIGNATURE. */
} __attribute__((packed)) mbr_t;

typedef struct {
    uint32_t magic;           /**< FS_SUPER_MAGIC when this is one of ours. */
    uint16_t format_version;  /**< FS_FORMAT_VERSION; a higher one is refused. */
    uint16_t sector_size;     /**< Bytes per sector; 512. */
    uint32_t total_sectors;   /**< Sectors the partition spans, from its own start. */
    uint32_t dir_start;       /**< First sector of the directory table. */
    uint32_t dir_sectors;     /**< Sectors the directory table occupies. */
    uint32_t max_entries;     /**< Entries the directory table holds. */
    uint16_t entry_size;      /**< Bytes per directory entry. */
    uint16_t cluster_sectors; /**< Sectors per allocation unit; FS_CLUSTER_SECTORS. */
    uint32_t fat_start;       /**< First sector of the allocation table. */
    uint32_t data_start;      /**< Sector cluster 0 would begin at. */
    uint32_t total_clusters;  /**< Clusters the allocation table describes. */
    uint32_t flags;           /**< Reserved; zero. */
    /**
     * The passphrase-protected wrapping of this disk's data key.
     *
     * Added in v1.1.0, in the space save_superblock() has always zeroed for the
     * purpose - its comment said a field added later "reads as zero on a disk
     * written now, and zero can be given a meaning." This is that field, and the
     * meaning of zero is "no slot", which keyslot_is_present() reads off the
     * iteration count.
     *
     * The slot is not itself a secret: it is a salt, an IV, a wrapped key and a
     * tag, and all four are safe to hand to an attacker. What it is not safe to
     * do is mount a disk whose slot has been swapped for one the attacker made,
     * which is why the tag covers the parameters and not just the ciphertext.
     */
    keyslot_t keyslot;
} __attribute__((packed)) disk_superblock_t;

/**
 * @brief Bytes a superblock occupies on disk. Asserted by the test suite.
 *
 * 44 until v1.1.0 and 160 after it, and written out rather than left to sizeof
 * for the same reason disk_file_entry_t's 96 is: a field added or reordered
 * without thinking about the disk moves every byte behind it, and the disk has
 * no way to notice. The sector is 512, so there is room for this to grow again.
 */
#define FS_SUPER_DISK_SIZE 160

/**
 * @brief The geometry the mounted file system is actually being read with.
 *
 * Filled by init_fs() from the disk, not from the constants above. Every sector
 * address in it is relative to the start of the partition.
 */
extern disk_superblock_t fs_super;

/**
 * @brief Whether this boot formatted the disk.
 *
 * Read by the boot path to tell a disk that has never had a key slot from one
 * that has lost the slot it had. The first gets a passphrase; the second is
 * refused, because installing a slot generates a new data key and every file
 * already on the disk is encrypted under the old one.
 */
extern int fs_was_formatted;

/**
 * @brief Whether the mounted disk carries a passphrase key slot.
 * @return 1 when a slot is present, 0 when there is none or nothing is mounted.
 */
int fs_keyslot_present(void);

/**
 * @brief Generates this disk's data key and locks it behind a passphrase.
 * @param passphrase The passphrase to protect it with.
 * @return E_OK, or a negative errno with the disk untouched.
 */
int fs_keyslot_install(const char *passphrase);

/**
 * @brief Opens the slot and hands the data key to the kernel.
 * @param passphrase The passphrase to try.
 * @return E_OK, or E_ACCES when it is wrong.
 */
int fs_keyslot_unlock(const char *passphrase);

/**
 * @brief Re-wraps the data key under a new passphrase.
 *
 * The data key does not change, so nothing on the disk needs re-encrypting.
 *
 * @param old_pass The current passphrase.
 * @param new_pass Its replacement.
 * @return E_OK, E_ACCES when the old passphrase is wrong, or E_INVAL when the
 *         new one is unusable.
 */
int fs_keyslot_rewrap(const char *old_pass, const char *new_pass);

/**
 * @brief Installs a program embedded in the kernel image onto the disk.
 *
 * The embedded programs are encrypted at build time with ESDUMAN_ELF_KEY_HEX, a
 * key that necessarily ships inside the same image. This decrypts one with that
 * key and writes it back out under the disk's own key, so that what ends up on
 * the disk is protected by the user's passphrase like everything else.
 *
 * Replaces the fs_create_file_raw() calls that used to store the build-encrypted
 * blob verbatim - which left every /bin program on every disk readable with a
 * key published in the ISO.
 *
 * @param name      Name to create.
 * @param blob      The embedded image.
 * @param blob_len  Its length in bytes.
 * @param parent_id Directory to create it in.
 * @return E_OK, or a negative errno.
 */
int fs_install_image_asset(const char *name, const uint8_t *blob, uint32_t blob_len,
                           fs_id_t parent_id);

/**
 * @brief Whether a file system was successfully mounted.
 *
 * Zero when the disk held something this kernel would not touch. Every entry
 * point checks it: refusing to mount and then writing anyway would be worse than
 * not checking at all.
 */
extern int fs_mounted;

/**
 * @brief The partition's first sector, counted from the start of the disk.
 *
 * The one value that turns a partition-relative address into a disk address.
 * fs_read_sector() and fs_write_sector() add it; nothing else should, and
 * nothing else in the file system should call the block cache directly.
 */
extern uint32_t fs_part_start;

/**
 * @brief Sectors in the partition, as the device and the MBR agree on them.
 */
extern uint32_t fs_max_sectors;

/**
 * @brief Clusters this file system may address, capped at FS_MAX_CLUSTERS_CAP.
 */
extern uint32_t fs_total_clusters;

/**
 * @brief The partition-relative sector a cluster begins at.
 *
 * Pure, and in the header because the tests need it and because a conversion
 * that lives in one place is a conversion that cannot disagree with itself.
 *
 * @param cluster Cluster index; FS_FIRST_DATA_CLUSTER or above for real data.
 * @return First sector of that cluster, relative to the partition.
 */
static inline uint32_t fs_cluster_to_sector(uint32_t cluster) {
    return fs_super.data_start + cluster * fs_super.cluster_sectors;
}

/**
 * @brief The file system's only door to the disk.
 *
 * Takes a sector named relative to the partition and adds fs_part_start on the
 * way to the block cache. Nothing inside the file system calls the cache
 * directly any more: a single missed offset would read the right sector of the
 * wrong place, which is a silent wrong answer, and one door is the only defence
 * against that which does not depend on remembering.
 *
 * The master boot record is the one exception, and deliberately so - it lives at
 * absolute sector 0 and belongs to the disk rather than to any file system.
 *
 * @param rel_sector Sector, counted from the first sector of the partition.
 * @param buffer 512 bytes.
 */
/**
 * @brief Reads a sector named relative to the partition.
 *
 * Returns a status as of v1.2.0. It returned void, so nothing above it could
 * learn that the disk had failed - and the block cache handed back a zeroed
 * buffer on failure, which the mount path read as a blank disk and formatted.
 *
 * @return E_OK, or a negative errno from the device.
 */
int fs_read_sector(uint32_t rel_sector, uint8_t *buffer);

/** @brief Writes a partition-relative sector. See fs_read_sector(). */
int fs_write_sector(uint32_t rel_sector, uint8_t *buffer);

/**
 * @brief Checks the directory table's own invariants.
 *
 * Run at mount, before anything is allowed to believe the table. The tree rests
 * on things nothing enforced until v0.10.0: that an entry's id is the slot it
 * occupies, that a parent chain terminates at the root, and that a start cluster
 * is inside the file system. A crafted image could break any of them.
 *
 * Not static, so a test can corrupt an entry and watch this refuse it. That is
 * the only way to cover the branch: the alternative is an image built to be
 * broken, and the check would then be tested by the thing it exists to catch.
 *
 * @return 1 when every used entry is consistent, 0 otherwise.
 */
int fs_validate_directory_table(void);

/**
 * @brief In-memory Virtual File System (VFS) file structure.
 * Maintains runtime state for open files, including reference counts for safety.
 */
typedef struct {
    char     filename[MAX_FILENAME]; /**< File name. */
    uint32_t file_size;              /**< Size of the file in bytes. */
    uint32_t current_offset;         /**< Current read/write offset within the file. */
    uint32_t start_cluster;          /**< First cluster of the file's data; 0 when none. */
    int      ref_count;              /**< Reference count to prevent Use-After-Free vulnerabilities! [SECURITY PATCH ADDED] */
    int      inode_idx;              /**< Index in the dir_table for locking */

    /*
     * Buffered writes, committed as a whole when the last descriptor closes.
     *
     * Appending to a file is not possible with the on-disk format: under
     * SEC_LEVEL_CRYPTO_ENFORCED - the default - a file is one AES-CBC blob with
     * an HMAC over the entire plaintext, so adding a byte at the end means
     * re-encrypting and re-authenticating all of it. The VFS has no streaming
     * write primitive either; the only way to change a file's contents is
     * fs_atomic_update(), which replaces the whole thing.
     *
     * So writes accumulate here and are committed once, on the last close. That
     * is what makes the semantics what they are: opening for writing truncates,
     * writes append to this buffer in order, and lseek does not reposition a
     * write.
     */
    uint8_t *write_buf;              /**< NULL until the first write; owned by this struct. */
    uint32_t write_len;              /**< Bytes buffered so far.                            */
    uint32_t write_cap;              /**< Allocated capacity of write_buf.                  */
    uint8_t  dirty;                  /**< Set when opened for writing; cleared on commit.   */
} vfs_file_t;

/**
 * @brief Largest file that can be produced through write().
 *
 * The buffer lives in the kernel heap until the last descriptor closes, so this
 * bounds how much one process can pin there. The whole disk is 2 MB, and
 * sys_create_file() caps its own path at 4 KB, so 64 KB is generous for what
 * the system can actually store while staying well clear of exhausting the heap.
 */
#define MAX_FILE_WRITE_BYTES 65536

/**
 * @brief Reads data from an opened file into a buffer.
 * 
 * @param file Pointer to the VFS file structure.
 * @param buffer Pointer to the destination buffer.
 * @param size Number of bytes to read.
 * @return The number of bytes successfully read, or a negative error code.
 */
int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size);

/**
 * @brief Opens a file by name within a specific parent directory.
 * 
 * @param name The name of the file to open.
 * @param parent_id The entry ID of the parent directory.
 * @param file Pointer to the VFS structure to populate upon success.
 * @return 0 on success, or a negative error code.
 */
int fs_open(const char *name, fs_id_t parent_id, vfs_file_t *file);

/**
 * @brief Deletes a file or directory from the filesystem.
 * 
 * @param name The name of the target file.
 * @param parent_id The entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_delete(const char *name, fs_id_t parent_id);

/**
 * @brief Renames an existing file or directory.
 * 
 * @param old_name The current name of the file.
 * @param new_name The new name to apply.
 * @param parent_id The entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_rename(const char *old_name, const char *new_name, fs_id_t parent_id);

/**
 * @brief Lists all files in the root filesystem.
 */
void fs_list_files(void);

/**
 * @brief Creates a new file with the specified content.
 * 
 * @param name Name of the new file.
 * @param content Data buffer to write into the file.
 * @param size Length of the data buffer in bytes.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_create_file(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id);

/**
 * @brief Creates an encrypted file using the provided symmetric key.
 * 
 * @param name Name of the encrypted file.
 * @param data Unencrypted data buffer.
 * @param len Length of the data.
 * @param key 32-byte encryption key.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], fs_id_t parent_id);

/**
 * @brief Creates a new file by writing raw, unprocessed sectors to the disk.
 * 
 * @param name Name of the file.
 * @param content Raw data buffer.
 * @param size Length of the raw data.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id);

/**
 * @brief Reads and decrypts an encrypted file.
 * 
 * @param file Pointer to the VFS file structure.
 * @param buffer Destination buffer for the decrypted data.
 * @param size Number of bytes to read.
 * @param key 32-byte decryption key.
 * @return The number of bytes read, or a negative error code.
 */
int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]);

/**
 * @brief Reports how many bytes an open file will actually yield to read().
 *
 * Not the same as the directory table's file_size: under SEC_LEVEL_CRYPTO_ENFORCED,
 * which is the default, the on-disk bytes are ciphertext with an IV, a header and
 * padding on top of the plaintext. stat() and lseek(SEEK_END) ask here so they
 * agree with read().
 *
 * @param file Open file to measure.
 * @param out_size Receives the readable byte count on success.
 * @return E_OK on success, or a negative error code.
 */
int fs_size(vfs_file_t *file, uint32_t *out_size);

/**
 * @brief Reads the plaintext length out of an encrypted file's header.
 *
 * Decrypts only the first ciphertext block, which is where fs_create_encrypted()
 * puts the magic and the original length. The result is not authenticated - see
 * the note on the definition.
 *
 * @param file Open file whose header is to be read.
 * @param key 32-byte decryption key.
 * @param out_size Receives the plaintext length on success.
 * @return E_OK on success, or a negative error code.
 */
int fs_size_encrypted(vfs_file_t *file, const uint8_t key[32], uint32_t *out_size);

/**
 * @brief Appends to an open file's write buffer.
 *
 * Does not touch the disk. The bytes are held until fs_commit_writes() runs, for
 * the reason described on vfs_file_t: the stored form cannot be appended to.
 *
 * @param file Open file, opened for writing.
 * @param data Bytes to append; already copied into kernel memory.
 * @param size Number of bytes.
 * @return The count written, or a negative error code (E_FBIG past the cap,
 *         E_NOMEM if the buffer cannot grow).
 */
int fs_write_buffered(vfs_file_t *file, const uint8_t *data, uint32_t size);

/**
 * @brief Commits an open file's buffered writes and releases the buffer.
 *
 * Called when the last descriptor referring to the file goes away - on close, or
 * when a process exits still holding it. A file opened for writing and never
 * written to commits zero bytes, which is what makes opening for writing a
 * truncation.
 *
 * @param file Open file; may have no buffer, in which case this does nothing.
 * @return E_OK, or the error fs_atomic_update() reported. A caller that
 *         discards this turns a full disk into silent data loss.
 */
int fs_commit_writes(vfs_file_t *file);

/**
 * @brief Reads raw, unprocessed data directly from the file's sectors.
 * 
 * @param file Pointer to the VFS file structure.
 * @param buffer Destination buffer.
 * @param size Number of bytes to read.
 * @return The number of bytes read, or a negative error code.
 */
int fs_read_raw(vfs_file_t *file, uint8_t *buffer, uint32_t size);

/**
 * @brief Creates a new directory.
 * 
 * @param name Name of the directory.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_mkdir(const char *name, fs_id_t parent_id);

/*
 * fs_list_dir() is gone as of v0.10.0. Its only caller was SYSCALL_LS_DIR, which
 * printed the listing from inside the kernel and so could never reach a pipe;
 * removing the call left the function with nobody to answer to. What replaced it
 * is READDIR, which hands the entries back and lets the caller do the printing.
 */

/**
 * @brief Atomically updates an existing file by writing the new content safely.
 * Ensures that partial updates are not visible upon failure.
 * 
 * @param name Name of the file to update.
 * @param content New data buffer.
 * @param size Length of the new data.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, fs_id_t parent_id);


/**
 * @brief Checks that a parent id names a directory that actually exists.
 *
 * @param parent_id Candidate parent entry id (0 is the root).
 * @return 1 when the id names an existing directory, 0 otherwise.
 */
int fs_dir_exists(fs_id_t parent_id);

/**
 * @brief Sets an entry's permission bits.
 *
 * Who may call this is decided by the caller - the syscall layer, alongside the
 * other access rules. What this owns is the mechanics: the lock, the ctime, and
 * getting the change onto the disk.
 *
 * Bits outside FS_MODE_PERM_MASK are dropped rather than stored. The mask is
 * 07777 and always was, so the sticky bit has been kept here since v0.9.0 - the
 * change in v0.9.4 is that something finally reads it. The set-user-id and
 * set-group-id bits are still stored and still consulted by nothing.
 *
 * @param entry_id Entry to change.
 * @param mode New permission bits.
 * @return E_OK, or a negative errno.
 */
int fs_chmod(fs_id_t entry_id, uint16_t mode);

/**
 * @brief Sets an entry's owner and group.
 *
 * @param entry_id Entry to change.
 * @param uid New owner.
 * @param gid New group.
 * @return E_OK, or a negative errno.
 */
int fs_chown(fs_id_t entry_id, uint32_t uid, uint32_t gid);

/**
 * @brief Whether a mode grants what a caller is asking for.
 *
 * The Unix rule exactly: the owning user gets the owner bits, the owning group
 * gets the group bits, everyone else gets the other bits, and **the first class
 * that matches is the one that decides.** An owner with no permission is refused
 * rather than falling through to the group bits, which is the part people
 * misremember and the part a `chmod 077` depends on.
 *
 * Pure, and in the header for the reason editbuf.h and esdtime.h are: permission
 * logic that is wrong is wrong silently, so it has to be reachable from a test
 * without a disk or a screen behind it.
 *
 * @param mode Entry's permission bits.
 * @param owner_uid Entry's owner.
 * @param owner_gid Entry's group.
 * @param uid Caller's user id.
 * @param gid Caller's group id.
 * @param want Bits wanted, in the other-class positions: 4 read, 2 write, 1 execute.
 * @return 1 when every wanted bit is granted.
 */
static inline int fs_mode_allows(uint16_t mode, uint32_t owner_uid, uint32_t owner_gid,
                                 uint32_t uid, uint32_t gid, int want) {
    int bits;

    /* Root is not subject to the bits, as everywhere else. */
    if (uid == 0) return 1;

    if (uid == owner_uid)      bits = (mode >> 6) & 7;
    else if (gid == owner_gid) bits = (mode >> 3) & 7;
    else                       bits = mode & 7;

    return (bits & want) == want;
}

/** @brief Permission bits, in the positions fs_mode_allows() wants them. */
#define FS_WANT_READ  4
#define FS_WANT_WRITE 2
#define FS_WANT_EXEC  1

/**
 * @brief Whether a sticky directory lets this caller remove this entry.
 *
 * The sticky bit exists for one arrangement: a directory everybody may write in,
 * where what you wrote is still yours. `/tmp` is `0777` without it, which means
 * anyone may create a file there *and* anyone may delete somebody else's. Write
 * permission on a directory is the permission to remove things from it, so
 * without a second rule the two cannot be told apart.
 *
 * The rule is the Unix one, and the third clause is the part people forget: the
 * directory's own owner may remove anything in it. That is what lets root - or
 * whoever administers a shared directory - clean it out without owning every
 * file inside.
 *
 * This is asked *in addition to* write permission on the directory, never
 * instead of it. A caller refused by the mode is refused before this is
 * consulted.
 *
 * Pure and in the header for fs_mode_allows()'s reason: permission logic that is
 * wrong is wrong silently, so it has to be reachable from a test with no disk
 * and no screen behind it.
 *
 * @param dir_mode Mode of the directory the entry lives in.
 * @param dir_owner_uid Owner of that directory.
 * @param entry_owner_uid Owner of the entry being removed or renamed.
 * @param uid Caller's user id.
 * @return 1 when the removal may proceed.
 */
static inline int fs_sticky_allows_removal(uint16_t dir_mode, uint32_t dir_owner_uid,
                                           uint32_t entry_owner_uid, uint32_t uid) {
    /* Not sticky: the directory's write bit already said everything. */
    if (!(dir_mode & FS_MODE_STICKY)) return 1;

    if (uid == 0) return 1;
    if (uid == entry_owner_uid) return 1;
    if (uid == dir_owner_uid) return 1;

    return 0;
}


// --- Added by Refactor Script ---
extern int fs_get_entry_idx(const char *name, fs_id_t parent_id);
/*
 * Both tables are allocated at mount and sized from the disk as of v1.3.0. They
 * were static arrays of FS_MAX_ENTRIES_CAP and FS_MAX_CLUSTERS_CAP, which is why
 * the disk was capped at 16 MB and the file system at 512 entries whatever it
 * was mounted on.
 *
 * Pointers rather than arrays, and every dir_table[i] in the tree compiles
 * unchanged - eighty-nine of them in vfs.c alone. What had to change is the
 * bound of every loop over them, which is fs_max_entries and fs_total_clusters.
 */
extern disk_file_entry_t *dir_table;

/**
 * @brief Entries the mounted file system actually holds.
 *
 * Read from the superblock at mount, which is where the format recorded what it
 * chose. Never FS_MAX_ENTRIES_CAP: that is the ceiling a format may not exceed,
 * and a disk formatted by an older release holds 512.
 *
 * Signed, because an entry index is an int everywhere in this tree -
 * fs_get_entry_idx() returns one and uses -1 for "no such entry", and every
 * inode_idx is an int. A uint32_t here would have put a signed/unsigned
 * comparison in twenty-seven loops and ten bounds checks, and the warning would
 * have been right: the mismatch is real, and the answer is to type the count the
 * way the indices are typed rather than to cast at every use. The value is at
 * most FS_MAX_ENTRIES_CAP.
 */
extern int fs_max_entries;

/* The allocation table, one uint32_t per cluster. Declared here as of v0.10.0
 * because the tests need to assert that clusters 0 and 1 are reserved - which is
 * what makes a start_cluster of 0 mean "no data" rather than "the first
 * cluster", and is therefore worth checking rather than assuming. */
extern uint32_t *file_allocation_table;
extern void init_fs(void);

#endif // FS_H