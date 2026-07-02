#include "ata.h"
#include "io.h"
#include "stdio.h"

static int ata_wait_bsy() {
    uint32_t timeout = 100000;
    while ((inb(ATA_PORT_STATUS) & ATA_SR_BSY) && --timeout);
    
    if (timeout == 0) {
        printk("\n[ATA_DRV] KRITIK HATA: BSY Timeout! (Disk yanit vermiyor)\n");
        return 0;
    }
    return 1;
}

static int ata_wait_drq() {
    uint32_t timeout = 100000;
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ) && --timeout);
    
    if (timeout == 0) {
        printk("\n[ATA_DRV] KRITIK HATA: DRQ Timeout! (Veri okumaya/yazmaya hazir degil)\n");
        return 0;
    }
    return 1;
}

uint32_t ata_identify(void) {
    outb(ATA_PORT_DRV_HEAD, 0xA0);
    outb(ATA_PORT_SECT_COUNT, 0);
    outb(ATA_PORT_LBA_LOW, 0);
    outb(ATA_PORT_LBA_MID, 0);
    outb(ATA_PORT_LBA_HIGH, 0);
    outb(ATA_PORT_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PORT_STATUS);
    if (status == 0) return 4096;

    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY);

    if (inb(ATA_PORT_LBA_MID) != 0 || inb(ATA_PORT_LBA_HIGH) != 0) return 4096;

    while (1) {
        status = inb(ATA_PORT_STATUS);
        if (status & ATA_SR_ERR) return 4096;
        if (status & ATA_SR_DRQ) break;
    }

    uint16_t buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PORT_DATA);
    }

    uint32_t total_sectors = (buffer[61] << 16) | buffer[60];
    
    if (total_sectors > 0) {
        printk("[ATA] Disk tanindi! Kapasite: %d Sektor (%d KB)\n", total_sectors, (total_sectors * 512) / 1024);
        return total_sectors;
    }
    
    return 4096;
}

void ata_read_sector(uint32_t lba, uint8_t *buffer) {
    outb(ATA_PORT_CONTROL, 0x02);

    if (!ata_wait_bsy()) return; 

    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);

    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    outb(ATA_PORT_COMMAND, ATA_CMD_READ_PIO);

    if (!ata_wait_bsy()) return;
    if (!ata_wait_drq()) return;
    
    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("[ATA_DRV] DONANIM HATASI: Disk okuma basarisiz (Sector: %d)!\n", lba);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PORT_DATA);
    }
}

void ata_write_sector(uint32_t lba, uint8_t *buffer) {
    outb(ATA_PORT_CONTROL, 0x02);
    
    if (!ata_wait_bsy()) return;
    
    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);
    
    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PORT_COMMAND, ATA_CMD_WRITE_PIO);
    
    if (!ata_wait_bsy()) return;
    if (!ata_wait_drq()) return;

    if (inb(ATA_PORT_STATUS) & ATA_SR_ERR) {
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("[ATA_DRV] DONANIM HATASI: Diske yazma basarisiz (Sector: %d)!\n", lba);
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        return;
    }
    
    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PORT_DATA, ptr[i]);
    }
    
    outb(ATA_PORT_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy(); 
}