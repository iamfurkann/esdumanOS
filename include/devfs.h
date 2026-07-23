#ifndef DEVFS_H
#define DEVFS_H

#include "types.h"

/**
 * @brief Device read function pointer type.
 * Defines the signature for character or block device read operations.
 * 
 * @param buf Pointer to the destination buffer.
 * @param size Number of bytes to read.
 * @return Number of bytes read, or a negative error code.
 */
typedef int (*dev_read_fn)(uint8_t *buf, int size);

/**
 * @brief Device write function pointer type.
 * Defines the signature for character or block device write operations.
 * 
 * @param buf Pointer to the source buffer.
 * @param size Number of bytes to write.
 * @return Number of bytes written, or a negative error code.
 */
typedef int (*dev_write_fn)(const uint8_t *buf, int size);

/**
 * @brief Virtual device node structure.
 * Represents a device in the Device File System (DevFS), binding an explicit name 
 * to device-specific read/write handler functions.
 */
typedef struct {
    char name[32];        /**< Name of the device (e.g., "urandom", "null"). */
    dev_read_fn read;     /**< Pointer to the device's read handler function. */
    dev_write_fn write;   /**< Pointer to the device's write handler function. */
} device_node_t;

/**
 * @brief Global table of registered devices in the system.
 */
extern device_node_t dev_table[];

/**
 * @brief Looks up a device by its name in the global device table.
 * 
 * @param name The name of the device to search for.
 * @return The index of the device in the device table, or -1 if not found.
 */
int get_device_idx(const char *name);

#endif // DEVFS_H