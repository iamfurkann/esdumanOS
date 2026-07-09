#ifndef FS_H
#define FS_H

#include "types.h"

#define MAX_FILENAME 24
#define MAX_FILES_IN_DIR 32

// Disk Düzeni (Layout) Sabitleri
#define FS_DIR_START_SECTOR 1
#define FS_DIR_SECTOR_COUNT 4
#define FS_BITMAP_SECTOR 5
#define FS_DATA_START_SECTOR 6

#define FT_REGULAR 0  // Normal dosya
#define FT_DIR     1  // Dizin (Klasör)

typedef struct {
    char     filename[MAX_FILENAME]; // 24 Byte
    uint32_t file_size;              // 4 Byte
    uint32_t start_sector;           // 4 Byte
    uint8_t  is_used;                // 1 Byte
    uint8_t  file_type;              // 1 Byte (FT_REGULAR veya FT_DIR)
    uint8_t  entry_id;               // 1 Byte (0 - 31 arası eşsiz ID)
    uint8_t  parent_id;              // 1 Byte (Üst dizinin entry_id'si, Kök dizin için 0)
    uint32_t owner_uid;
} disk_file_entry_t;

typedef struct {
    char     filename[32];
    uint32_t file_size;
    uint32_t current_offset;
    uint32_t start_sector;
} vfs_file_t;

int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size);
int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);
int fs_delete(const char *name, uint8_t parent_id);
int fs_rename(const char *old_name, const char *new_name, uint8_t parent_id);
void fs_list_files(void);
int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id);
int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);

int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]);
int fs_read_raw(vfs_file_t *file, uint8_t *buffer, uint32_t size);
int fs_mkdir(const char *name, uint8_t parent_id);
void fs_list_dir(uint8_t parent_id);

#endif // FS_H