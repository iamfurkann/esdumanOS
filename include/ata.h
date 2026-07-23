#ifndef ATA_H
#define ATA_H

#include "types.h"

/**
 * @brief ATA Port definitions for the Primary Bus.
 * These ports are used to communicate with the primary IDE controller.
 */
#define ATA_PORT_DATA       0x1F0
#define ATA_PORT_ERROR      0x1F1
#define ATA_PORT_SECT_COUNT 0x1F2
#define ATA_PORT_LBA_LOW    0x1F3
#define ATA_PORT_LBA_MID    0x1F4
#define ATA_PORT_LBA_HIGH   0x1F5
#define ATA_PORT_DRV_HEAD   0x1F6
#define ATA_PORT_COMMAND    0x1F7
#define ATA_PORT_STATUS     0x1F7

/**
 * @brief ATA PIO Read command.
 */
#define ATA_CMD_READ_PIO    0x20

/**
 * @brief ATA Device Control Port.
 */
#define ATA_PORT_CONTROL 0x3F6

/**
 * @brief ATA PIO Write command.
 */
#define ATA_CMD_WRITE_PIO   0x30 

/**
 * @brief ATA Cache Flush command.
 * Ensures that all cached data in the drive's internal buffer is written to the disk.
 */
#define ATA_CMD_CACHE_FLUSH 0xE7

/**
 * @brief ATA Status Register Flags.
 * ATA_SR_ERR: Error occurred.
 * ATA_SR_DRQ: Data Request Ready (Drive is ready to transfer data).
 * ATA_SR_BSY: Busy (Drive is preparing to send/receive data).
 */
#define ATA_SR_ERR          0x01
#define ATA_SR_DRQ          0x08
#define ATA_SR_BSY          0x80


/**
 * @brief ATA Identify Device command.
 * Requests device information (geometry, features) from the drive.
 */
#define ATA_CMD_IDENTIFY 0xEC

/**
 * @brief Reads a single 512-byte sector from the ATA drive using LBA28 addressing.
 * 
 * @param lba Logical Block Address of the sector to read.
 * @param buffer Pointer to the memory buffer where the sector data will be stored.
 * @return 0 on success, or a negative error code on failure.
 */
int ata_read_sector(uint32_t lba, uint8_t *buffer);

/**
 * @brief Writes a single 512-byte sector to the ATA drive using LBA28 addressing.
 * 
 * @param lba Logical Block Address of the sector to write.
 * @param buffer Pointer to the memory buffer containing the data to write.
 * @return 0 on success, or a negative error code on failure.
 */
int ata_write_sector(uint32_t lba, uint8_t *buffer);

#endif