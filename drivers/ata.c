#include "ata.h"
#include "io.h"
#include "stdio.h"

static void ata_wait_bsy() {
    while (inb(ATA_PORT_STATUS) & ATA_SR_BSY);
}

static void ata_wait_drq() {
    while (!(inb(ATA_PORT_STATUS) & ATA_SR_DRQ));
}

void ata_read_sector(uint32_t lba, uint8_t *buffer) {
    outb(ATA_PORT_CONTROL, 0x02);
    ata_wait_bsy();
    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);

    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));

    outb(ATA_PORT_COMMAND, ATA_CMD_READ_PIO);
    
    ata_wait_bsy();
    ata_wait_drq();
    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(ATA_PORT_DATA);
    }
}

void ata_write_sector(uint32_t lba, uint8_t *buffer) {
    outb(0x3F6, 0x02);
    ata_wait_bsy();
    
    outb(ATA_PORT_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PORT_SECT_COUNT, 1);
    
    outb(ATA_PORT_LBA_LOW, (uint8_t) lba);
    outb(ATA_PORT_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PORT_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PORT_COMMAND, ATA_CMD_WRITE_PIO);
    
    ata_wait_bsy();

    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_PORT_DATA, ptr[i]);
    }
    
    outb(ATA_PORT_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
}