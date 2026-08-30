#ifndef USBMSC_H
#define USBMSC_H

#include "types.h"

/**
 * @file usbmsc.h
 * @brief USB sticks, as disks the file system can be told to use.
 *
 * The last thing 2.0.0 needs. The screen stopped being text mode in v1.6.0, the
 * boot path learned UEFI in v1.7.0, and the keyboard came off the PS/2 port in
 * v1.10.0; what was left was that a machine booted from a stick could not read
 * the stick. This is that.
 *
 * Bulk-Only Transport, which is three bulk transfers per command: a 31-byte
 * command wrapper out, the data in whichever direction the command wants, and a
 * 13-byte status wrapper back. On top of it, five SCSI commands - TEST UNIT
 * READY, INQUIRY, READ CAPACITY(10), READ(10) and WRITE(10) - enough to learn
 * the device is there, learn how big it is, and move a sector each way. Nothing
 * here implements a queue, a second logical unit, or any of the two other
 * transports the specification defines.
 *
 * Sticks register as blockdev_t called "usb0" and "usb1", and are deliberately
 * **not** made the root. The boot path's order - IDE, then AHCI only if IDE
 * found nothing - is what made v1.5.0 additive rather than a gamble, and a third
 * driver that quietly took the root would undo it. The stick is chosen with
 * mount().
 */

/**
 * @brief How many USB sticks this kernel will drive at once.
 *
 * Two, and the number comes from the machine rather than from taste: booting
 * esdumanOS from a flash drive and keeping its file system on another means two
 * are plugged in at the same time, and the first release that drove one drove
 * whichever enumerated first - which is the wrong one about half the time.
 *
 * Bounded for the reason every table here is bounded, and bounded *below*
 * BLOCKDEV_MAX rather than beside it: four block devices is an internal disk and
 * two sticks with room to spare, and blockdev_register() is still the thing that
 * decides, so the two numbers cannot disagree about what fits.
 */
#define USBMSC_MAX_DISKS 2

/** @brief "USBC", little-endian, at the head of every command wrapper. */
#define USBMSC_CBW_SIGNATURE 0x43425355u

/** @brief "USBS", at the head of every status wrapper. */
#define USBMSC_CSW_SIGNATURE 0x53425355u

/** @brief CBW flags bit 7: the data stage runs device to host. */
#define USBMSC_CBW_IN 0x80

/** @brief CSW status: the command did what it was asked. */
#define USBMSC_CSW_PASSED 0

/**
 * @brief Command Block Wrapper. Exactly 31 bytes, and the hardware counts them.
 *
 * The length is not a detail of this structure - it is a field of the protocol.
 * A device receiving 32 bytes where it expects 31 does not read a padded wrapper
 * and carry on; it stalls the endpoint, and the recovery for that is a reset
 * this driver does not implement. So the size is asserted in the test module,
 * for the same reason a TRB's sixteen bytes are.
 */
typedef struct {
    uint32_t signature;
    uint32_t tag;             /**< Echoed in the status wrapper.             */
    uint32_t data_length;     /**< Bytes the data stage will move.           */
    uint8_t  flags;           /**< USBMSC_CBW_IN, or 0 for host to device.   */
    uint8_t  lun;             /**< Always 0 here; see the file comment.      */
    uint8_t  cb_length;       /**< Bytes of cb that mean anything, 1 to 16.  */
    uint8_t  cb[16];          /**< The SCSI command itself.                  */
} __attribute__((packed)) usbmsc_cbw_t;

/**
 * @brief Command Status Wrapper. Exactly 13 bytes.
 *
 * The tag is what makes this an answer rather than a coincidence: it is the one
 * from the command wrapper, echoed, and a status carrying a different one
 * belongs to some other command. This driver issues one command at a time, so a
 * mismatch means the device and the driver have lost each other rather than that
 * a reply arrived out of order - which is worth telling apart from a command
 * that simply failed.
 */
typedef struct {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;         /**< Bytes of data_length not moved.           */
    uint8_t  status;          /**< USBMSC_CSW_PASSED, or a failure.          */
} __attribute__((packed)) usbmsc_csw_t;

/* The SCSI commands this driver sends, and no others. */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A

/**
 * @brief The only sector size this kernel can use.
 *
 * blockdev_t carries a sector_size and every caller in the tree assumes 512:
 * bcache_node_t's buffer is 512 bytes, the superblock's geometry counts 512-byte
 * sectors, and the MBR is read as one. A stick reporting 4096 is refused by name
 * rather than read with the wrong arithmetic - which would produce a disk that
 * mounts and is wrong everywhere.
 */
#define USBMSC_SECTOR_SIZE 512

/**
 * @brief How many times TEST UNIT READY is asked before the device is given up on.
 *
 * A stick that has just been powered is allowed to say it is not ready yet, and
 * the specification expects the host to ask again. Bounded, because a device
 * that will never be ready must not hold the boot: this is the same discipline
 * every wait in the xHCI driver has, one level up.
 */
#define USBMSC_READY_TRIES 8

/**
 * @brief Finds the mass storage devices, sizes them, and registers them.
 *
 * Called from the boot path after xhci_init(), and gated on nothing - a machine
 * with no stick gets one log line and no change of behaviour, exactly as the
 * SATA and USB probes before it.
 *
 * A stick that will not come up costs only itself: the walk goes on to the next
 * one. That distinction did not exist while there was one stick, where "report
 * the error" and "give up on USB storage" were the same sentence.
 *
 * @return E_OK when at least one disk was registered, E_NODEV when the bus
 *         carries no storage at all, or E_IO when some was there and none of it
 *         answered.
 */
int usbmsc_init(void);

/** @brief Non-zero once a stick has been registered as a block device. */
int usbmsc_present(void);

/**
 * @brief How many sticks were registered, 0 to USBMSC_MAX_DISKS.
 *
 * Separate from usbmsc_present() because "is there one" and "how many" are
 * different questions and the second one only started having interesting answers
 * in v1.11.0.
 */
int usbmsc_count(void);

#endif // USBMSC_H
