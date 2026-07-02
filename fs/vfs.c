#include "fs.h"
#include "ata.h"
#include "libft.h"
#include "stdio.h"
#include "errno.h"
#include "security.h"
#include "crypto.h"

extern uint8_t kernel_master_key[32];

disk_file_entry_t dir_table[MAX_FILES_IN_DIR];
static uint8_t sector_bitmap[512];

static void save_bitmap_to_disk(void) {
    ata_write_sector(FS_BITMAP_SECTOR, sector_bitmap);
}

extern uint32_t ata_identify(void);
uint32_t fs_max_sectors = 4096;
int fs_get_entry_idx(const char *name, uint8_t parent_id);
static void set_sector_used(uint32_t sector) {
    if (sector >= fs_max_sectors) return;
    sector_bitmap[sector / 8] |= (1 << (sector % 8));
}

static void clear_sector(uint32_t sector) {
    if (sector >= fs_max_sectors) return;
    sector_bitmap[sector / 8] &= ~(1 << (sector % 8));
}

static int is_sector_free(uint32_t sector) {
    if (sector >= fs_max_sectors) return 0;
    return !(sector_bitmap[sector / 8] & (1 << (sector % 8)));
}

static uint32_t allocate_free_sectors(uint32_t count) {
    uint32_t contiguous = 0;
    uint32_t start_sector = 0;
    uint32_t total_free = 0; 

    for (uint32_t i = FS_DATA_START_SECTOR; i < fs_max_sectors; i++) {
        if (is_sector_free(i)) {
            total_free++; 
            
            if (contiguous == 0) start_sector = i;
            contiguous++;
            
            if (contiguous == count) {
                for (uint32_t j = 0; j < count; j++) {
                    set_sector_used(start_sector + j);
                }
                save_bitmap_to_disk();
                return start_sector;
            }
        } else {
            contiguous = 0; 
        }
    }
    
    if (total_free >= count) return 0xFFFFFFFE; 
    return 0; 
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
        ata_write_sector(FS_DIR_START_SECTOR + i, sec_buf);
    }
}

void init_fs(void) {
    fs_max_sectors = ata_identify();

    if (fs_max_sectors > 4096) {
        fs_max_sectors = 4096; 
    }

    uint8_t *dir_ptr = (uint8_t *)dir_table;
    uint32_t total_bytes = sizeof(disk_file_entry_t) * MAX_FILES_IN_DIR;
    uint32_t sectors_needed = (total_bytes + 511) / 512;

    for (uint32_t i = 0; i < sectors_needed; i++) {
        uint8_t sec_buf[512];
        ata_read_sector(FS_DIR_START_SECTOR + i, sec_buf);
        
        uint32_t copy_size = 512;
        if ((i * 512) + 512 > total_bytes) {
            copy_size = total_bytes % 512;
        }
        for (uint32_t j = 0; j < copy_size; j++) {
            dir_ptr[(i * 512) + j] = sec_buf[j];
        }
    }
    
    ata_read_sector(FS_BITMAP_SECTOR, sector_bitmap);
    for (uint32_t i = 0; i < FS_DATA_START_SECTOR; i++) {
        set_sector_used(i);
    }
    save_bitmap_to_disk();
}

void fs_list_files(void) {
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
}

int fs_read_raw(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    if (file->current_offset >= file->file_size) return 0;

    if (file->current_offset + size > file->file_size)
        size = file->file_size - file->current_offset;

    uint32_t bytes_read = 0;

    while (bytes_read < size) {
        uint32_t current_sector = file->start_sector + (file->current_offset / 512);
        uint32_t offset_in_sector = file->current_offset % 512;

        uint8_t sector_buf[512];
        ata_read_sector(current_sector, sector_buf);

        uint32_t bytes_to_copy = 512 - offset_in_sector;
        if (bytes_to_copy > (size - bytes_read)) {
            bytes_to_copy = size - bytes_read;
        }

        for (uint32_t i = 0; i < bytes_to_copy; i++) {
            buffer[bytes_read + i] = sector_buf[offset_in_sector + i];
        }

        file->current_offset += bytes_to_copy;
        bytes_read += bytes_to_copy;
    }
    return bytes_read;
}

int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        return fs_read_encrypted(file, buffer, size, kernel_master_key);
    }

    return fs_read_raw(file, buffer, size);
}

int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
    vfs_file_t temp_file;
    if (fs_open(name, parent_id, &temp_file) == E_OK) {
        return E_EXISTS;
    }

    int free_idx = -1;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 0) {
            free_idx = i;
            break;
        }
    }
    int existing_idx = fs_get_entry_idx(name, parent_id);
    if (existing_idx != -1) return E_EXISTS;

    if (free_idx == -1) {
        printk("HATA: Dosya sistemi (Dizin tablosu) dolu!\n");
        return E_NOMEM;
    }

    uint32_t sectors_needed = (size == 0) ? 1 : ((size + 511) / 512);

    uint32_t new_sector = allocate_free_sectors(sectors_needed);
    if (new_sector == 0xFFFFFFFE) {
        terminal_setcolor(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        printk("[VFS HATA] Disk parcalanmasi (Fragmentation) nedeniyle ardisik sektor bulunamadi!\n");
        printk("[VFS BILGI] FAT zincirleme yapisi gerekli.\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return E_NOMEM;
    } else if (new_sector == 0) {
        printk("HATA: Disk tamamen dolu!\n");
        return E_NOMEM;
    }

    uint32_t bytes_written = 0;
    for (uint32_t s = 0; s < sectors_needed; s++) {
        uint8_t sector_buf[512];
        ft_memset(sector_buf, 0, 512);

        uint32_t chunk_size = 512;
        if (bytes_written + 512 > size) {
            chunk_size = size - bytes_written;
        }

        for(uint32_t i = 0; i < chunk_size; i++) {
            sector_buf[i] = content[bytes_written + i];
        }

        ata_write_sector(new_sector + s, sector_buf);
        bytes_written += chunk_size;
    }

ft_memset(dir_table[free_idx].filename, 0, MAX_FILENAME);
    ft_strcpy(dir_table[free_idx].filename, name);
    dir_table[free_idx].start_sector = new_sector;
    dir_table[free_idx].file_size = size;
    dir_table[free_idx].is_used = 1;
    dir_table[free_idx].file_type = 0;
    dir_table[free_idx].entry_id = free_idx; 
    dir_table[free_idx].parent_id = parent_id; 

    save_directory_to_disk();
    return E_OK;
}


int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id) {
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        printk("[VFS HATA] Sistem Immutable (Salt-Okunur) modda. Diske yazilamaz!\n");
        return E_ACCESS;
    }

    if (current_sec_level >= SEC_LEVEL_CRYPTO_ENFORCED) {
        extern int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id);
        return fs_create_encrypted(name, content, size, kernel_master_key, parent_id);
    }

    return fs_create_file_raw(name, content, size, parent_id);
}

int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
            
            ft_strcpy(file->filename, dir_table[i].filename);
            file->start_sector = dir_table[i].start_sector;
            file->file_size = dir_table[i].file_size;
            file->current_offset = 0;
            return E_OK;
        }
    }
    return E_NOENT;
}

int fs_delete(const char *name, uint8_t parent_id) {
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        printk("[VFS HATA] Sistem Immutable (Salt-Okunur) modda. Dosya silinemez!\n");
        return E_ACCESS;
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
            
            uint32_t sectors_used = (dir_table[i].file_size + 511) / 512;
            if (sectors_used == 0) sectors_used = 1;
            
            for (uint32_t s = 0; s < sectors_used; s++) {
                clear_sector(dir_table[i].start_sector + s);
            }
            save_bitmap_to_disk();
            dir_table[i].is_used = 0; 
            ft_memset(dir_table[i].filename, 0, MAX_FILENAME);
            save_directory_to_disk();
            return E_OK;
        }
    }
    return E_NOENT;
}

// 3. YENİDEN ADLANDIRMA (MV) FONKSİYONU
int fs_rename(const char *old_name, const char *new_name, uint8_t parent_id) {
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) {
        printk("[VFS HATA] Sistem Immutable (Salt-Okunur) modda. Dosya adi degistirilemez!\n");
        return E_ACCESS;
    }

    vfs_file_t temp_file;
    if (fs_open(new_name, parent_id, &temp_file) == E_OK) {
        printk("HATA: '%s' adinda bir dosya zaten mevcut!\n", new_name);
        return E_EXISTS;
    }

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        // [DÜZELTME]: Filtre eklendi
        if (dir_table[i].is_used == 1 && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, old_name) == 0) {
            
            ft_memset(dir_table[i].filename, 0, MAX_FILENAME);
            ft_strcpy(dir_table[i].filename, new_name);
            save_directory_to_disk();
            return E_OK;
        }
    }
    return E_NOENT;
}

int fs_get_entry_idx(const char *name, uint8_t parent_id) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used && 
            dir_table[i].parent_id == parent_id && 
            ft_strcmp(dir_table[i].filename, name) == 0) {
            return i;
        }
    }
    return -1;
}

int fs_mkdir(const char *name, uint8_t parent_id) {
    if (current_sec_level == SEC_LEVEL_IMMUTABLE) return E_ACCESS;
    if (fs_get_entry_idx(name, parent_id) != -1) return E_EXISTS;

    int free_idx = -1;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 0) {
            free_idx = i;
            break;
        }
    }
    if (free_idx == -1) return E_NOMEM;

    ft_memset(dir_table[free_idx].filename, 0, MAX_FILENAME);
    ft_strcpy(dir_table[free_idx].filename, name);
    dir_table[free_idx].file_size = 0; 
    dir_table[free_idx].start_sector = 0;
    dir_table[free_idx].file_type = 1;
    dir_table[free_idx].entry_id = free_idx;
    dir_table[free_idx].parent_id = parent_id;
    dir_table[free_idx].is_used = 1;

    save_directory_to_disk();
    return E_OK;
}

void fs_list_dir(uint8_t parent_id) {
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
}