#ifndef FS_H
#define FS_H

#include "types.h"

/**
 * @brief FAT markers indicating end of file chain or free clusters.
 */
#define FAT_EOF 0xFFFFFFFF
#define FAT_FREE 0x00000000

/**
 * @brief Maximum length of a filename and maximum number of files per directory.
 */
#define MAX_FILENAME 24
#define MAX_FILES_IN_DIR 32

/**
 * @brief Disk Layout Constants.
 * Define the physical sector boundaries for different file system regions.
 */
#define FS_DIR_START_SECTOR 1
#define FS_DIR_SECTOR_COUNT 4
#define FS_FAT_START_SECTOR 5
#define FS_DATA_START_SECTOR 37

/**
 * @brief File Type Definitions.
 * FT_REGULAR indicates a standard file.
 * FT_DIR indicates a directory.
 */
#define FT_REGULAR 0
#define FT_DIR     1

/**
 * @brief On-disk file entry structure representing a file or directory.
 * Packed to ensure exact byte alignment on storage media.
 */
typedef struct {
    char     filename[MAX_FILENAME]; /**< 24-byte array for the file name. */
    uint32_t file_size;              /**< Size of the file in bytes. */
    uint32_t start_sector;           /**< First sector of the file's data. */
    uint8_t  is_used;                /**< Flag indicating if the entry is active. */
    uint8_t  file_type;              /**< File type (FT_REGULAR or FT_DIR). */
    uint8_t  entry_id;               /**< Unique identifier (0 to 31). */
    uint8_t  parent_id;              /**< Parent directory's entry ID. */
    uint32_t owner_uid;              /**< User ID of the file's owner. */
} __attribute__((packed)) disk_file_entry_t;

/**
 * @brief In-memory Virtual File System (VFS) file structure.
 * Maintains runtime state for open files, including reference counts for safety.
 */
typedef struct {
    char     filename[MAX_FILENAME]; /**< File name. */
    uint32_t file_size;              /**< Size of the file in bytes. */
    uint32_t current_offset;         /**< Current read/write offset within the file. */
    uint32_t start_sector;           /**< Starting sector on the disk. */
    int      ref_count;              /**< Reference count to prevent Use-After-Free vulnerabilities! [SECURITY PATCH ADDED] */
} vfs_file_t;

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
int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);

/**
 * @brief Deletes a file or directory from the filesystem.
 * 
 * @param name The name of the target file.
 * @param parent_id The entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_delete(const char *name, uint8_t parent_id);

/**
 * @brief Renames an existing file or directory.
 * 
 * @param old_name The current name of the file.
 * @param new_name The new name to apply.
 * @param parent_id The entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_rename(const char *old_name, const char *new_name, uint8_t parent_id);

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
int fs_create_file(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);

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
int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id);

/**
 * @brief Creates a new file by writing raw, unprocessed sectors to the disk.
 * 
 * @param name Name of the file.
 * @param content Raw data buffer.
 * @param size Length of the raw data.
 * @param parent_id Entry ID of the parent directory.
 * @return 0 on success, or a negative error code.
 */
int fs_create_file_raw(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);

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
int fs_mkdir(const char *name, uint8_t parent_id);

/**
 * @brief Lists the contents of a specific directory.
 * 
 * @param parent_id Entry ID of the directory to list.
 */
void fs_list_dir(uint8_t parent_id);

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
int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);

#endif // FS_H