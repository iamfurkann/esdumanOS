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
#define ATA_TIMEOUT_MS 200

volatile int ata_interrupt_fired = 0;
static uint32_t ata_total_sectors = 0;

/**
 * @brief Handles the ATA interrupt request.
 */
void ata_irq_handler(void) {
    ata_interrupt_fired = 1;
    inb(ATA_PORT_STATUS); 
}

/**
 * @brief Waits for an ATA interrupt to fire.
 * @return 1 on success, 0 on timeout.
 */
static int ata_wait_irq(void) {
    uint32_t start_time = timer_get_ticks();
    
    while (!ata_interrupt_fired) {
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
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
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
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
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
            klog(LOG_LEVEL_ERROR, "ATA", "DRQ Timeout! (Not ready for data transfer)");
            return 0;
        }
        asm volatile("pause");
    }
    return 1;
}

/**
 * @brief Identifies the ATA device and retrieves its parameters.
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

    // [PATCH 2]: We must wait for the BSY (Busy) bit to clear immediately
    // after the IDENTIFY command. QEMU can sometimes get stuck here.
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY) {
        // Stay in the loop, wait for busy state to end
    }

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
        
        if ((timer_get_ticks() - start_time) > ATA_TIMEOUT_MS) {
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
        return total_sectors;
    }
    
    return 0;
}

/**
 * @brief Reads a sector from the ATA disk.
 * @param lba Logical Block Address to read from.
 * @param buffer Pointer to the buffer where data will be stored.
 * @return 1 on success, 0 on error.
 */
int ata_read_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk not initialized or recognized! Read rejected.");
        return 0;
    }
    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] ERROR: LBA Limit Exceeded! (Requested: %d, Max: %d)\n", lba, ata_total_sectors - 1);
        return 0;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sector read started. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);

    if (!ata_wait_bsy()) {
ft_memset(buffer, 0, 512);
        return 0; 
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
        return 0;
    }
    
    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Hardware Error: Disk read failed! Sector", lba);
ft_memset(buffer, 0, 512);
        return 0;
    }
    
    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PORT_DATA);
    }
    
    return 1;
}

/**
 * @brief Writes a sector to the ATA disk.
 * @param lba Logical Block Address to write to.
 * @param buffer Pointer to the buffer containing data to write.
 * @return 1 on success, 0 on error.
 */
int ata_write_sector(uint32_t lba, uint8_t *buffer) {
    if (ata_total_sectors == 0) {
        klog(LOG_LEVEL_ERROR, "ATA", "Disk not initialized or recognized! Write rejected.");
        return 0;
    }

    if (lba >= ata_total_sectors) {
        printk("[ATA_DRV] ERROR: LBA Limit Exceeded! Write Cancelled (Requested: %d, Max: %d)\n", lba, ata_total_sectors - 1);
        return 0;
    }

    klog_int(LOG_LEVEL_DEBUG, "ATA", "Disk sector write started. LBA", lba);

    outb(ATA_PORT_CONTROL, 0x00);
    
    if (!ata_wait_bsy()) return 0;
    
    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);
    
    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    outb(ATA_PORT_COMMAND, ATA_CMD_WRITE_PIO);
    if (!ata_wait_bsy()) return 0;
    if (!ata_wait_drq()) return 0;

    uint16_t *ptr = (uint16_t *)buffer;
    asm volatile("cli");
    ata_interrupt_fired = 0;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PORT_DATA, ptr[i]);
    }
    asm volatile("sti");

    if (!ata_wait_irq()) return 0;

    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        klog_int(LOG_LEVEL_ERROR, "ATA", "Hardware Error: Disk write failed! Sector", lba);
        return 0;
    }

    outb(ATA_PORT_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy(); 
    
    ata_interrupt_fired = 0;
    inb(ATA_PORT_STATUS);

    return 1;
}