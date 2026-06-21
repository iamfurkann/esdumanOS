#ifndef FS_H
#define FS_H

#include "types.h"

#define MAX_FILENAME 32
#define MAX_FILES_IN_DIR 8

typedef struct {
    char filename[MAX_FILENAME];
    uint32_t start_sector;
    uint32_t file_size;
    uint32_t is_used;
} __attribute__((packed)) disk_file_entry_t;

typedef struct {
    char filename[MAX_FILENAME];
    uint32_t start_sector;
    uint32_t file_size;
    uint32_t current_offset;
} vfs_file_t;

void init_fs(void);
int fs_open(const char *name, vfs_file_t *file);
int fs_read(vfs_file_t *file, uint8_t *buffer, uint32_t size);
void fs_list_files(void);
int fs_create_file(const char *name, const char *content);

#endif