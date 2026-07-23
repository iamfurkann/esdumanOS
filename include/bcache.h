#ifndef BCACHE_H
#define BCACHE_H

#include "types.h"

/**
 * @brief Maximum number of sectors that can be stored in the block cache.
 */
#define BCACHE_SIZE 64 

/**
 * @brief Represents a single cached block entry.
 * Contains the cached sector data along with metadata for cache eviction and synchronization.
 */
typedef struct {
    uint32_t sector;      /**< The logical sector number being cached. */
    uint8_t  data[512];   /**< The 512-byte data buffer for the sector. */
    uint32_t last_access; /**< Timestamp of the last access, used for LRU eviction. */
    uint8_t  is_valid;    /**< Flag indicating if this cache node contains valid sector data. */
    uint8_t  is_dirty;    /**< Flag indicating if the cached data has been modified and needs flushing. */
} bcache_node_t;

/**
 * @brief Initializes the block cache system.
 * Marks all cache nodes as invalid and clears data structures.
 */
void bcache_init(void);

/**
 * @brief Reads a sector from the block cache or loads it from the underlying device.
 * 
 * @param sector The logical sector number to read.
 * @param buffer The buffer where the sector data will be copied.
 */
void bcache_read_sector(uint32_t sector, uint8_t *buffer);

/**
 * @brief Writes data to a cached sector, marking it as dirty.
 * If the sector is not in cache, it will be loaded or allocated first.
 * 
 * @param sector The logical sector number to write.
 * @param buffer The buffer containing the data to write.
 */
void bcache_write_sector(uint32_t sector, uint8_t *buffer);

/**
 * @brief Flushes all dirty sectors in the block cache to the underlying physical storage.
 */
void bcache_flush(void);

#endif // BCACHE_H