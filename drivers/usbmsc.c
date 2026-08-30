/*
 * File: usbmsc.c
 * Purpose: A USB stick, presented to the file system as a disk.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Bulk-Only Transport over the endpoints xhci.c opens, and five SCSI commands on
 * top of it. It knows nothing about the controller beyond four calls, and
 * nothing about the file system beyond blockdev_t - which is the shape v1.2.0
 * built this seam for and the third driver to use it unchanged.
 *
 * Every transfer here is synchronous, and the reason it can be is written in
 * xhci.h: xhci_bulk_transfer() drives the event ring itself rather than waiting
 * for a timer tick to drive it. That is what makes a sector cost microseconds
 * instead of thirty milliseconds, and it is also why nothing in this file may be
 * called from an interrupt handler - the rule bcache.c has stated about block
 * devices since v1.2.0, now with something behind it that would actually break.
 *
 * More than one stick, which is the shape of the machine this release is aimed
 * at rather than an abstraction taken on speculation: booting esdumanOS from a
 * flash drive and keeping its file system on another means two of them are
 * plugged in at once, and a driver that drove whichever enumerated first would
 * pick the wrong one about half the time. Everything a stick needs is in
 * usbmsc_disk_t and reached through blockdev_t's ctx - the field that has said
 * "Unused by ATA" since v1.2.0 and is finally carrying something.
 */
#include "usbmsc.h"
#include "xhci.h"
#include "blockdev.h"
#include "errno.h"
#include "klog.h"
#include "libft.h"
#include "stdio.h"

/* Where the three parts of a transport exchange live inside one frame. The
 * command and status wrappers are tiny and the data is a sector; 31 + 13 + 512
 * is 556 bytes of a 4096-byte page, with every one of them at an offset the
 * controller is happy to be handed. One frame per stick, because two sticks can
 * have a command in flight at the same point in the boot and a shared buffer
 * would have them writing over each other's wrappers. */
#define MSC_OFF_CBW  0x000
#define MSC_OFF_CSW  0x040
#define MSC_OFF_DATA 0x200

/**
 * @brief One stick, and everything about it that is not the controller's.
 *
 * The block device is embedded rather than pointed at, so that registering it
 * hands the block layer a struct with the same lifetime as the driver - which
 * blockdev_register() requires and which a static table gives for free.
 */
typedef struct {
    int         device;       /**< xHCI device index, or -1 when unused.     */
    uint8_t    *dma;          /**< The transport frame, mapped uncached.     */
    uint32_t    dma_phys;
    uint32_t    tag;          /**< Per stick: a tag is only unique per queue. */
    uint32_t    sectors;
    blockdev_t  bd;
} usbmsc_disk_t;

static usbmsc_disk_t disks[USBMSC_MAX_DISKS];
static int disk_count = 0;

/*
 * The names, spelled out rather than composed.
 *
 * There is no integer-to-string helper on this side of the kernel and this is
 * not the release to add one for two strings. The array is sized by the same
 * constant that bounds the table, so a slot can never be registered without a
 * name to register it under.
 */
static const char *const disk_names[USBMSC_MAX_DISKS] = { "usb0", "usb1" };

/**
 * @brief Writes a 32-bit value the way SCSI reads one.
 *
 * Big-endian, which is not this machine's order and not the order anything else
 * in this kernel uses. SCSI came from a world that had settled the question
 * differently, and a driver that wrote a block address in the host's order would
 * read sector 0x01000000 when it meant sector 1 - a bug that works perfectly for
 * the first sector and destroys the disk from the second.
 */
static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/** @brief Reads a 32-bit big-endian value, as READ CAPACITY answers with. */
static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/**
 * @brief One Bulk-Only command: wrapper out, data, status back.
 *
 * The tag is incremented per command and checked on the way back. This driver
 * issues one command at a time to any one stick, so a status carrying somebody
 * else's tag does not mean a reply arrived out of order - it means the device
 * and the driver have lost track of each other, which is a different thing from
 * a command that failed and is worth saying separately.
 *
 * The tag counter belongs to the stick rather than to the driver. A shared one
 * would still be unique, but it would make the numbers on the wire depend on
 * what the *other* stick had been asked to do, and a value that moves for
 * reasons outside the exchange it identifies is a bad thing to debug against.
 *
 * @param d The stick.
 * @param cdb The SCSI command block.
 * @param cdb_len Its length, 6 or 10 here.
 * @param data_in Non-zero when the data stage runs device to host.
 * @param data_len Bytes the data stage moves; 0 for a command with no data.
 * @return E_OK when the device reported the command passed.
 */
static int msc_command(usbmsc_disk_t *d, const uint8_t *cdb, uint8_t cdb_len,
                       int data_in, uint32_t data_len) {
    usbmsc_cbw_t *cbw = (usbmsc_cbw_t *)(d->dma + MSC_OFF_CBW);
    usbmsc_csw_t *csw = (usbmsc_csw_t *)(d->dma + MSC_OFF_CSW);
    uint32_t tag = ++d->tag;

    ft_memset(cbw, 0, sizeof(usbmsc_cbw_t));
    cbw->signature   = USBMSC_CBW_SIGNATURE;
    cbw->tag         = tag;
    cbw->data_length = data_len;
    cbw->flags       = data_in ? USBMSC_CBW_IN : 0;
    cbw->lun         = 0;
    cbw->cb_length   = cdb_len;
    ft_memcpy(cbw->cb, cdb, cdb_len);

    if (xhci_bulk_transfer(d->device, 0, d->dma_phys + MSC_OFF_CBW,
                           sizeof(usbmsc_cbw_t), 0) != E_OK) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "The command wrapper was not accepted.");
        return E_IO;
    }

    if (data_len > 0) {
        if (xhci_bulk_transfer(d->device, data_in, d->dma_phys + MSC_OFF_DATA,
                               data_len, 0) != E_OK) {
            /*
             * Deliberately not returned here. A failed data stage still leaves a
             * status wrapper waiting on the IN endpoint, and leaving it there
             * would make it the answer to the *next* command - the tag check
             * would catch that, but only after a second command had already been
             * sent. Reading it now costs one transfer and keeps the two sides in
             * step.
             */
            klog(LOG_LEVEL_WARN, "USBMSC", "The data stage failed; reading the status anyway.");
        }
    }

    ft_memset(csw, 0, sizeof(usbmsc_csw_t));

    if (xhci_bulk_transfer(d->device, 1, d->dma_phys + MSC_OFF_CSW,
                           sizeof(usbmsc_csw_t), 0) != E_OK) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "No status wrapper came back.");
        return E_IO;
    }

    if (csw->signature != USBMSC_CSW_SIGNATURE) {
        klog_hex(LOG_LEVEL_ERROR, "USBMSC",
                 "A status wrapper with the wrong signature", csw->signature);
        return E_IO;
    }

    if (csw->tag != tag) {
        klog(LOG_LEVEL_ERROR, "USBMSC",
             "A status wrapper answering a different command; the device is out of step.");
        return E_IO;
    }

    if (csw->status != USBMSC_CSW_PASSED) {
        /* The device understood and refused. Not a transport failure, and the
         * difference matters: this one is worth retrying, a transport failure is
         * not. */
        return E_IO;
    }

    return E_OK;
}

/** @brief Asks whether the device has media and is willing to be read. */
static int msc_test_unit_ready(usbmsc_disk_t *d) {
    uint8_t cdb[6];

    ft_memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_TEST_UNIT_READY;

    return msc_command(d, cdb, sizeof(cdb), 0, 0);
}

/**
 * @brief Reads the capacity, and refuses a sector size this kernel cannot use.
 *
 * READ CAPACITY(10) answers with the *last* logical block address rather than a
 * count, which is one of the two places this protocol invites an off-by-one; the
 * other is that the answer is big-endian.
 *
 * The answer is stored on the stick that gave it. That reads as obvious and was
 * not: a single static held the capacity until v1.11.0, so a second stick would
 * have been registered with the first one's size - a disk that mounts, reads the
 * sectors it has, and returns nothing for the rest.
 */
static int msc_read_capacity(usbmsc_disk_t *d) {
    uint8_t cdb[10];

    ft_memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_CAPACITY10;

    if (msc_command(d, cdb, sizeof(cdb), 1, 8) != E_OK) return E_IO;

    uint32_t last_lba    = get_be32(d->dma + MSC_OFF_DATA);
    uint32_t block_size  = get_be32(d->dma + MSC_OFF_DATA + 4);

    if (block_size != USBMSC_SECTOR_SIZE) {
        klog_int(LOG_LEVEL_ERROR, "USBMSC",
                 "Refusing a device whose sector size this kernel cannot use",
                 (int)block_size);
        return E_INVAL;
    }

    if (last_lba == 0xFFFFFFFFu) {
        /*
         * The specification's way of saying "ask again with the 16-byte
         * version", which this driver does not send. Refused rather than
         * wrapped: a count of zero would be a disk nothing could mount, and a
         * count of 0xFFFFFFFF would be one that lies about its end.
         */
        klog(LOG_LEVEL_ERROR, "USBMSC",
             "Device is larger than READ CAPACITY(10) can describe; refusing it.");
        return E_INVAL;
    }

    d->sectors = last_lba + 1;
    return E_OK;
}

/**
 * @brief The stick a block layer call is about.
 *
 * From ctx, which is what that field is for. Asking the block layer to carry the
 * identity is what keeps this driver from having to guess it from an lba, and it
 * is the difference between a second stick working and a second stick reading
 * the first one's sectors.
 */
static usbmsc_disk_t *disk_of(void *ctx) {
    return (usbmsc_disk_t *)ctx;
}

static int usbmsc_bd_read(void *ctx, uint32_t lba, uint8_t *buf) {
    usbmsc_disk_t *d = disk_of(ctx);
    uint8_t cdb[10];

    if (d == 0 || d->device < 0 || buf == 0) return E_INVAL;

    ft_memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ10;
    put_be32(cdb + 2, lba);
    cdb[8] = 1;                       /* one block; the block layer's shape */

    if (msc_command(d, cdb, sizeof(cdb), 1, USBMSC_SECTOR_SIZE) != E_OK) {
        /*
         * Zeroed on failure, as the IDE and SATA drivers do, and not enough on
         * its own for the same reason: handing back the previous sector would be
         * worse, and zeros are not evidence. The return value is what the caller
         * has to read.
         */
        ft_memset(buf, 0, USBMSC_SECTOR_SIZE);
        return E_IO;
    }

    ft_memcpy(buf, d->dma + MSC_OFF_DATA, USBMSC_SECTOR_SIZE);
    return E_OK;
}

static int usbmsc_bd_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    usbmsc_disk_t *d = disk_of(ctx);
    uint8_t cdb[10];

    if (d == 0 || d->device < 0 || buf == 0) return E_INVAL;

    ft_memcpy(d->dma + MSC_OFF_DATA, buf, USBMSC_SECTOR_SIZE);

    ft_memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_WRITE10;
    put_be32(cdb + 2, lba);
    cdb[8] = 1;

    return msc_command(d, cdb, sizeof(cdb), 0, USBMSC_SECTOR_SIZE);
}

int usbmsc_present(void) {
    return disk_count > 0;
}

int usbmsc_count(void) {
    return disk_count;
}

/**
 * @brief Brings one enumerated storage device up into the slot it was given.
 *
 * Every failure here is local to the stick. A device that will not describe
 * itself is one this kernel cannot use, and that is a fact about that device
 * rather than about the bus - so the loop below goes on to the next one instead
 * of giving up on all of them. Before v1.11.0 there was only ever one, and
 * "return the error" and "abandon USB storage" were the same sentence.
 *
 * @param d The slot, already carrying its xHCI device index.
 * @return E_OK when the stick is ready to be registered.
 */
static int usbmsc_bring_up(usbmsc_disk_t *d) {
    uint8_t cdb[6];

    if (xhci_open_storage(d->device) != E_OK) return E_IO;

    d->dma = xhci_dma_page(&d->dma_phys);
    if (d->dma == 0) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "Out of memory setting up the transport.");
        return E_NOMEM;
    }

    /*
     * A stick that has just been powered is allowed to say it is not ready, and
     * the specification expects the host to ask again. Bounded, for the reason
     * every wait in the controller driver below is bounded: a device that will
     * never be ready must not hold the boot.
     */
    int ready = 0;

    for (int i = 0; i < USBMSC_READY_TRIES && !ready; i++) {
        if (msc_test_unit_ready(d) == E_OK) ready = 1;
    }

    if (!ready) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "Device never reported itself ready.");
        return E_IO;
    }

    /* Asked and its answer discarded, and that is deliberate rather than
     * careless: what this proves is that the transport carries a data stage in
     * the IN direction, which the two commands after it depend on and which
     * nothing before it has exercised. The vendor string is not information this
     * kernel has any use for. */
    ft_memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_INQUIRY;
    cdb[4] = 36;

    if (msc_command(d, cdb, sizeof(cdb), 1, 36) != E_OK) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "Device would not describe itself.");
        return E_IO;
    }

    return msc_read_capacity(d);
}

int usbmsc_init(void) {
    int found = 0;
    int index = -1;

    /*
     * Every slot starts holding no device. A static table zeroes to 0, and 0 is
     * a perfectly good xHCI device index - so an untouched slot would claim to
     * be the first device on the bus. Nothing reads an unregistered slot today;
     * that is exactly the kind of sentence a later release stops being able to
     * say, and -1 costs two stores.
     */
    for (int i = 0; i < USBMSC_MAX_DISKS; i++) disks[i].device = -1;

    /*
     * Walked rather than asked once. The cursor is the controller's, so this
     * loop cannot disagree with it about how many storage devices there are -
     * which a separate count and index could, and would do so silently.
     */
    while (disk_count < USBMSC_MAX_DISKS &&
           (index = xhci_storage_device_next(index)) >= 0) {
        usbmsc_disk_t *d = &disks[disk_count];

        found++;

        d->device  = index;
        d->tag     = 0;
        d->sectors = 0;

        if (usbmsc_bring_up(d) != E_OK) {
            d->device = -1;
            continue;
        }

        d->bd.name         = disk_names[disk_count];
        d->bd.sector_size  = USBMSC_SECTOR_SIZE;
        d->bd.sector_count = d->sectors;
        d->bd.read         = usbmsc_bd_read;
        d->bd.write        = usbmsc_bd_write;
        d->bd.ctx          = d;

        if (blockdev_register(&d->bd) != E_OK) {
            /*
             * The table is the bound, not a second constant kept in step with
             * it. Registration is also where a name collision would surface, and
             * either way this stick is one the system cannot reach - so it is
             * given up rather than half-installed.
             */
            klog(LOG_LEVEL_ERROR, "USBMSC", "The block device table would not take this stick.");
            d->device = -1;
            continue;
        }

        printk("[USBMSC] USB disk %s: %d sectors (%d KB) [Bulk-Only]\n",
               d->bd.name, d->sectors, d->sectors / 2);
        klog_int(LOG_LEVEL_INFO, "USBMSC", "USB disk registered. Sectors",
                 (int)d->sectors);

        disk_count++;
    }

    if (found == 0) {
        klog(LOG_LEVEL_INFO, "USBMSC", "No USB storage device on the bus.");
        return E_NODEV;
    }

    if (disk_count == 0) {
        klog(LOG_LEVEL_ERROR, "USBMSC", "USB storage was present and none of it came up.");
        return E_IO;
    }

    /*
     * Said out loud when there is more than one, because which stick is which is
     * the question a person is about to have. The names are the answer and this
     * is the only place they are printed together.
     */
    if (disk_count > 1) {
        klog_int(LOG_LEVEL_INFO, "USBMSC", "USB disks registered, usb0 upward", disk_count);
    }

    return E_OK;
}
