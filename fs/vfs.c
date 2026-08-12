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
disk_file_entry_t dir_table[MAX_FILES_IN_DIR];
uint32_t fs_max_sectors = 4096;
uint32_t file_allocation_table[4096];

int fs_get_entry_idx(const char *name, uint8_t parent_id);

/**
 * @brief Checks that a parent id names a directory that actually exists.
 *
 * Entry ids arrive from user space as a raw byte, and fs_mkdir()/
 * fs_create_file_raw() store whatever they are given. Accepting an arbitrary id
 * let a process create an entry whose parent is itself, and the parent walk in
 * check_vfs_access() then never terminated.
 *
 * @param parent_id Candidate parent entry id. 0 is the root, which has no
 *                  directory table entry of its own.
 * @return 1 when the id names an existing directory, 0 otherwise.
 */
int fs_dir_exists(uint8_t parent_id) {
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
        bcache_write_sector(FS_FAT_START_SECTOR + i, fat_ptr + (i * 512));
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

    for (uint32_t i = FS_DATA_START_SECTOR; i < fs_max_sectors && allocated < count; i++) {
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
        bcache_write_sector(FS_DIR_START_SECTOR + i, sec_buf);
    }
}

/* --- FILE SYSTEM INITIALIZATION --- */

/**
 * @brief init_fs
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

    uint8_t *dir_ptr = (uint8_t *)dir_table;
    uint32_t total_bytes = sizeof(disk_file_entry_t) * MAX_FILES_IN_DIR;
    uint32_t sectors_needed = (total_bytes + 511) / 512;

    for (uint32_t i = 0; i < sectors_needed; i++) {
        uint8_t sec_buf[512];
        bcache_read_sector(FS_DIR_START_SECTOR + i, sec_buf);
        
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
        bcache_read_sector(FS_FAT_START_SECTOR + i, fat_ptr + (i * 512));
    }

    for (uint32_t i = 0; i < FS_DATA_START_SECTOR; i++) {
        if (file_allocation_table[i] == FAT_FREE) {
            file_allocation_table[i] = FAT_EOF;
        }
    }
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
 * @brief fs_create_file_raw
 * @param name
 * @param content
 * @param size
 * @param parent_id
 * @return int
 */
int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
    vfs_lock();

    if (!fs_dir_exists(parent_id)) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Rejected file creation under a non-existent parent", parent_id);
        vfs_unlock(); return E_NOENT;
    }

uint32_t c_uid = (current_task != 0) ? current_task->uid : 0;
    
    if (ft_strcmp(name, "passwd") == 0 && c_uid != 0) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Access Denied: Only ROOT can modify 'passwd'. UID", c_uid);
        vfs_unlock(); return E_ACCES;
    }

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
    dir_table[free_idx].file_type = 0;
    dir_table[free_idx].entry_id = free_idx; 
    dir_table[free_idx].parent_id = parent_id; 
    dir_table[free_idx].owner_uid = c_uid;

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
int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
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
int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file) {
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
int fs_delete(const char *name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File deletion blocked!");
        vfs_unlock(); return E_ROFS;
    }
uint32_t c_uid = (current_task != 0) ? current_task->uid : 0;
    
    if (ft_strcmp(name, "passwd") == 0 && c_uid != 0) {
        klog(LOG_LEVEL_WARN, "VFS", "Access Denied: Only ROOT can delete 'passwd'!");
        vfs_unlock(); return E_ACCES;
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
            uint32_t sec_to_free = dir_table[i].start_sector;
            while (sec_to_free != FAT_EOF && sec_to_free != FAT_FREE) {
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
int fs_rename(const char *old_name, const char *new_name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "System is in Immutable mode. File renaming blocked!");
        vfs_unlock(); return E_ROFS;
    }
uint32_t c_uid = (current_task != 0) ? current_task->uid : 0;
    
    if ((ft_strcmp(old_name, "passwd") == 0 || ft_strcmp(new_name, "passwd") == 0) && c_uid != 0) {
        klog(LOG_LEVEL_WARN, "VFS", "Access Denied: Only ROOT can rename 'passwd'!");
        vfs_unlock(); return E_ACCES;
    }

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
int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
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
 * @brief fs_get_entry_idx
 * @param name
 * @param parent_id
 * @return int
 */
int fs_get_entry_idx(const char *name, uint8_t parent_id) {
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
int fs_mkdir(const char *name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) { vfs_unlock(); return E_ROFS; }

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
    
    dir_table[free_idx].file_size = 0; 
    dir_table[free_idx].start_sector = 0;
    dir_table[free_idx].file_type = 1;
    dir_table[free_idx].entry_id = free_idx;
    dir_table[free_idx].parent_id = parent_id;
    dir_table[free_idx].is_used = 1;
uint32_t current_uid = (current_task != 0) ? current_task->uid : 0;
    dir_table[free_idx].owner_uid = current_uid;

    save_directory_to_disk();
    vfs_unlock(); return E_OK;
}

/**
 * @brief fs_list_dir
 * @param parent_id
 */
void fs_list_dir(uint8_t parent_id) {
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