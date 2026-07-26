/*
 * File: bcache.c
 * Purpose: Block cache system implementation (Write-Back) for disk sector caching.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "bcache.h"
#include "ata.h"
#include "libft.h"
#include "stdio.h"
#include "klog.h"

static bcache_node_t cache[BCACHE_SIZE];
static uint32_t bcache_ticks = 0;

/**
 * @brief Initialize the block cache system.
 */
void bcache_init(void) {
    for (int i = 0; i < BCACHE_SIZE; i++) {
        cache[i].is_valid = 0;
        cache[i].is_dirty = 0;
        cache[i].last_access = 0;
        cache[i].sector = 0;
        ft_memset(cache[i].data, 0, 512);
    }
    printk("[BCACHE] Block Cache System (Write-Back) initialized with 32KB.\n");
}

/**
 * @brief Get the least recently used cache slot.
 * @return The index of the available or LRU cache slot.
 */
static int bcache_get_lru_slot(void) {
    int oldest_idx = 0;
    uint32_t oldest_time = 0xFFFFFFFF;

    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].is_valid) {
            return i;
        }
        if (cache[i].last_access < oldest_time) {
            oldest_time = cache[i].last_access;
            oldest_idx = i;
        }
    }

    if (cache[oldest_idx].is_valid && cache[oldest_idx].is_dirty) {
        ata_write_sector(cache[oldest_idx].sector, cache[oldest_idx].data);
        cache[oldest_idx].is_dirty = 0;
    }
    
    return oldest_idx;
}

/**
 * @brief Read a sector from the block cache or disk.
 * @param sector The sector number to read.
 * @param buffer The buffer to store the sector data.
 */
void bcache_read_sector(uint32_t sector, uint8_t *buffer) {
    bcache_ticks++;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].sector == sector) {
            ft_memcpy(buffer, cache[i].data, 512);
            cache[i].last_access = bcache_ticks;
            return;
        }
    }

    int slot = bcache_get_lru_slot();
    ata_read_sector(sector, cache[slot].data);
    
    cache[slot].sector = sector;
    cache[slot].is_valid = 1;
    cache[slot].is_dirty = 0;
    cache[slot].last_access = bcache_ticks;

    ft_memcpy(buffer, cache[slot].data, 512);
}

/**
 * @brief Write a sector to the block cache.
 * @param sector The sector number to write.
 * @param buffer The buffer containing the sector data.
 */
void bcache_write_sector(uint32_t sector, uint8_t *buffer) {
    bcache_ticks++;

    int slot = -1;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].sector == sector) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        slot = bcache_get_lru_slot();
    }

    cache[slot].sector = sector;
    cache[slot].is_valid = 1;
    cache[slot].is_dirty = 1;
    cache[slot].last_access = bcache_ticks;
    ft_memcpy(cache[slot].data, buffer, 512);
}

/**
 * @brief Flush all dirty cache slots to disk.
 */
void bcache_flush(void) {
    int flushed = 0;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].is_dirty) {
            ata_write_sector(cache[i].sector, cache[i].data);
            cache[i].is_dirty = 0;
            flushed++;
        }
    }
    if (flushed > 0) {
        klog_int(LOG_LEVEL_INFO, "BCACHE", "Dirty caches synchronized to disk (Flush)", flushed);
    }
}