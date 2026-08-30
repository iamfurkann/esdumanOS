/*
 * File: blockdev.c
 * Purpose: The seam between the file system and whatever it is stored on.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "blockdev.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"

/*
 * A table now, and the sentence that used to stand here explained why there was
 * not one: "there is one disk, and a registry with a single possible entry is a
 * table nobody indexes." That was true for nine releases. mount() is the caller
 * v1.2.0 said would eventually need to choose between devices, and it is here.
 *
 * The point of this file has not changed with it. It is still to stop fs/ naming
 * a driver, and still to hold the bounds check in one place - which matters more
 * with two devices than it did with one, because now there is a second
 * sector_count to check against and a second chance to forget.
 */
static blockdev_t *devices[BLOCKDEV_MAX];
static int device_count = 0;

/** The device the root file system uses. Index 0 unless something moved it. */
static blockdev_t *root_dev = 0;

/**
 * @brief The checks a device must pass before anything is allowed to use it.
 *
 * A device that cannot read is not a device. Registering one would move the
 * failure from here - where it can be reported once, at boot, with a name - to
 * the first mount, where it arrives as a null call through a function pointer.
 *
 * A write handler is not required. A read-only device is a coherent thing to
 * have; blockdev_write() answers E_ROFS for one.
 */
static int device_is_usable(const blockdev_t *dev) {
    if (dev == 0) return 0;
    if (dev->read == 0) return 0;
    if (dev->sector_size == 0 || dev->sector_count == 0) return 0;
    if (dev->name == 0 || dev->name[0] == '\0') return 0;
    return 1;
}

int blockdev_register(blockdev_t *dev) {
    if (!device_is_usable(dev)) {
        klog(LOG_LEVEL_ERROR, "BLOCK", "Refused a device that cannot be read from.");
        return E_INVAL;
    }

    for (int i = 0; i < device_count; i++) {
        if (devices[i] == dev) return E_OK;   /* already in, and idempotent */

        /*
         * Two devices may not share a name, because a name is how mount() is
         * told which one to use. A duplicate would make that instruction
         * ambiguous, and the ambiguity would be resolved by whichever happened
         * to be registered first - which is not a rule anybody could rely on.
         */
        if (ft_strcmp(devices[i]->name, dev->name) == 0) {
            klog(LOG_LEVEL_ERROR, "BLOCK", "Refused a device whose name is already taken.");
            return E_EXIST;
        }
    }

    if (device_count >= BLOCKDEV_MAX) {
        klog_int(LOG_LEVEL_ERROR, "BLOCK",
                 "No room for another device; the table holds", BLOCKDEV_MAX);
        return E_NOMEM;
    }

    devices[device_count++] = dev;

    /* The first one registered is the root, which is how the boot path's
     * IDE-then-AHCI order keeps meaning what it meant in v1.5.0. */
    if (root_dev == 0) root_dev = dev;

    klog_int(LOG_LEVEL_INFO, "BLOCK", "Block device registered. Sectors",
             (int)dev->sector_count);
    return E_OK;
}

/*
 * Setting the root and registering a device are two questions, and this file
 * kept them as one until it had a reason not to.
 *
 * "Which device does the file system use" and "which devices exist" stopped
 * being the same question the moment mount() could change the first without
 * changing the second. Keeping them joined also made a device that is a
 * temporary stand-in for another - a copy with its write handler removed, which
 * is how the read-only path is tested - collide with the original over a name
 * neither of them is being looked up by.
 *
 * So this validates and points; it does not add to the table. A driver that
 * wants its disk to be findable by name calls blockdev_register(), and that is
 * also what makes the first one the root.
 */
int blockdev_set_root(blockdev_t *dev) {
    if (dev == 0) {
        root_dev = 0;
        return E_OK;
    }

    if (!device_is_usable(dev)) {
        klog(LOG_LEVEL_ERROR, "BLOCK", "Refused a root device that cannot be read from.");
        return E_INVAL;
    }

    root_dev = dev;
    return E_OK;
}

blockdev_t *blockdev_root(void) {
    return root_dev;
}

blockdev_t *blockdev_find(const char *name) {
    if (name == 0 || name[0] == '\0') return 0;

    for (int i = 0; i < device_count; i++) {
        if (ft_strcmp(devices[i]->name, name) == 0) return devices[i];
    }
    return 0;
}

blockdev_t *blockdev_get(int index) {
    if (index < 0 || index >= device_count) return 0;
    return devices[index];
}

int blockdev_count(void) {
    return device_count;
}

/**
 * @brief The bounds check both entry points share.
 *
 * One copy, and it is here rather than in the drivers on purpose. The ATA driver
 * carried its own until v1.2.0, which worked and would have been the first thing
 * a second driver forgot - the class of omission this project has hit before
 * with permission checks and with IMMUTABLE guards. With two devices there are
 * two sector counts to check against, which is two chances to forget rather
 * than one.
 */
static int blockdev_check(const blockdev_t *dev, uint32_t lba) {
    if (dev == 0) return E_NODEV;
    if (lba >= dev->sector_count) return E_INVAL;
    return E_OK;
}

int blockdev_read(blockdev_t *dev, uint32_t lba, uint8_t *buf) {
    int res = blockdev_check(dev, lba);

    if (res != E_OK) return res;
    if (buf == 0) return E_INVAL;

    return dev->read(dev->ctx, lba, buf);
}

int blockdev_write(blockdev_t *dev, uint32_t lba, const uint8_t *buf) {
    int res = blockdev_check(dev, lba);

    if (res != E_OK) return res;
    if (buf == 0) return E_INVAL;
    if (dev->write == 0) return E_ROFS;

    return dev->write(dev->ctx, lba, buf);
}
