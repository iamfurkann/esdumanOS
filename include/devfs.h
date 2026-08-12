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
 * @return The index of the device, or a NEGATIVE errno (E_NOENT) if not found.
 *         Test the result with < 0, never against -1: this function follows the
 *         kernel's errno convention and E_NOENT is -2.
 */
int get_device_idx(const char *name);

/**
 * @brief Counts the registered devices, excluding the terminating sentinel.
 *
 * @return Number of usable indices into dev_table.
 */
int get_device_count(void);

/**
 * @brief Checks that an index actually addresses a registered device.
 *
 * @param idx Candidate index.
 * @return 1 when idx addresses a registered device, 0 otherwise.
 */
int dev_index_is_valid(int idx);


// --- Added by Refactor Script 2 ---
extern int dev_null_read(uint8_t *buf, int size);

/**
 * @brief Reads from /dev/random: ChaCha20 output keyed from the entropy pool.
 *
 * Never blocks. Refuses with E_IO only when the pool is unseeded; a WEAK pool is
 * served with a once-per-boot kernel warning. See the rationale in devfs.c.
 *
 * @return Bytes produced (always size, when it succeeds), or a negative errno.
 */
extern int dev_random_read(uint8_t *buf, int size);

/**
 * @brief Reads from /dev/urandom: the same generator, asked for by its
 *        best-effort name, so a WEAK pool is not reported as a mismatch.
 *
 * @return Bytes produced (always size, when it succeeds), or a negative errno.
 */
extern int dev_urandom_read(uint8_t *buf, int size);

#endif // DEVFS_H