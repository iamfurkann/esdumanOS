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
 * Field order is chosen so the struct has no padding holes at -m32: five 32-bit
 * words, then two bytes and a 16-bit tail that together fill one more word.
 * copy_to_user() moves it as a flat block, so a hole would leak whatever the
 * kernel stack happened to hold there.
 *
 * There is deliberately no st_mtime. disk_file_entry_t carries no timestamps
 * (see fs.h) and the RTC is not wired into the VFS at all, so any time reported
 * here would be invented. When the on-disk format grows a real one, this struct
 * can grow with it.
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

    /** Entry id of the containing directory; the root is 0 and is its own parent. */
    uint32_t st_parent;

    /** FT_REGULAR or FT_DIR, as defined in fs.h. */
    uint8_t st_type;

    /** 1 when the bytes on disk are ciphertext. */
    uint8_t st_encrypted;

    uint16_t st_reserved;
} esd_stat_t;

/**
 * @brief Origins accepted by lseek().
 */
#define SEEK_SET 0 /**< Offset is absolute.                        */
#define SEEK_CUR 1 /**< Offset is relative to the current position. */
#define SEEK_END 2 /**< Offset is relative to the end of the file.  */

#endif // STAT_H
