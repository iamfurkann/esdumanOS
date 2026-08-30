/*
 * File: ata.c
 * Purpose: ATA disk driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ata.h"
#include "io.h"
#include "stdio.h"
#include "klog.h"
#include "libft.h"
#include "rtc.h"
#include "entropy.h"
#include "errno.h"
#include "blockdev.h"
/*
 * The wait budget, in timer ticks.
 *
 * It was called ATA_TIMEOUT_MS and it was never milliseconds: every use compares
 * it against a difference of timer_get_ticks(), and the PIT runs at TIMER_HZ,
 * which is 100. The budget has always been two seconds. Renaming it rather than
 * rescaling it keeps the behaviour every existing release was tested with and
 * stops the next reader from budgeting in the wrong unit.
 */
#define ATA_TIMEOUT_TICKS 200

volatile int ata_interrupt_fired = 0;
static uint32_t ata_total_sectors = 0;

/**
 * @brief Handles the ATA interrupt request.
 */
void ata_irq_handler(void) {
    ata_interrupt_fired = 1;

    /*
     * Command completion timing is aperiodic, so unlike the PIT it earns entropy
     * credit - but only a couple of bits per event, because on an emulated disk
     * most of the apparent variation comes from the emulator rather than from
     * physical seek and rotation.
     */
    entropy_add_event(ENTROPY_SRC_ATA, 0);

    inb(ATA_PORT_STATUS);
}

/**
 * @brief Waits for an ATA interrupt to fire.
 * @return 1 on success, 0 on timeout.
 */
static int ata_wait_irq(void) {
    uint32_t start_time = timer_get_ticks();
    
    while (!ata_interrupt_fired) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "ATA", "IRQ Timeout! Disk not responding.");
            return 0;
        }
        // [FIX]: We only use hlt. Removed 'cli' which disables interrupts.
        // The timer interrupt will wake the CPU every 10ms to check the loop.
        asm volatile("hlt"); 
    }
    ata_interrupt_fired = 0;
    return 1;
}

/**
 * @brief Waits for the ATA busy (BSY) bit to clear.
 * @return 1 on success, 0 on timeout.
 */
static int ata_wait_bsy() {
    uint32_t start_time = timer_get_ticks();
    
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "ATA", "BSY Timeout! Disk did not exit busy state.");
            return 0;
        }
        asm volatile("pause"); 
    }
    return 1;
}

/**
 * @brief Waits for the ATA data request (DRQ) bit to set.
 * @return 1 on success, 0 on timeout.
 */
static int ata_wait_drq() {
    uint32_t start_time = timer_get_ticks();
    
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ)) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "ATA", "DRQ Timeout! (Not ready for data transfer)");
            return 0;
        }
        asm volatile("pause");
    }
    return 1;
}

/*
 * The block-layer face of this driver.
 *
 * Thin on purpose: the LBA has already been range-checked by blockdev_read() and
 * blockdev_write() against sector_count, so these do not repeat it. The driver's
 * own checks stay because ata_read_sector() is still callable directly, and a
 * bounds check that only exists one layer up is one a direct caller does not get.
 *
 * The const is dropped on the write path because ata_write_sector() takes a
 * mutable pointer it does not actually write through. Fixing that signature
 * would be a change to a function every caller in the tree already agrees on,
 * for no behavioural gain; it is noted here instead.
 */
static int ata_bd_read(void *ctx, uint32_t lba, uint8_t *buf) {
    (void)ctx;
    return ata_read_sector(lba, buf);
}

static int ata_bd_write(void *ctx, uint32_t lba, const uint8_t *buf) {
    (void)ctx;
    return ata_write_sector(lba, (uint8_t *)buf);
}

static blockdev_t ata_blockdev = {
    .name         = "ata0",
    .sector_size  = 512,
    .sector_count = 0,      /* filled by ata_identify() */
    .read         = ata_bd_read,
    .write        = ata_bd_write,
    .ctx          = 0
};

/**
 * @brief Identifies the ATA device and retrieves its parameters.
 *
 * Also registers the disk as the root block device once its capacity is known,
 * which is what lets the file system stop naming this driver.
 *
 * Every way out of here that is not a disk is a return of 0, including the two
 * that mean nothing is listening: a controller with no drive on it, and no
 * controller at all. Nothing registers, and init_fs() then refuses to mount
 * rather than formatting what it cannot read.
 *
 * @return Total sectors of the disk, or 0 if initialization fails.
 */
uint32_t ata_identify(void) {
    // 1. Select drive (Drive 0 / Master)
    outb(ATA_PORT_DRV_HEAD, 0xA0);
    
    // [PATCH 1]: After drive selection, a short delay is required for the hardware
    // to settle (critical for QEMU/Bochs emulators)
    for (int i = 0; i < 1000; i++) {
        inb(ATA_PORT_STATUS); 
    }

    outb(ATA_PORT_SECT_COUNT, 0);
    outb(ATA_PORT_LBA_LOW, 0);
    outb(ATA_PORT_LBA_MID, 0);
    outb(ATA_PORT_LBA_HIGH, 0);
    
    // 2. Send IDENTIFY Command (0xEC)
    outb(ATA_PORT_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PORT_STATUS);

    // If status is 0, no disk is connected to this port.
    if (status == 0) return 0;

    /*
     * And if it is all ones, there is no controller here at all.
     *
     * An x86 port nobody decodes reads back 0xFF. The check above catches the
     * other shape of absence - a controller that answers with a status of zero
     * because no drive is attached to it, which is what QEMU's i440fx does when
     * the machine is started without a -drive - but it cannot catch this one,
     * because 0xFF is not 0.
     *
     * That mattered more than a missing branch usually does. The wait below used
     * to be written out by hand with no timeout, and BSY is bit 7, so it is set
     * in 0xFF and stays set forever: on a machine with no IDE controller this
     * function did not fail, it stopped, before init_fs() and before anything
     * reached the screen. A modern board is exactly that machine - q35 has no
     * legacy IDE at 0x1F0 - so the failure was reserved for the hardware this
     * project is aiming at.
     */
    if (status == 0xFF) {
        klog(LOG_LEVEL_INFO, "ATA",
             "No IDE controller answered on the primary bus.");
        return 0;
    }

    /*
     * [PATCH 2]: the BSY (Busy) bit has to clear before the result of IDENTIFY
     * can be read.
     *
     * This calls ata_wait_bsy() rather than spinning inline. The helper is
     * ninety lines above, it has always had the timeout and the log line, and
     * every other wait in this driver goes through it - this one call site was
     * written out by hand and got neither.
     */
    if (!ata_wait_bsy()) return 0;

    // If the disk is ATAPI (CD-ROM etc.), we cannot read capacity, reject it.
    if (inb(ATA_PORT_LBA_MID) != 0 || inb(ATA_PORT_LBA_HIGH) != 0) return 0;

    // 3. Wait for DRQ (Data Ready) or ERR (Error) bit
    uint32_t start_time = timer_get_ticks();
    while (1) {
        status = inb(ATA_PORT_STATUS);
        if (status & ATA_SR_ERR) {
            klog(LOG_LEVEL_ERROR, "ATA", "IDENTIFY command rejected by disk (ERR Bit).");
            return 0;
        }
        if (status & ATA_SR_DRQ) break; // Ready to read data
        
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_TICKS) {
            klog(LOG_LEVEL_ERROR, "ATA", "DRQ Timeout during IDENTIFY.");
            return 0;
        }
    }

    // 4. Read device information (256 16-bit words)
    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PORT_DATA);
    }

    // 5. Calculate capacity (words 60 and 61 for LBA28)
    uint32_t total_sectors = (buffer[61] << 16) | buffer[60];
    if (total_sectors > 0) {
        printk("[ATA] Disk recognized! Capacity: %d Sectors (%d KB) [IRQ Mode Active]\n",
               total_sectors, (total_sectors * 512) / 1024);
        ata_total_sectors = total_sectors;

        /*
         * Registering here rather than in a separate init step, because this is
         * the moment the capacity is known and a device whose capacity is
         * unknown is one blockdev_register() refuses.
         */
        ata_blockdev.sector_count = total_sectors;
        blockdev_register(&ata_blockdev);

        return total_sectors;
    }

    return 0;
}

/**
 * @brief Reads a sector from the ATA disk.
 *
 * Returns E_OK or a negative errno as of v1.2.0. It used to return 1 for success
 * and 0 for failure, while the declaration in ata.h promised "0 on success, or a
 * negative error code on failure" - so the header and the code disagreed about
 * what zero meant, in the direction that turns every failure into a success. No
 * caller was reading the value, which is why nothing caught it and why it
 * mattered: the block cache stored the zero-filled buffer as though it were data.
 *
 * The buffer is still zeroed on failure. Leaving it would hand the caller
 * whatever was on the stack, which is worse than zeros; the fix is that the
 * return value now says so.
 *
 * @param lba Logical Block Address to read from.
 * @param buffer Pointer to the buffer where data will be stored.
 * @return E_OK, or E_NODEV / E_INVAL / E_IO.
 */
int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk not initialized or recognized! Read rejected.");
        return E_NODEV;
    }
    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] ERROR: LBA Limit Exceeded! (Requested: %d, Max: %d)\n", lba, ata_total_sectors - 1);
        return E_INVAL;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sector read started. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);

    if (!ata_wait_bsy()) {
        ft_memset(buffer, 0, 512);
        return E_IO;
    }

    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);

    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    asm volatile("cli");
    ata_interrupt_fired = 0;
    outb(ATA_PORT_COMMAND, ATA_CMD_READ_PIO);
    asm volatile("sti");

    if (!ata_wait_irq()) {
        ft_memset(buffer, 0, 512);
        return E_IO;
    }
    
    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Hardware Error: Disk read failed! Sector", lba);
        ft_memset(buffer, 0, 512);
        return E_IO;
    }

    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PORT_DATA);
    }

    return E_OK;
}

/**
 * @brief Writes a sector to the ATA disk.
 *
 * E_OK or a negative errno, matching ata_read_sector(). This one's own comment
 * used to say "1 on success, 0 on error", which was true of the code and the
 * opposite of what ata.h said about its twin - two documents in the same tree
 * disagreeing, with the wrong one being the public header.
 *
 * @param lba Logical Block Address to write to.
 * @param buffer Pointer to the buffer containing data to write.
 * @return E_OK, or E_NODEV / E_INVAL / E_IO.
 */
int ata_write_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk not initialized or recognized! Write rejected.");
        return E_NODEV;
    }

    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] ERROR: LBA Limit Exceeded! Write Cancelled (Requested: %d, Max: %d)\n", lba, ata_total_sectors - 1);
        return E_INVAL;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sector write started. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);
    
    if (!ata_wait_bsy()) return E_IO;

    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);
    
    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    outb(ATA_PORT_COMMAND, ATA_CMD_WRITE_PIO);
    if (!ata_wait_bsy()) return E_IO;
    if (!ata_wait_drq()) return E_IO;

    uint16_t *ptr = (uint16_t *)buffer;
    asm volatile("cli");
    ata_interrupt_fired = 0;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PORT_DATA, ptr[i]);
    }
    asm volatile("sti");

    if (!ata_wait_irq()) return E_IO;

    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Hardware Error: Disk write failed! Sector", lba);
        return E_IO;
    }

    outb(ATA_PORT_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy(); 
    
    ata_interrupt_fired = 0;
    inb(ATA_PORT_STATUS);

    return E_OK;
}