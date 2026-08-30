/*
 * File: test_usbmsc.c
 * Purpose: Bulk-Only Transport, and the disk it presents.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Half of this is arithmetic about two structures, and that half matters more
 * than it looks. A command wrapper is thirty-one bytes and a status wrapper is
 * thirteen, and those are not properties of the C this driver is written in -
 * they are fields of the protocol. A device handed thirty-two bytes where it
 * expects thirty-one does not read a padded wrapper and carry on; it stalls the
 * endpoint, and the recovery for that is a reset this driver does not implement.
 * So a byte of padding the compiler decided to insert would be a stick that
 * enumerates, reports its size, and then hangs on the first command.
 *
 * The other half is the hardware, and it is only assertable because every kernel
 * test target now has two sticks attached - the same variable in the Makefile
 * that gives them all a keyboard and a mouse. Two rather than one, because one
 * stick cannot tell a driver that drives every stick from a driver that drives
 * the first one it finds, and the second reading is what the first release of
 * this driver actually implemented.
 */
#include "ktest.h"
#include "usbmsc.h"
#include "blockdev.h"
#include "xhci.h"
#include "errno.h"
#include "libft.h"

/**
 * @brief The two shapes the device counts rather than parses.
 */
static void run_layout_assertions(void) {
    KTEST_ASSERT(sizeof(usbmsc_cbw_t) == 31,
                 "[STRICT] [USBMSC] a command wrapper is exactly 31 bytes, as the device counts them");
    KTEST_ASSERT(sizeof(usbmsc_csw_t) == 13,
                 "[STRICT] [USBMSC] and a status wrapper is exactly 13");

    /*
     * The command block starts at byte 15, after four four-byte fields and three
     * single-byte ones. If the compiler aligned any of the three the block would
     * move, the device would read the tail of the header as a SCSI opcode, and
     * what it did next would be its own business.
     */
    usbmsc_cbw_t cbw;
    uint32_t cb_offset = (uint32_t)((uint8_t *)cbw.cb - (uint8_t *)&cbw);

    KTEST_ASSERT(cb_offset == 15,
                 "[STRICT] [USBMSC] the command block sits at byte 15, where the device looks for it");

    /*
     * The signatures are ASCII read as little-endian words - "USBC" and "USBS".
     * Asserted as the characters rather than compared against the same constant
     * they are defined as, which would only prove the constant equals itself.
     */
    KTEST_ASSERT(USBMSC_CBW_SIGNATURE == 0x43425355u &&
                 (USBMSC_CBW_SIGNATURE & 0xFF) == 'U' &&
                 ((USBMSC_CBW_SIGNATURE >> 24) & 0xFF) == 'C',
                 "[STRICT] [USBMSC] the command signature is \"USBC\" read the way the wire sends it");

    KTEST_ASSERT(USBMSC_CSW_SIGNATURE == 0x53425355u &&
                 ((USBMSC_CSW_SIGNATURE >> 24) & 0xFF) == 'S',
                 "[STRICT] [USBMSC] and the status signature is \"USBS\"");

    KTEST_ASSERT(USBMSC_CBW_IN == 0x80,
                 "[STRICT] [USBMSC] the direction flag is bit 7, which is where the device reads it");
}

/**
 * @brief The disk, as the block layer sees it.
 *
 * Everything here goes through blockdev_find("usb0") rather than through the
 * driver, because that is the only thing the file system will ever use - and a
 * driver that works and is registered wrongly is a driver nothing can reach.
 */
static void run_registration_assertions(void) {
    KTEST_ASSERT(usbmsc_present(),
                 "[STRICT] [USBMSC] a USB disk was found and registered");

    blockdev_t *usb = blockdev_find("usb0");

    if (usb == 0) {
        KTEST_ASSERT(0, "[STRICT] [USBMSC] and it answers to the name mount uses");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] with a capacity and 512-byte sectors");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] and it is not the root, which the boot order decides");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] a sector reads back through the whole transport");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] and a sector past the end is refused before the device sees it");
        return;
    }

    KTEST_ASSERT(usb->read != 0 && usb->write != 0,
                 "[STRICT] [USBMSC] and it answers to the name mount uses");

    KTEST_ASSERT(usb->sector_count > 0 && usb->sector_size == USBMSC_SECTOR_SIZE,
                 "[STRICT] [USBMSC] with a capacity and 512-byte sectors");

    /*
     * The stick must not have taken the root. The boot path tries IDE, then AHCI
     * only if IDE found nothing, and that order is what made v1.5.0 additive
     * rather than a gamble - a third driver quietly becoming the root would undo
     * it, and the symptom would be a machine that boots from the wrong disk.
     */
    KTEST_ASSERT(blockdev_root() != usb,
                 "[STRICT] [USBMSC] and it is not the root, which the boot order decides");

    /*
     * One real transfer, end to end: a command wrapper out, a sector in, a
     * status wrapper back. Everything above this is arithmetic; this is the
     * assertion that says the transport works.
     */
    static uint8_t buf[512];

    KTEST_ASSERT(blockdev_read(usb, 0, buf) == E_OK,
                 "[STRICT] [USBMSC] a sector reads back through the whole transport");

    KTEST_ASSERT(blockdev_read(usb, usb->sector_count, buf) == E_INVAL,
                 "[STRICT] [USBMSC] and a sector past the end is refused before the device sees it");
}

/**
 * @brief The second stick, which is the whole reason this driver has a table.
 *
 * A machine that boots esdumanOS from a flash drive and keeps its file system on
 * another has two of these plugged in, and the first version of this driver
 * would have taken whichever enumerated first - the right disk about half the
 * time. Everything here is about the second one being a disk of its own rather
 * than a second view of the first.
 *
 * The capacities are the load-bearing part. Both sticks answer READ CAPACITY,
 * and if that answer were kept in a single static the second registration would
 * carry the first one's size: a disk that mounts, reads the sectors it has, and
 * returns nothing past an end it reports wrongly. The Makefile gives the two
 * images different sizes for this assertion and says so.
 */
static void run_second_disk_assertions(void) {
    blockdev_t *usb0 = blockdev_find("usb0");
    blockdev_t *usb1 = blockdev_find("usb1");

    KTEST_ASSERT(usbmsc_count() == 2 && usbmsc_count() <= USBMSC_MAX_DISKS,
                 "[STRICT] [USBMSC] both sticks on the bus were brought up, not just the first");

    if (usb0 == 0 || usb1 == 0) {
        KTEST_ASSERT(0, "[STRICT] [USBMSC] and each one registered under a name of its own");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] each sized from its own capacity rather than the first one's");
        KTEST_ASSERT(0, "[STRICT] [USBMSC] and the second one's transport carries a sector too");
        return;
    }

    KTEST_ASSERT(usb0 != usb1 && usb0->ctx != usb1->ctx && usb1->ctx != 0,
                 "[STRICT] [USBMSC] and each one registered under a name of its own");

    /*
     * Different images, so different answers. Equal counts here would mean the
     * driver asked twice and stored the answer once.
     */
    KTEST_ASSERT(usb0->sector_count != usb1->sector_count &&
                 usb1->sector_count > 0,
                 "[STRICT] [USBMSC] each sized from its own capacity rather than the first one's");

    /*
     * A real transfer on the second stick's own bulk endpoints. The controller
     * keeps its rings per device and always has; what this proves is that this
     * driver opened the second set rather than sending the second stick's
     * commands down the first stick's rings.
     */
    static uint8_t buf2[512];

    KTEST_ASSERT(blockdev_read(usb1, 0, buf2) == E_OK,
                 "[STRICT] [USBMSC] and the second one's transport carries a sector too");
}

void run_usbmsc_tests(void) {
    printk("\n--- USB Mass Storage Tests ---\n");

    run_layout_assertions();
    run_registration_assertions();
    run_second_disk_assertions();
}
