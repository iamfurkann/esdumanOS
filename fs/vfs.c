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

extern uint8_t kernel_master_key[32];
extern uint32_t ata_identify(void);

disk_file_entry_t dir_table[MAX_FILES_IN_DIR];
uint32_t fs_max_sectors = 4096;
uint32_t file_allocation_table[4096];

int fs_get_entry_idx(const char *name, uint8_t parent_id);

static void safe_strcpy(char *dest, const char *src, size_t max_len) {
    if (max_len == 0) return;
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++) { dest[i] = src[i]; }
    dest[i] = '\0';
}

static mutex_t vfs_mutex;
static int vfs_lock_owner = -1;
static int vfs_lock_count = 0;

static void vfs_lock(void) {
    if (!multitasking_enabled || current_task == -1) return;
    int my_pid = tasks[current_task].pid;
    if (vfs_lock_owner == my_pid) {
        vfs_lock_count++;
        return;
    }
    mutex_lock(&vfs_mutex, 0);
    vfs_lock_owner = my_pid;
    vfs_lock_count = 1;
}

static void vfs_unlock(void) {
    if (!multitasking_enabled || current_task == -1) return;
    int my_pid = tasks[current_task].pid;
    if (vfs_lock_owner == my_pid) {
        vfs_lock_count--;
        if (vfs_lock_count == 0) {
            vfs_lock_owner = -1;
            mutex_unlock(&vfs_mutex);
        }
    }
}

/* --- FAT (FILE ALLOCATION TABLE) YÖNETİMİ --- */

static void save_fat_to_disk(void) {
    uint8_t *fat_ptr = (uint8_t *)file_allocation_table;
    uint32_t total_bytes = fs_max_sectors * sizeof(uint32_t);
    uint32_t sectors_needed = (total_bytes + 511) / 512;
    
    for (uint32_t i = 0; i < sectors_needed; i++) {
        bcache_write_sector(FS_FAT_START_SECTOR + i, fat_ptr + (i * 512));
    }
}

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

/* --- DOSYA SİSTEMİ BAŞLATMA --- */

void init_fs(void) {
    mutex_init(&vfs_mutex);
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

/* --- VFS API (OKUMA / YAZMA / SİLME) --- */

void fs_list_files(void) {
    vfs_lock();
    printk("Dosya Listesi:\n");
    printk("----------------------------------------\n");
    int found = 0;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1) {
            printk("    %s \t\t Boyut: %d Byte \t Sektor: %d\n",
            dir_table[i].filename, dir_table[i].file_size, dir_table[i].start_sector);
            found = 1;
        }
    }
    if (!found) printk(" (Dosya sistemi bos veya dizin bulunumadi)\n");
    printk("----------------------------------------\n");
    vfs_unlock();
}

int fs_read_raw(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    vfs_lock();
    if (file->current_offset >= file->file_size) { vfs_unlock(); return 0; }

    if (file->current_offset + size > file->file_size)
        size = file->file_size - file->current_offset;

    uint32_t bytes_read = 0;
    uint32_t current_sector = file->start_sector;
    uint32_t sectors_to_skip = file->current_offset / 512;
    
    for (uint32_t i = 0; i < sectors_to_skip; i++) {
        if (current_sector == FAT_EOF) { vfs_unlock(); return 0; }
        current_sector = file_allocation_table[current_sector];
    }

    uint32_t offset_in_sector = file->current_offset % 512;

    while (bytes_read < size && current_sector != FAT_EOF) {
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
    vfs_unlock();
    return bytes_read;
}

int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        return fs_read_encrypted(file, buffer, size, kernel_master_key);
    }
    return fs_read_raw(file, buffer, size);
}

int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
    vfs_lock();
    extern process_t tasks[];
    uint32_t c_uid = (current_task >= 0) ? tasks[current_task].uid : 0;
    
    if (ft_strcmp(name, "passwd") == 0 && c_uid != 0) {
        klog_int(LOG_LEVEL_WARN, "VFS", "Erisim Engellendi: Sadece ROOT 'passwd' dosyasini degistirebilir. UID", c_uid);
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
        klog(LOG_LEVEL_ERROR, "VFS", "Dosya sistemi (Dizin tablosu) dolu!");
        vfs_unlock(); return E_NFILE;
    }

    uint32_t sectors_needed = (size == 0) ? 1 : ((size + 511) / 512);
    uint32_t new_sector = allocate_fat_chain(sectors_needed);
    
    if (new_sector == FAT_FREE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Disk tamamen dolu veya yeterli alan yok!");
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
    klog(LOG_LEVEL_INFO, "VFS", "Yeni dosya olusturuldu ve diske yazildi.");
    vfs_unlock();
    return E_OK;
}

int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Sistem Immutable modda. Dosya olusturma engellendi!");
        return E_ROFS;
    }
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        extern int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id);
        return fs_create_encrypted(name, content, size, kernel_master_key, parent_id);
    }
    return fs_create_file_raw(name, content, size, parent_id);
}

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
            vfs_unlock();
            return E_OK;
        }
    }
    vfs_unlock();
    return E_NOENT;
}

int fs_delete(const char *name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Sistem Immutable modda. Dosya silme engellendi!");
        vfs_unlock(); return E_ROFS;
    }

    extern process_t tasks[];
    uint32_t c_uid = (current_task >= 0) ? tasks[current_task].uid : 0;
    
    if (ft_strcmp(name, "passwd") == 0 && c_uid != 0) {
        klog(LOG_LEVEL_WARN, "VFS", "Erisim Engellendi: Sadece ROOT 'passwd' dosyasini silebilir!");
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
            klog(LOG_LEVEL_INFO, "VFS", "Dosya basariyla diskten silindi.");
            vfs_unlock(); return E_OK;
        }
    }
    vfs_unlock();
    return E_NOENT;
}

int fs_rename(const char *old_name, const char *new_name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        klog(LOG_LEVEL_ERROR, "VFS", "Sistem Immutable modda. Dosya adi degistirilemez!");
        vfs_unlock(); return E_ROFS;
    }

    extern process_t tasks[];
    uint32_t c_uid = (current_task >= 0) ? tasks[current_task].uid : 0;
    
    if ((ft_strcmp(old_name, "passwd") == 0 || ft_strcmp(new_name, "passwd") == 0) && c_uid != 0) {
        klog(LOG_LEVEL_WARN, "VFS", "Erisim Engellendi: Sadece ROOT 'passwd' dosyasini adlandirabilir!");
        vfs_unlock(); return E_ACCES;
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && dir_table[i].parent_id == parent_id && ft_strcmp(dir_table[i].filename, new_name) == 0) {
            klog(LOG_LEVEL_WARN, "VFS", "Adlandirma basarisiz: Hedef isimde dosya zaten var!");
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

int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
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

    vfs_lock(); // Değişim işlemi için tekrar kilitle
    int tmp_idx = -1;
    orig_idx = -1;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && dir_table[i].parent_id == parent_id) {
            if (ft_strcmp(dir_table[i].filename, name) == 0) orig_idx = i;
            if (ft_strcmp(dir_table[i].filename, tmp_name) == 0) tmp_idx = i;
        }
    }

    if (orig_idx == -1 || tmp_idx == -1) { vfs_unlock(); return E_NOENT; }

    uint32_t old_sector = dir_table[orig_idx].start_sector;
    uint32_t old_size = dir_table[orig_idx].file_size;

    dir_table[orig_idx].start_sector = dir_table[tmp_idx].start_sector;
    dir_table[orig_idx].file_size = dir_table[tmp_idx].file_size;

    dir_table[tmp_idx].start_sector = old_sector;
    dir_table[tmp_idx].file_size = old_size;
    
    save_directory_to_disk();
    vfs_unlock();
    
    fs_delete(tmp_name, parent_id);
    return E_OK;
}

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

int fs_mkdir(const char *name, uint8_t parent_id) {
    vfs_lock();
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) { vfs_unlock(); return E_ROFS; }
    
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
        klog(LOG_LEVEL_ERROR, "VFS", "Dizin tablosu dolu, yeni klasor acilamadi.");
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

    extern process_t tasks[];
    uint32_t current_uid = (current_task >= 0) ? tasks[current_task].uid : 0;
    dir_table[free_idx].owner_uid = current_uid;

    save_directory_to_disk();
    vfs_unlock(); return E_OK;
}

void fs_list_dir(uint8_t parent_id) {
    vfs_lock();
    printk("Icerik:\n----------------------------------------\n");
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
    if (!found) printk("(Bos)\n");
    printk("----------------------------------------\n");
    vfs_unlock();
}