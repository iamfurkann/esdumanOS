/*
 * File: vfs.c
 * Purpose: Virtual File System implementation handling file operations and FAT.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "fs.h"
#include "ata.h"
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
disk_file_entry_t dir_table[MAX_FILES_IN_DIR];
uint32_t fs_max_sectors = 4096;
uint32_t file_allocation_table[4096];

disk_superblock_t fs_super;
int fs_mounted = 0;

int fs_get_entry_idx(const char *name, fs_id_t parent_id);

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
    if (parent_id == 0) return 1;

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used &&
            dir_table[i].file_type == FT_DIR &&
            dir_table[i].entry_id == parent_id) {
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
static void safe_strcpy(char *dest, const char *src, size_t max_len) {
    if (max_len == 0) return;
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) { dest[i] = src[i]; }
    dest[i] = '\0';
}

static mutex_t vfs_mutex;
rwlock_t inode_locks[MAX_FILES_IN_DIR];
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
 * @brief save_fat_to_disk
 * @return static void
 */
static void save_fat_to_disk(void) {
    uint8_t *fat_ptr = (uint8_t *)file_allocation_table;
    uint32_t total_bytes = fs_max_sectors * sizeof(uint32_t);
    uint32_t sectors_needed = (total_bytes + 511) / 512;

    for (uint32_t i = 0; i < sectors_needed; i++) {
        bcache_write_sector(fs_super.fat_start + i, fat_ptr + (i * 512));
    }
}

/**
 * @brief allocate_fat_chain
 * @param count
 * @return static uint32_t
 */
static uint32_t allocate_fat_chain(uint32_t count) {
    if (count == 0) return FAT_EOF;

    uint32_t start_sector = FAT_EOF;
    uint32_t current_sector = FAT_EOF;
    uint32_t allocated = 0;

    for (uint32_t i = fs_super.data_start; i < fs_max_sectors && allocated < count; i++) {
        if (file_allocation_table[i] == FAT_FREE) {
            if (start_sector == FAT_EOF) {
                start_sector = i;
            } else {
                file_allocation_table[current_sector] = i;
            }
            
            current_sector = i;
            file_allocation_table[current_sector] = FAT_EOF; 
            allocated++;
        }
    }

    if (allocated < count) {
        uint32_t rollback_sector = start_sector;
        while (rollback_sector != FAT_EOF) {
            uint32_t next = file_allocation_table[rollback_sector];
            file_allocation_table[rollback_sector] = FAT_FREE;
            rollback_sector = next;
        }
        return FAT_FREE;
    }

    save_fat_to_disk();
    return start_sector;
}

/**
 * @brief save_directory_to_disk
 * @return static void
 */
static void save_directory_to_disk(void) {
    uint8_t *dir_ptr = (uint8_t *)dir_table;
    uint32_t total_bytes = sizeof(disk_file_entry_t) * MAX_FILES_IN_DIR;
    uint32_t sectors_needed = (total_bytes + 511) / 512;

    for (uint32_t i = 0; i < sectors_needed; i++) {
        uint8_t sec_buf[512];
        ft_memset(sec_buf, 0, 512);

        uint32_t copy_size = 512;
        if ((i * 512) + 512 > total_bytes) {
            copy_size = total_bytes % 512;
        }

        for (uint32_t j = 0; j < copy_size; j++) {
            sec_buf[j] = dir_ptr[(i * 512) + j];
        }
        bcache_write_sector(fs_super.dir_start + i, sec_buf);
    }
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
static void save_superblock(void) {
    uint8_t sec_buf[512];
    uint8_t *src = (uint8_t *)&fs_super;

    ft_memset(sec_buf, 0, 512);
    for (uint32_t i = 0; i < sizeof(disk_superblock_t); i++) sec_buf[i] = src[i];
    bcache_write_sector(FS_SUPER_SECTOR, sec_buf);
}

/**
 * @brief Whether every byte of a run of sectors is zero.
 *
 * @param start First sector.
 * @param count How many.
 * @return 1 when the whole run is zero.
 */
static int region_is_blank(uint32_t start, uint32_t count) {
    uint8_t sec_buf[512];

    for (uint32_t s = 0; s < count; s++) {
        bcache_read_sector(start + s, sec_buf);
        for (int i = 0; i < 512; i++) {
            if (sec_buf[i] != 0) return 0;
        }
    }
    return 1;
}

/**
 * @brief Lays a fresh file system onto a blank disk.
 *
 * The geometry comes from the FS_ constants here and is then written down in the
 * superblock, which is what everything afterwards reads. Changing the constants
 * later therefore changes what new disks are formatted as, and cannot change how
 * an existing one is read.
 */
static void format_disk(void) {
    uint8_t zero[512];

    klog(LOG_LEVEL_INFO, "VFS", "Blank disk; laying out a new file system.");

    ft_memset(&fs_super, 0, sizeof(fs_super));
    fs_super.magic = FS_SUPER_MAGIC;
    fs_super.format_version = FS_FORMAT_VERSION;
    fs_super.sector_size = 512;
    fs_super.total_sectors = fs_max_sectors;
    fs_super.dir_start = FS_DIR_START_SECTOR;
    fs_super.dir_sectors = FS_DIR_SECTOR_COUNT;
    fs_super.max_entries = MAX_FILES_IN_DIR;
    fs_super.entry_size = (uint16_t)sizeof(disk_file_entry_t);
    fs_super.cluster_sectors = 1;
    fs_super.fat_start = FS_FAT_START_SECTOR;
    fs_super.data_start = FS_DATA_START_SECTOR;
    fs_super.flags = 0;

    ft_memset(dir_table, 0, sizeof(dir_table));
    ft_memset(file_allocation_table, 0, sizeof(file_allocation_table));

    /* The metadata sectors are spoken for before anything can be allocated. */
    for (uint32_t i = 0; i < fs_super.data_start && i < fs_max_sectors; i++) {
        file_allocation_table[i] = FAT_EOF;
    }

    ft_memset(zero, 0, 512);
    for (uint32_t i = 0; i < fs_super.dir_sectors; i++) {
        bcache_write_sector(fs_super.dir_start + i, zero);
    }

    save_superblock();
    save_fat_to_disk();
    save_directory_to_disk();
    bcache_flush();
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
static int load_superblock(void) {
    uint8_t sec_buf[512];
    uint8_t *dst = (uint8_t *)&fs_super;

    bcache_read_sector(FS_SUPER_SECTOR, sec_buf);
    for (uint32_t i = 0; i < sizeof(disk_superblock_t); i++) dst[i] = sec_buf[i];

    if (fs_super.magic != FS_SUPER_MAGIC) return 0;

    if (fs_super.format_version != FS_FORMAT_VERSION) {
        klog_int(LOG_LEVEL_ERROR, "VFS",
                 "Disk was written by a different format version", fs_super.format_version);
        return 0;
    }
    if (fs_super.sector_size != 512 ||
        fs_super.entry_size != (uint16_t)sizeof(disk_file_entry_t) ||
        fs_super.max_entries > MAX_FILES_IN_DIR ||
        fs_super.cluster_sectors != 1 ||
        fs_super.dir_start == 0 ||
        fs_super.fat_start <= fs_super.dir_start ||
        fs_super.data_start <= fs_super.fat_start) {
        klog(LOG_LEVEL_ERROR, "VFS", "Superblock describes a geometry this kernel cannot read.");
        return 0;
    }
    return 1;
}

/**
 * @brief Mounts the file system, formatting a blank disk and refusing anything else.
 *
 * Three outcomes, and the third is the one this exists for.
 *
 * A disk carrying our superblock is mounted with the geometry the superblock
 * records. A disk that is entirely blank is formatted. Anything else - an image
 * written by a kernel from before v0.9.0, a disk belonging to something else, or
 * 2 MB of noise - is refused, and nothing is written to it.
 *
 * Refusing matters more than mounting. Before v0.9.0 there was no superblock and
 * no check of any kind: the directory table and the FAT were read out of their
 * sectors and used as they were. Handed an older image, this kernel would have
 * read 272-byte entries as 96-byte ones, found a directory tree that was not
 * there, and written its own tables over the user's files. An old disk is
 * recognisable precisely because the old format never used sector 0, so a zero
 * superblock over a non-zero directory region can only be one thing.
 */
void init_fs(void) {
    mutex_init(&vfs_mutex);
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        rwlock_init(&inode_locks[i]);
    }
    bcache_init();
    fs_max_sectors = ata_identify();

    if (fs_max_sectors > 4096) {
        fs_max_sectors = 4096;
    }

    fs_mounted = 0;
    ft_memset(dir_table, 0, sizeof(dir_table));

    if (!load_superblock()) {
        if (!region_is_blank(FS_SUPER_SECTOR, 1) ||
            !region_is_blank(FS_DIR_START_SECTOR, FS_LEGACY_DIR_SECTORS)) {
            klog(LOG_LEVEL_ERROR, "VFS",
                 "Unrecognised disk; refusing to mount it or write to it.");
            printk("\n[VFS] This disk was not written by this version of esdumanOS.\n");
            printk("      v0.9.0 changed the on-disk format, and an image from an\n");
            printk("      earlier release cannot be read. Nothing has been written to\n");
            printk("      it. Use the disk.img from the release, or start from a blank\n");
            printk("      one - `make run` does that for you.\n\n");
            return;
        }
        format_disk();
    }

    uint8_t *dir_ptr = (uint8_t *)dir_table;
    uint32_t total_bytes = (uint32_t)fs_super.entry_size * fs_super.max_entries;
    uint32_t sectors_needed = (total_bytes + 511) / 512;

    for (uint32_t i = 0; i < sectors_needed; i++) {
        uint8_t sec_buf[512];
        bcache_read_sector(fs_super.dir_start + i, sec_buf);

        uint32_t copy_size = 512;
        if ((i * 512) + 512 > total_bytes) {
            copy_size = total_bytes % 512;
        }
        for (uint32_t j = 0; j < copy_size; j++) {
            dir_ptr[(i * 512) + j] = sec_buf[j];
        }
    }

    uint8_t *fat_ptr = (uint8_t *)file_allocation_table;
    uint32_t fat_total_bytes = fs_max_sectors * sizeof(uint32_t);
    uint32_t fat_sectors_needed = (fat_total_bytes + 511) / 512;

    for (uint32_t i = 0; i < fat_sectors_needed; i++) {
        bcache_read_sector(fs_super.fat_start + i, fat_ptr + (i * 512));
    }

    for (uint32_t i = 0; i < fs_super.data_start && i < fs_max_sectors; i++) {
        if (file_allocation_table[i] == FAT_FREE) {
            file_allocation_table[i] = FAT_EOF;
        }
    }

    fs_mounted = 1;
    save_fat_to_disk();
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
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1) {
            printk("    %s \t\t Size: %d Byte \t Sector: %d\n",
            dir_table[i].filename, dir_table[i].file_size, dir_table[i].start_sector);
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
    if (file->inode_idx >= 0 && file->inode_idx < MAX_FILES_IN_DIR) {
        /*
         * No interrupt frame is passed. This runs several frames below the
         * syscall entry and does not have the live one; handing over
         * &current_task->regs instead - a saved copy that merely looks like a
         * frame - let the lock's sleep path write through the task's stored
         * context and desynchronise current_task from CR3. With 0 the lock
         * degrades to spinning, which never happens because the kernel is not
         * preemptible.
         */
        rwlock_acquire_read(&inode_locks[file->inode_idx], 0);
    }
    if (file->current_offset >= file->file_size) { 
        if (file->inode_idx >= 0 && file->inode_idx < MAX_FILES_IN_DIR) rwlock_release_read(&inode_locks[file->inode_idx]); 
        return 0; 
    }

    if (file->current_offset + size > file->file_size)
        size = file->file_size - file->current_offset;

    uint32_t bytes_read = 0;
    uint32_t current_sector = file->start_sector;
    uint32_t sectors_to_skip = file->current_offset / 512;
    
    for (uint32_t i = 0; i < sectors_to_skip; i++) {
        if (current_sector == FAT_EOF || current_sector >= 4096) {
            if (file->inode_idx >= 0 && file->inode_idx < MAX_FILES_IN_DIR)
                rwlock_release_read(&inode_locks[file->inode_idx]);
            return 0;
        }
        current_sector = file_allocation_table[current_sector];
    }

    uint32_t offset_in_sector = file->current_offset % 512;

    while (bytes_read < size && current_sector != FAT_EOF) {
        if (current_sector >= 4096) { break; }
        uint8_t sector_buf[512];
        bcache_read_sector(current_sector, sector_buf);

        uint32_t bytes_to_copy = 512 - offset_in_sector;
        if (bytes_to_copy > (size - bytes_read)) {
            bytes_to_copy = size - bytes_read;
        }

        for (uint32_t i = 0; i < bytes_to_copy; i++) {
            buffer[bytes_read + i] = sector_buf[offset_in_sector + i];
        }

        file->current_offset += bytes_to_copy;
        bytes_read += bytes_to_copy;
        current_sector = file_allocation_table[current_sector];
        offset_in_sector = 0;
    }
    if (file->inode_idx >= 0 && file->inode_idx < MAX_FILES_IN_DIR) {
        rwlock_release_read(&inode_locks[file->inode_idx]);
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
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        /* LOCKDOWN destroys the key but outranks CRYPTO_ENFORCED, so this
         * branch is still taken afterwards. Refuse rather than decrypt with
         * zeroes and hand the caller plausible-looking garbage. */
        if (!crypto_fs_key_is_usable()) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted read refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_read_encrypted(file, buffer, size, kernel_master_key);
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
    if (!file || !file->dirty) return E_OK;

    int res = E_OK;

    if (file->inode_idx < 0 || file->inode_idx >= MAX_FILES_IN_DIR) {
        res = E_BADF;
    } else {
        fs_id_t parent_id = dir_table[file->inode_idx].parent_id;
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
    if (!file || !out_size) return E_INVAL;

    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        if (!crypto_fs_key_is_usable()) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted size query refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_size_encrypted(file, kernel_master_key, out_size);
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
    if (!fs_mounted) return E_NODEV;

    vfs_lock();

    /*
     * A name too long to store is refused rather than shortened. safe_strcpy()
     * truncates, which was academic while a name could be 255 bytes and is not
     * now that it is 63: a file created under a name the user did not choose is
     * a file they cannot find again, and two long names that share a prefix
     * become the same file.
     */
    if (ft_strlen(name) >= MAX_FILENAME) {
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
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id && ft_strcmp(dir_table[i].filename, name) == 0) {
            existing_idx = i;
        }
        if (i > 0 && dir_table[i].is_used == 0 && free_idx == -1) {
            free_idx = i;
        }
    }

    if (existing_idx != -1) { vfs_unlock(); return E_EXIST; }
    if (free_idx == -1) {
        klog(LOG_LEVEL_ERROR, "VFS", "File system (Directory table) full!");
        vfs_unlock(); return E_NFILE;
    }

    uint32_t sectors_needed = (size == 0) ? 1 : ((size + 511) / 512);
    uint32_t new_sector = allocate_fat_chain(sectors_needed);
    
    if (new_sector == FAT_FREE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Disk is completely full or not enough space!");
        vfs_unlock(); return E_NOSPC;
    }

    uint32_t bytes_written = 0;
    uint32_t current_sec_to_write = new_sector;

    for (uint32_t s = 0; s < sectors_needed; s++) {
        if (current_sec_to_write == FAT_EOF) break;

        uint8_t sector_buf[512];
        ft_memset(sector_buf, 0, 512);

        uint32_t chunk_size = 512;
        if (bytes_written + 512 > size) {
            chunk_size = size - bytes_written;
        }

        for(uint32_t i = 0; i < chunk_size; i++) {
            sector_buf[i] = content[bytes_written + i];
        }
        bcache_write_sector(current_sec_to_write, sector_buf);
        bytes_written += chunk_size;
        
        if (current_sec_to_write >= 4096) { break; }
        current_sec_to_write = file_allocation_table[current_sec_to_write];
    }

    ft_memset(dir_table[free_idx].filename, 0, MAX_FILENAME);
    safe_strcpy(dir_table[free_idx].filename, name, MAX_FILENAME);
    
    dir_table[free_idx].start_sector = new_sector;
    dir_table[free_idx].file_size = size;
    dir_table[free_idx].is_used = 1;
    dir_table[free_idx].file_type = FT_REGULAR;
    dir_table[free_idx].entry_id = (fs_id_t)free_idx;
    dir_table[free_idx].parent_id = parent_id;
    dir_table[free_idx].owner_uid = c_uid;
    dir_table[free_idx].owner_gid = c_gid;
    dir_table[free_idx].mode = FS_MODE_DEFAULT_FILE;
    dir_table[free_idx].ctime = now;
    dir_table[free_idx].mtime = now;

    save_directory_to_disk();
    klog(LOG_LEVEL_INFO, "VFS", "New file created and written to disk.");
    vfs_unlock();
    return E_OK;
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
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File creation blocked!");
        return E_ROFS;
    }
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        /* Writing with a zeroed key would produce a file nobody can ever read
         * back - a silent, permanent loss. Refuse instead. */
        if (!crypto_fs_key_is_usable()) {
            klog(LOG_LEVEL_ERROR, "VFS", "Encrypted write refused: master key destroyed.");
            return E_ACCES;
        }
        return fs_create_encrypted(name, content, size, kernel_master_key, parent_id);
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
    if (!fs_mounted) return E_NODEV;

    vfs_lock();
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
            safe_strcpy(file->filename, dir_table[i].filename, MAX_FILENAME);
            file->start_sector = dir_table[i].start_sector;
            file->file_size = dir_table[i].file_size;
            file->current_offset = 0;
            file->ref_count = 1;
            file->inode_idx = i;

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
    if (!fs_mounted) return E_NODEV;

    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File deletion blocked!");
        vfs_unlock(); return E_ROFS;
    }
    /* The "passwd" name comparison is gone; see fs_create_file_raw() for why.
     * Deleting from /etc needs write permission on /etc, which is 0755 and
     * root's, and the syscall layer asks that before this is reached. */

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 &&
            dir_table[i].parent_id == parent_id &&
            ft_strcmp(dir_table[i].filename, name) == 0) {
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
            if (dir_table[i].file_type == FT_DIR) {
                for (int c = 0; c < MAX_FILES_IN_DIR; c++) {
                    if (dir_table[c].is_used == 1 && dir_table[c].parent_id == dir_table[i].entry_id) {
                        klog(LOG_LEVEL_WARN, "VFS", "Refusing to delete a directory that is not empty.");
                        vfs_unlock(); return E_NOTEMPTY;
                    }
                }
            }

            /*
             * The index is bounded before every use.
             *
             * file_allocation_table is uint32_t[4096] and dir_table is loaded
             * verbatim from the disk at init_fs() with no validation, so
             * start_sector is attacker-controlled by anyone who can write the
             * image. Unbounded, this loop turned "rm <file>" into a four-byte
             * kernel write at an offset the image chose, and then followed
             * whatever it read there.
             *
             * Every other FAT walk in this file already guards the same value -
             * fs_read_raw() checks it twice and fs_create_file_raw() once. This
             * one was the exception.
             */
            uint32_t sec_to_free = dir_table[i].start_sector;
            while (sec_to_free != FAT_EOF && sec_to_free != FAT_FREE) {
                if (sec_to_free >= 4096) {
                    klog_int(LOG_LEVEL_ERROR, "VFS",
                             "Refusing to follow an out-of-range FAT entry while deleting", (int)sec_to_free);
                    break;
                }
                uint32_t next_sec = file_allocation_table[sec_to_free];
                file_allocation_table[sec_to_free] = FAT_FREE;
                sec_to_free = next_sec;
            }
            save_fat_to_disk();
            dir_table[i].is_used = 0; 
            ft_memset(dir_table[i].filename, 0, MAX_FILENAME);
            save_directory_to_disk();
            klog(LOG_LEVEL_INFO, "VFS", "File successfully deleted from disk.");
            vfs_unlock(); return E_OK;
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
    if (!fs_mounted) return E_NODEV;
    if (ft_strlen(new_name) >= MAX_FILENAME) return E_INVAL;

    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File renaming blocked!");
        vfs_unlock(); return E_ROFS;
    }
    /* The "passwd" name comparison is gone; see fs_create_file_raw() for why.
     * This one also refused renaming anything *to* "passwd", which made the name
     * itself reserved system-wide - a user could not call their own file that in
     * their own home directory. */

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && dir_table[i].parent_id == parent_id && ft_strcmp(dir_table[i].filename, new_name) == 0) {
            klog(LOG_LEVEL_WARN, "VFS", "Rename failed: File with target name already exists!");
            vfs_unlock(); return E_EXIST;
        }
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, old_name) == 0) {
            
            ft_memset(dir_table[i].filename, 0, MAX_FILENAME);
            safe_strcpy(dir_table[i].filename, new_name, MAX_FILENAME);

            /* The entry changed, the contents did not, so this is a ctime and
             * not an mtime - the distinction the two fields exist to make. */
            dir_table[i].ctime = fs_now();

            save_directory_to_disk();
            vfs_unlock(); return E_OK;
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
    if (!fs_mounted) return E_NODEV;

    if (current_sec_level >= SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_WARN, "VFS", "fs_atomic_update: Blocked by IMMUTABLE security level");
        return E_ACCES;
    }
    // CRYPTO_ENFORCED: atomic updates also go through encrypted path
    // This is handled by using fs_create_file() instead of fs_create_file_raw()
    // (see Fix 2 above - already addressed by calling fs_create_file)
    vfs_lock();
    
    int orig_idx = -1;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id && ft_strcmp(dir_table[i].filename, name) == 0) {
            orig_idx = i; break;
        }
    }

    if (orig_idx == -1) {
        vfs_unlock();
        return fs_create_file(name, content, size, parent_id);
    }

    char tmp_name[MAX_FILENAME];
    ft_memset(tmp_name, 0, MAX_FILENAME);
    safe_strcpy(tmp_name, name, MAX_FILENAME - 4);
    
    uint32_t len = ft_strlen(tmp_name);
    tmp_name[len] = '.'; tmp_name[len+1] = 't'; tmp_name[len+2] = 'm'; tmp_name[len+3] = 'p'; tmp_name[len+4] = '\0';

    vfs_unlock();
    fs_delete(tmp_name, parent_id);
    int res = fs_create_file(tmp_name, content, size, parent_id);
    if (res != E_OK) return res;

    vfs_lock(); // Lock again for the swap operation
    int tmp_idx = -1;
    orig_idx = -1;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id) {
            if (ft_strcmp(dir_table[i].filename, name) == 0) orig_idx = i;
            if (ft_strcmp(dir_table[i].filename, tmp_name) == 0) tmp_idx = i;
        }
    }

    if (orig_idx == -1 || tmp_idx == -1) {
        vfs_unlock();
        // Clean up leaked temporary file
        fs_delete(tmp_name, parent_id);
        return E_NOENT;
    }

    uint32_t old_sector = dir_table[orig_idx].start_sector;
    uint32_t old_size = dir_table[orig_idx].file_size;

    if (orig_idx >= 0 && orig_idx < MAX_FILES_IN_DIR && tmp_idx >= 0 && tmp_idx < MAX_FILES_IN_DIR) {
        /* No live interrupt frame here either; see fs_read_raw(). */
        rwlock_acquire_write(&inode_locks[orig_idx], 0);
        
        dir_table[orig_idx].start_sector = dir_table[tmp_idx].start_sector;
        dir_table[orig_idx].file_size = dir_table[tmp_idx].file_size;

        /*
         * The contents changed, so the modification time does. Everything else
         * about the entry stays: this is the same file with new bytes in it, and
         * taking the owner or the creation time from the temporary would make a
         * write look like a new file to anything that asked.
         */
        dir_table[orig_idx].mtime = fs_now();

        dir_table[tmp_idx].start_sector = old_sector;
        dir_table[tmp_idx].file_size = old_size;

        rwlock_release_write(&inode_locks[orig_idx]);
    }
    
    save_directory_to_disk();
    vfs_unlock();
    
    fs_delete(tmp_name, parent_id);
    return E_OK;
}

/**
 * @brief Finds the directory table slot an entry id occupies.
 *
 * @param entry_id Entry to find.
 * @return Slot index, or -1.
 */
static int fs_slot_of(fs_id_t entry_id) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].entry_id == entry_id) return i;
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
    if (!fs_mounted) return E_NODEV;
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) return E_ROFS;

    vfs_lock();

    int idx = fs_slot_of(entry_id);
    if (idx < 0) { vfs_unlock(); return E_NOENT; }

    dir_table[idx].mode = (uint16_t)(mode & FS_MODE_PERM_MASK);
    /* The entry changed and the contents did not, which is the distinction the
     * two timestamps exist to make. */
    dir_table[idx].ctime = fs_now();

    save_directory_to_disk();
    vfs_unlock();
    return E_OK;
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
    if (!fs_mounted) return E_NODEV;
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) return E_ROFS;

    vfs_lock();

    int idx = fs_slot_of(entry_id);
    if (idx < 0) { vfs_unlock(); return E_NOENT; }

    dir_table[idx].owner_uid = uid;
    dir_table[idx].owner_gid = gid;
    dir_table[idx].ctime = fs_now();

    save_directory_to_disk();
    vfs_unlock();
    return E_OK;
}

/**
 * @brief fs_get_entry_idx
 * @param name
 * @param parent_id
 * @return int
 */
int fs_get_entry_idx(const char *name, fs_id_t parent_id) {
    vfs_lock();
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
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
    if (!fs_mounted) return E_NODEV;

    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) { vfs_unlock(); return E_ROFS; }

    /* Refused rather than truncated, as in fs_create_file_raw(). */
    if (ft_strlen(name) >= MAX_FILENAME) { vfs_unlock(); return E_INVAL; }

    if (!fs_dir_exists(parent_id)) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Rejected mkdir under a non-existent parent", parent_id);
        vfs_unlock(); return E_NOENT;
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id && ft_strcmp(dir_table[i].filename, name) == 0) {
            vfs_unlock(); return E_EXIST;
        }
    }

    int free_idx = -1;
    for (int i = 1; i < MAX_FILES_IN_DIR; i++) { 
        if (dir_table[i].is_used == 0) {
            free_idx = i;
            break;
        }
    }
    if (free_idx == -1) {
        klog(LOG_LEVEL_ERROR, "VFS", "Directory table full, cannot create new directory.");
        vfs_unlock(); return E_NFILE;
    }

    ft_memset(dir_table[free_idx].filename, 0, MAX_FILENAME);
    safe_strcpy(dir_table[free_idx].filename, name, MAX_FILENAME);
    
    uint32_t current_uid = (current_task != 0) ? current_task->uid : 0;
    uint32_t current_gid = (current_task != 0) ? current_task->gid : 0;
    uint32_t now = fs_now();

    dir_table[free_idx].file_size = 0;
    dir_table[free_idx].start_sector = 0;
    dir_table[free_idx].file_type = FT_DIR;
    dir_table[free_idx].entry_id = (fs_id_t)free_idx;
    dir_table[free_idx].parent_id = parent_id;
    dir_table[free_idx].is_used = 1;
    dir_table[free_idx].owner_uid = current_uid;
    dir_table[free_idx].owner_gid = current_gid;
    dir_table[free_idx].mode = FS_MODE_DEFAULT_DIR;
    dir_table[free_idx].ctime = now;
    dir_table[free_idx].mtime = now;

    save_directory_to_disk();
    vfs_unlock(); return E_OK;
}

/**
 * @brief fs_list_dir
 * @param parent_id
 */
void fs_list_dir(fs_id_t parent_id) {
    vfs_lock();
    printk("Contents:\n----------------------------------------\n");
    int found = 0;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id) {
            if (dir_table[i].file_type == 1) {
                terminal_setcolor(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
                printk("[DIR]  ");
            } else {
                terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                printk("[FILE] ");
            }
            printk("%s\n", dir_table[i].filename);
            found = 1;
        }
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    if (!found) printk("(Empty)\n");
    printk("----------------------------------------\n");
    vfs_unlock();
}