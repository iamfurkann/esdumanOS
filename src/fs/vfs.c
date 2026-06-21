#include "fs.h"
#include "ata.h"
#include "libft.h"
#include "stdio.h"

static disk_file_entry_t dir_table[MAX_FILES_IN_DIR];

void init_fs(void) {
    uint8_t buf[512];

    ata_read_sector(1, buf);
    disk_file_entry_t *disk_dir = (disk_file_entry_t *)buf;
    for (int i = 0; i < MAX_FILES_IN_DIR; i++)
        dir_table[i] = disk_dir[i];
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
    if (!found)
        printk(" (Dosya sistemi bos veya dizin bulunumadi)\n");
    printk("----------------------------------------\n");
}

int fs_open(const char *name, vfs_file_t *file) {
    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 1 && ft_strcmp(dir_table[i].filename, name) == 0) {
            ft_strcpy(file->filename, dir_table[i].filename);
            file->start_sector = dir_table[i].start_sector;
            file->file_size = dir_table[i].file_size;
            file->current_offset = 0;
            return 0;
        }
    }
    return -1;
}

int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size) {
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

int fs_create_file(const char *name, const char *content) {
    int free_idx = -1;
    uint32_t highest_sector = 2; 

    for (int i = 0; i < MAX_FILES_IN_DIR; i++) {
        if (dir_table[i].is_used == 0 && free_idx == -1) {
            free_idx = i;
        }
        if (dir_table[i].is_used == 1 && dir_table[i].start_sector > highest_sector) {
            highest_sector = dir_table[i].start_sector + (dir_table[i].file_size / 512);
        }
    }

    if (free_idx == -1) return -1;

    uint32_t new_sector = highest_sector + 1;

    uint32_t size = ft_strlen(content);
    if (size > 512) size = 512; 

    uint8_t sector_buf[512];
    ft_memset(sector_buf, 0, 512);
    for(uint32_t i = 0; i < size; i++) {
        sector_buf[i] = content[i];
    }

    ata_write_sector(new_sector, sector_buf);

    ft_memset(dir_table[free_idx].filename, 0, MAX_FILENAME);
    ft_strcpy(dir_table[free_idx].filename, name);
    dir_table[free_idx].start_sector = new_sector;
    dir_table[free_idx].file_size = size;
    dir_table[free_idx].is_used = 1;

    uint8_t dir_buf[512];
    ft_memset(dir_buf, 0, 512);
    uint8_t *dir_ptr = (uint8_t *)dir_table;
    for(uint32_t i = 0; i < (MAX_FILES_IN_DIR * sizeof(disk_file_entry_t)); i++) {
        dir_buf[i] = dir_ptr[i];
    }
    ata_write_sector(1, dir_buf);

    return 0;
}