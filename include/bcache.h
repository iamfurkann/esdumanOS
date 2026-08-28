#ifndef BCACHE_H
#define BCACHE_H

#include "types.h"
#include "rtc.h"

/**
 * @brief Maximum number of sectors that can be stored in the block cache.
 */
#define BCACHE_SIZE 64

/**
 * @brief Dirty slots that force a write-behind flush.
 *
 * Half the cache. Bounds how much unwritten filesystem a crash can take with it
 * by volume, and leaves the other half free so that a flush is not triggered on
 * nearly every write once the cache is warm.
 */
#define BCACHE_DIRTY_HIGH_WATER (BCACHE_SIZE / 2)

/**
 * @brief How long dirty data may sit in the cache, in timer ticks.
 *
 * Five seconds, the same order as Linux's dirty_writeback_centisecs default. This
 * is the bound that matters for a single small edit, which the volume bound never
 * reaches. Derived from TIMER_HZ rather than written out as a tick count, so it
 * stays five seconds if the PIT rate is ever changed.
 */
#define BCACHE_FLUSH_INTERVAL_TICKS (TIMER_HZ * 5)

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

/*
 * These three returned void until v1.2.0, so the file system had no way to learn
 * that the disk had failed - and the driver zero-fills its buffer on failure, so
 * a failed read arrived as a sector full of zeros that nothing could question.
 * One of the callers is the check that decides whether a disk is blank and
 * should therefore be formatted.
 */

/**
 * @brief Reads a sector from the block cache or loads it from the underlying device.
 *
 * A failed read is not cached: the slot stays invalid so the next attempt reaches
 * the device again, and the caller's buffer is zeroed. Zeros are not evidence of
 * anything; the return value is.
 *
 * @param sector The logical sector number to read.
 * @param buffer The buffer where the sector data will be copied.
 * @return E_OK, or a negative errno from the device.
 */
int bcache_read_sector(uint32_t sector, uint8_t *buffer);

/**
 * @brief Writes data to a cached sector, marking it as dirty.
 * If the sector is not in cache, it will be loaded or allocated first.
 *
 * E_OK means the cache took the sector, not that the disk did - this is a
 * write-back cache and the device is written later. bcache_flush() is where a
 * device write failure surfaces. The one failure this can report is a cache with
 * no slot to give: every slot holding dirty data the device would not accept.
 *
 * @param sector The logical sector number to write.
 * @param buffer The buffer containing the data to write.
 * @return E_OK, or E_IO when no slot could be freed.
 */
int bcache_write_sector(uint32_t sector, uint8_t *buffer);

/**
 * @brief Flushes all dirty sectors in the block cache to the underlying physical storage.
 *
 * A sector the device refuses stays dirty and stays counted, so a later flush
 * tries it again. Marking it clean would report success over data that never
 * left RAM and guarantee nothing ever retried it.
 *
 * @return E_OK when everything dirty reached the device, E_IO when any sector
 *         did not.
 */
int bcache_flush(void);

/**
 * @brief Whether dirty data has been waiting past BCACHE_FLUSH_INTERVAL_TICKS.
 *
 * Side-effect free, so the flush policy can be asserted without performing any
 * disk I/O.
 *
 * @return 1 when a time-based flush is owed, 0 when there is nothing dirty or
 *         the deadline has not passed.
 */
int bcache_flush_is_due(void);

/**
 * @brief Flushes only if bcache_flush_is_due().
 *
 * Must be called from normal kernel context with interrupts enabled - never from
 * an interrupt handler, because the ATA driver waits for its IRQ with hlt.
 */
void bcache_flush_if_due(void);

/**
 * @brief Number of cached sectors modified but not yet written to disk.
 * @return The current dirty slot count, 0 to BCACHE_SIZE.
 */
uint32_t bcache_dirty_count(void);


// --- Added by Refactor Script 2 ---
extern uint32_t fs_max_sectors;

#endif // BCACHE_H