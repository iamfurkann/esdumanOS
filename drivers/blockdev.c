/*
 * File: blockdev.c
 * Purpose: The seam between the file system and whatever it is stored on.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "blockdev.h"
#include "errno.h"
#include "klog.h"

/*
 * One device, not a table.
 *
 * There is one disk, and a registry with a single possible entry is a table
 * nobody indexes. The point of this file is not to hold several devices - it is
 * to stop fs/ naming one, which it did until v1.2.0 by calling ata_read_sector()
 * from the block cache.
 */
static blockdev_t *root_dev = 0;

int blockdev_set_root(blockdev_t *dev) {
    if (dev == 0) {
        root_dev = 0;
        return E_OK;
    }

    /*
     * A device that cannot read is not a device. Registering one would move the
     * failure from here - where it can be reported once, at boot, with a name -
     * to the first mount, where it arrives as a null call through a function
     * pointer.
     *
     * A write handler is not required by this test. A read-only device is a
     * coherent thing to have; blockdev_write() answers E_ROFS for one.
     */
    if (dev->read == 0 || dev->sector_size == 0 || dev->sector_count == 0) {
        klog(LOG_LEVEL_ERROR, "BLOCK", "Refused a device that cannot be read from.");
        return E_INVAL;
    }

    root_dev = dev;
    klog_int(LOG_LEVEL_INFO, "BLOCK", "Root block device registered. Sectors",
             (int)dev->sector_count);
    return E_OK;
}

blockdev_t *blockdev_root(void) {
    return root_dev;
}

/**
 * @brief The bounds check both entry points share.
 *
 * One copy, and it is here rather than in the drivers on purpose. The ATA driver
 * carried its own until v1.2.0, which worked and would have been the first thing
 * a second driver forgot - the class of omission this project has hit before
 * with permission checks and with IMMUTABLE guards.
 */
static int blockdev_check(uint32_t lba) {
    if (root_dev == 0) return E_NODEV;
    if (lba >= root_dev->sector_count) return E_INVAL;
    return E_OK;
}

int blockdev_read(uint32_t lba, uint8_t *buf) {
    int res = blockdev_check(lba);

    if (res != E_OK) return res;
    if (buf == 0) return E_INVAL;

    return root_dev->read(root_dev->ctx, lba, buf);
}

int blockdev_write(uint32_t lba, const uint8_t *buf) {
    int res = blockdev_check(lba);

    if (res != E_OK) return res;
    if (buf == 0) return E_INVAL;
    if (root_dev->write == 0) return E_ROFS;

    return root_dev->write(root_dev->ctx, lba, buf);
}
