#ifndef FS_H
#define FS_H

#include "types.h"

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
 */
#define MAX_FILES_IN_DIR 512

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
 * The arithmetic: 512 entries of 96 bytes is 49152 bytes, which is exactly 96
 * sectors, so the directory table is sectors 1 to 96. The FAT holds one uint32_t
 * per sector, so 4096 sectors is 16384 bytes and 32 more sectors, 97 to 128. Data
 * starts at 200 as it always has, leaving 129 to 199 spare - the room the next
 * format change gets to grow into without moving a single byte of anybody's file.
 */
#define FS_DIR_START_SECTOR   1
#define FS_DIR_SECTOR_COUNT   96
#define FS_FAT_START_SECTOR   97
#define FS_DATA_START_SECTOR  200

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
 * Stored and reported as of v0.9.0; nothing enforces them yet. check_vfs_access()
 * still decides by comparing names, and replacing it is what v0.9.1 is for. A
 * mode that is recorded truthfully and not yet consulted is a smaller lie than a
 * mode invented at the moment something asks for one.
 */
#define FS_MODE_DEFAULT_FILE 0644
#define FS_MODE_DEFAULT_DIR  0755
#define FS_MODE_PERM_MASK    07777

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
    uint32_t start_sector;           /**< First sector of the file's data. */
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
#define FS_FORMAT_VERSION 1

typedef struct {
    uint32_t magic;           /**< FS_SUPER_MAGIC when this is one of ours. */
    uint16_t format_version;  /**< FS_FORMAT_VERSION; a higher one is refused. */
    uint16_t sector_size;     /**< Bytes per sector; 512. */
    uint32_t total_sectors;   /**< Sectors the file system was formatted across. */
    uint32_t dir_start;       /**< First sector of the directory table. */
    uint32_t dir_sectors;     /**< Sectors the directory table occupies. */
    uint32_t max_entries;     /**< Entries the directory table holds. */
    uint16_t entry_size;      /**< Bytes per directory entry. */
    uint16_t cluster_sectors; /**< Sectors per allocation unit; 1 today. */
    uint32_t fat_start;       /**< First sector of the allocation table. */
    uint32_t data_start;      /**< First sector file data may occupy. */
    uint32_t flags;           /**< Reserved; zero. */
} __attribute__((packed)) disk_superblock_t;

/**
 * @brief The geometry the mounted file system is actually being read with.
 *
 * Filled by init_fs() from the disk, not from the constants above. Everything
 * that addresses a sector goes through this.
 */
extern disk_superblock_t fs_super;

/**
 * @brief Whether a file system was successfully mounted.
 *
 * Zero when the disk held something this kernel would not touch. Every entry
 * point checks it: refusing to mount and then writing anyway would be worse than
 * not checking at all.
 */
extern int fs_mounted;

/**
 * @brief Sectors the device reports, capped at what the allocation table holds.
 */
extern uint32_t fs_max_sectors;

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

/**
 * @brief Lists the contents of a specific directory.
 * 
 * @param parent_id Entry ID of the directory to list.
 */
void fs_list_dir(fs_id_t parent_id);

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


// --- Added by Refactor Script ---
extern int fs_get_entry_idx(const char *name, fs_id_t parent_id);
extern disk_file_entry_t dir_table[];
extern void init_fs(void);

#endif // FS_H