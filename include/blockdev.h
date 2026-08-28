#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "types.h"

/**
 * @file blockdev.h
 * @brief The one thing the file system is allowed to know about storage.
 *
 * Until v1.2.0 the block cache called ata_read_sector() and ata_write_sector()
 * directly, so the file system knew it was sitting on an IDE disk reached
 * through PIO on ports 0x1F0-0x1F7. Nothing about the file system needs that to
 * be true, and everything this project wants to do next - a real machine's SATA
 * controller, a USB stick - needs it not to be.
 *
 * The interface is deliberately small. There is one disk today, so there is one
 * root device rather than a table of them; a registry with a single possible
 * entry would be a table nobody indexes. When a second device arrives it will
 * arrive with a caller that needs to choose between them, and that is the
 * release that should grow this.
 */

/**
 * @brief A device that reads and writes fixed-size sectors.
 *
 * The handlers return E_OK or a negative errno. They are not asked to
 * range-check the LBA - blockdev_read() and blockdev_write() do that once,
 * against sector_count, so that the next driver does not have to remember to.
 * That check lived inside the ATA driver until v1.2.0, which is exactly the kind
 * of thing a second implementation silently omits.
 */
typedef struct {
    const char *name;          /**< For the log. "ata0", and one day others. */
    uint32_t    sector_size;   /**< Bytes per sector. 512 everywhere so far. */
    uint32_t    sector_count;  /**< Sectors the device says it has. */

    /** Reads one sector. @return E_OK or a negative errno. */
    int (*read)(void *ctx, uint32_t lba, uint8_t *buf);

    /** Writes one sector. @return E_OK or a negative errno. */
    int (*write)(void *ctx, uint32_t lba, const uint8_t *buf);

    void       *ctx;           /**< Driver's own handle. Unused by ATA. */
} blockdev_t;

/**
 * @brief Makes a device the one the file system mounts.
 *
 * The struct is not copied; the caller must keep it alive for the life of the
 * system, which for a driver's own static means always.
 *
 * @param dev The device, or 0 to detach.
 * @return E_OK, or E_INVAL for a device that could not serve a read.
 */
int blockdev_set_root(blockdev_t *dev);

/**
 * @brief The device the file system is mounted on, or 0 when there is none.
 *
 * Exposed so that a caller can ask how big the disk is. Nothing outside this
 * file should call the handlers through it - blockdev_read() and
 * blockdev_write() are where the bounds check lives.
 */
blockdev_t *blockdev_root(void);

/**
 * @brief Reads one sector from the root device.
 *
 * @param lba Sector index, from the start of the device.
 * @param buf At least sector_size bytes.
 * @return E_OK, E_NODEV when nothing is registered, E_INVAL when the sector is
 *         past the end of the device, or whatever the driver reported.
 */
int blockdev_read(uint32_t lba, uint8_t *buf);

/**
 * @brief Writes one sector to the root device. Errors as blockdev_read().
 */
int blockdev_write(uint32_t lba, const uint8_t *buf);

#endif // BLOCKDEV_H
