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
 * v1.2.0 said this: "There is one disk today, so there is one root device rather
 * than a table of them; a registry with a single possible entry would be a table
 * nobody indexes. When a second device arrives it will arrive with a caller that
 * needs to choose between them, and that is the release that should grow this."
 *
 * This is that release, and the second device is the USB stick. The caller is
 * mount(), which does not hold two file systems at once - it chooses which disk
 * the one file system is on, which is exactly the word v1.2.0 used. Holding two
 * at once is v1.12.0, and it is a larger thing than this table.
 *
 * What did not change is where the bounds check lives. It is still in this file,
 * still once, and still not the drivers' problem; that was v1.2.0's real gain
 * and a registry is no reason to spread it out.
 */

/**
 * @brief The most devices this kernel will register.
 *
 * A bounded static table, for the reason every other table here is bounded: the
 * limit is a documented number rather than a memory condition discovered at
 * boot. Four is above anything this kernel can currently produce - an IDE disk,
 * a SATA disk, and a USB stick or two.
 */
#define BLOCKDEV_MAX 4

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
    /**
     * @brief For the log, and now for mount().
     *
     * It was "for the log, and one day others" until v1.11.0. The name is how a
     * user names a disk - `mount usb0` - so it is an interface now rather than a
     * diagnostic, and blockdev_find() is what makes it one. Two registered
     * devices must not share one: blockdev_register() refuses the second.
     */
    const char *name;
    uint32_t    sector_size;   /**< Bytes per sector. 512 everywhere so far. */
    uint32_t    sector_count;  /**< Sectors the device says it has. */

    /** Reads one sector. @return E_OK or a negative errno. */
    int (*read)(void *ctx, uint32_t lba, uint8_t *buf);

    /** Writes one sector. @return E_OK or a negative errno. */
    int (*write)(void *ctx, uint32_t lba, const uint8_t *buf);

    /**
     * @brief Driver's own handle, passed back to read and write untouched.
     *
     * This said "Unused by ATA" for nine releases, and nothing used it: there was
     * one disk per driver and a driver could find it in a static. The USB stick
     * is the first case where one driver has two of them, and the alternative -
     * working out which stick a call is about from the sector number - is not an
     * alternative at all. A field that carries identity is why a second stick is
     * a table entry rather than a second copy of the driver.
     */
    void       *ctx;
} blockdev_t;

/**
 * @brief Adds a device to the table.
 *
 * The struct is not copied; the caller must keep it alive for the life of the
 * system, which for a driver's own static means always.
 *
 * The first device registered also becomes the root, which is what keeps the
 * boot path's meaning unchanged: it probes IDE and then, only if that found
 * nothing, AHCI - so whichever answers first is the disk the system starts on,
 * exactly as in v1.5.0.
 *
 * @param dev The device.
 * @return E_OK, E_INVAL for a device that could not serve a read, E_EXIST when
 *         another device already has that name, or E_NOMEM when the table is
 *         full.
 */
int blockdev_register(blockdev_t *dev);

/**
 * @brief Points the root file system at a device.
 *
 * Does not register it. "Which device does the file system use" and "which
 * devices exist" are two questions, and they stopped being one when mount()
 * gained the ability to change the first without changing the second - so a
 * driver that wants its disk findable by name calls blockdev_register(), and
 * this only decides which one is in use.
 *
 * @param dev The device, or 0 to detach the root; the table is untouched either
 *            way.
 * @return E_OK, or E_INVAL for a device that could not serve a read.
 */
int blockdev_set_root(blockdev_t *dev);

/**
 * @brief The device the root file system is mounted on, or 0 when there is none.
 */
blockdev_t *blockdev_root(void);

/**
 * @brief Finds a registered device by the name it published.
 *
 * This is what turns a string a user typed into something mount() can use, and
 * it is the only place a name is matched.
 *
 * @param name Device name; compared whole.
 * @return The device, or 0 when no device answers to that name.
 */
blockdev_t *blockdev_find(const char *name);

/** @brief The registered device at @p index, or 0 when there is none. */
blockdev_t *blockdev_get(int index);

/** @brief How many devices are registered. */
int blockdev_count(void);

/**
 * @brief Reads one sector from a device.
 *
 * The device is named rather than assumed, which is the whole of what changed in
 * v1.11.0. It used to read the root, because there was one; a second mount makes
 * "the root" the wrong question to ask on the way to a sector.
 *
 * @param dev The device. 0 is refused rather than silently meaning the root -
 *            a caller that lost track of its device is a caller that must not
 *            be handed one.
 * @param lba Sector index, from the start of the device.
 * @param buf At least sector_size bytes.
 * @return E_OK, E_NODEV for no device, E_INVAL when the sector is past the end
 *         of it, or whatever the driver reported.
 */
int blockdev_read(blockdev_t *dev, uint32_t lba, uint8_t *buf);

/**
 * @brief Writes one sector to a device. Errors as blockdev_read(), plus E_ROFS
 *        for a device with no write handler.
 */
int blockdev_write(blockdev_t *dev, uint32_t lba, const uint8_t *buf);

#endif // BLOCKDEV_H
