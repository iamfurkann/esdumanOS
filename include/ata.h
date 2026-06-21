#ifndef ATA_H
#define ATA_H

#include "types.h"

/* ATA Portları (Primary Bus) */
#define ATA_PORT_DATA       0x1F0
#define ATA_PORT_ERROR      0x1F1
#define ATA_PORT_SECT_COUNT 0x1F2
#define ATA_PORT_LBA_LOW    0x1F3
#define ATA_PORT_LBA_MID    0x1F4
#define ATA_PORT_LBA_HIGH   0x1F5
#define ATA_PORT_DRV_HEAD   0x1F6
#define ATA_PORT_COMMAND    0x1F7
#define ATA_PORT_STATUS     0x1F7

/* ATA Komutları */
#define ATA_CMD_READ_PIO    0x20

/*ATA Cihaz Kontrol Portu*/
#define ATA_PORT_CONTROL 0x3F6
#define ATA_CMD_WRITE_PIO   0x30  /* YENİ: Diske yazma komutu */
#define ATA_CMD_CACHE_FLUSH 0xE7  /* YENİ: Diskin önbelleğini zorla yazdır */

/* ATA Durum (Status) Bayrakları */
#define ATA_SR_ERR          0x01    // Error
#define ATA_SR_DRQ          0x08    // Data Request Ready
#define ATA_SR_BSY          0x80    // Busy

void ata_read_sector(uint32_t lba, uint8_t *buffer);
void ata_write_sector(uint32_t lba, uint8_t *buffer);
#endif