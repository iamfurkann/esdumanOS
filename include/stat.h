#ifndef STAT_H
#define STAT_H

#include "types.h"

/**
 * @brief File metadata returned by stat() and fstat().
 *
 * Shared verbatim between the kernel and user space - programs build with
 * "-I include" (see USER_CFLAGS in the Makefile), so there is one definition
 * rather than a copy on each side that could drift.
 *
 * Field order is chosen so the struct has no padding holes at -m32: eight 32-bit
 * words, then a 16-bit field and two bytes that together fill one more.
 * copy_to_user() moves it as a flat block, so a hole would leak whatever the
 * kernel stack happened to hold there.
 *
 * It said there was deliberately no st_mtime, because disk_file_entry_t carried
 * no timestamps and anything reported here would have been invented - and that
 * when the format grew a real one, this struct could grow with it. v0.9.0 is
 * that release, and it did.
 */
typedef struct {
    /**
     * Bytes a read() will actually return.
     *
     * Under SEC_LEVEL_CRYPTO_ENFORCED - the default - this is smaller than what
     * the file occupies on disk, because the stored form carries an IV, a header
     * and padding. It comes from fs_size(), not from the directory table.
     *
     * BOUNDARY: for an encrypted file this length is read out of the file's own
     * header, which is encrypted but not authenticated until read() verifies the
     * HMAC over the whole plaintext. A tampered header can therefore report a
     * wrong size here; the read that follows fails, but the size alone is not
     * evidence of integrity.
     */
    uint32_t st_size;

    /** Bytes occupied on disk. Differs from st_size whenever st_encrypted is 1. */
    uint32_t st_disk_size;

    /** Directory table entry id, stable for the life of the entry. */
    uint32_t st_ino;

    /** Owning user id. */
    uint32_t st_uid;

    /** Owning group id. There is no group database, so it tracks the uid. */
    uint32_t st_gid;

    /** Entry id of the containing directory; the root is 0 and is its own parent. */
    uint32_t st_parent;

    /**
     * Seconds since the Unix epoch, UTC, when the contents were last written.
     *
     * Zero means the entry predates the timestamp, which nothing on a disk this
     * kernel will mount can - it refuses older formats - but zero is the honest
     * answer for a field that was never set rather than a plausible-looking date.
     */
    uint32_t st_mtime;

    /** Seconds since the Unix epoch, UTC, when the entry was created. */
    uint32_t st_ctime;

    /**
     * Permission bits.
     *
     * Recorded, reported, and consulted. check_vfs_access() has decided a
     * directory's permissions by these bits rather than by file names since
     * v0.9.1, and check_file_access() has asked the same of the file itself
     * since v0.9.4 - before which a mode on a file was reported here and read by
     * nothing. What is shown is what the system enforces.
     *
     * Includes the sticky bit, 01000, which is meaningful on a directory.
     */
    uint16_t st_mode;

    /** FT_REGULAR or FT_DIR, as defined in fs.h. */
    uint8_t st_type;

    /** 1 when the bytes on disk are ciphertext. */
    uint8_t st_encrypted;
} esd_stat_t;

/**
 * @brief Origins accepted by lseek().
 */
#define SEEK_SET 0 /**< Offset is absolute.                        */
#define SEEK_CUR 1 /**< Offset is relative to the current position. */
#define SEEK_END 2 /**< Offset is relative to the end of the file.  */

#endif // STAT_H
