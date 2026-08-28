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

/*
 * This header described the wrong contract from v0.4 until v1.2.0.
 *
 * Both functions below were documented as returning "0 on success, or a negative
 * error code on failure". They returned 1 for success and 0 for failure, and
 * never a negative anything - so the header and the code disagreed about the
 * meaning of zero, in the direction where every failure reads as success. It
 * survived because no caller read the value at all: the block cache called both
 * and discarded the result, so a failed read reached the file system as 512 zero
 * bytes indistinguishable from a sector that really was zero.
 *
 * The contract below is now the kernel's ordinary one - E_OK, or a negative
 * errno - and the code was changed to match it rather than the other way round,
 * because "success is zero" is what every other function in this tree means.
 */

/**
 * @brief Reads a single 512-byte sector from the ATA drive using LBA28 addressing.
 *
 * The buffer is zeroed on every failure path. That is deliberate - handing back
 * uninitialised stack contents would be worse - and it is exactly why the return
 * value has to be read: zeros are not evidence of anything.
 *
 * @param lba Logical Block Address of the sector to read.
 * @param buffer Pointer to the memory buffer where the sector data will be stored.
 * @return E_OK, E_NODEV when no disk was identified, E_INVAL when the LBA is past
 *         the end of it, or E_IO when the drive reported an error or stopped
 *         answering.
 */
int ata_read_sector(uint32_t lba, uint8_t *buffer);

/**
 * @brief Writes a single 512-byte sector to the ATA drive using LBA28 addressing.
 *
 * @param lba Logical Block Address of the sector to write.
 * @param buffer Pointer to the memory buffer containing the data to write.
 * @return E_OK, or E_NODEV / E_INVAL / E_IO as for ata_read_sector().
 */
int ata_write_sector(uint32_t lba, uint8_t *buffer);


/**
 * @brief Identifies the disk and registers it as the root block device.
 *
 * @return Total sectors, or 0 when no usable disk answered.
 */
extern uint32_t ata_identify(void);

#endif