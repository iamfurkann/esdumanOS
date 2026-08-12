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
#include "rtc.h"

static bcache_node_t cache[BCACHE_SIZE];
static uint32_t bcache_ticks = 0;

/*
 * Write-back bookkeeping.
 *
 * The cache was write-back with no policy at all: the only calls to
 * bcache_flush() were in sys_reboot() and sys_halt(), so a dirty sector reached
 * the platter either when an orderly shutdown happened to occur or when LRU
 * eviction happened to pick it. Anything else - a panic, a triple fault, the
 * power going away - lost it. With a 64-slot cache that is up to 32 KB of
 * filesystem, including /etc/shadow and the directory tables.
 *
 * Two independent bounds now apply, because either one alone leaves a hole:
 *
 *   By volume, BCACHE_DIRTY_HIGH_WATER caps how much unwritten data can
 *   accumulate. Without it a burst of writes puts the whole cache at risk.
 *
 *   By time, BCACHE_FLUSH_INTERVAL_TICKS caps how long any of it can sit there.
 *   Without it a single sector written once and never followed by another write
 *   stays in RAM indefinitely, which is the common case for a small edit.
 */
static uint32_t dirty_count = 0;
static uint32_t oldest_dirty_tick = 0;

/**
 * @brief Marks a slot dirty and starts the deadline if the cache was clean.
 */
static void mark_dirty(int slot) {
    if (cache[slot].is_dirty) return;

    cache[slot].is_dirty = 1;
    if (dirty_count == 0) oldest_dirty_tick = timer_get_ticks();
    dirty_count++;
}

/**
 * @brief Marks a slot clean after its contents have reached the disk.
 */
static void mark_clean(int slot) {
    if (!cache[slot].is_dirty) return;

    cache[slot].is_dirty = 0;
    if (dirty_count > 0) dirty_count--;
}

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
    dirty_count = 0;
    oldest_dirty_tick = 0;
    printk("[BCACHE] Block Cache System (Write-Back) initialized with 32KB.\n");
}

uint32_t bcache_dirty_count(void) {
    return dirty_count;
}

int bcache_flush_is_due(void) {
    if (dirty_count == 0) return 0;

    /*
     * Unsigned subtraction, so the tick counter wrapping does not postpone the
     * flush for the rest of the boot.
     */
    return (uint32_t)(timer_get_ticks() - oldest_dirty_tick) >= BCACHE_FLUSH_INTERVAL_TICKS;
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
        mark_clean(oldest_idx);
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
    cache[slot].last_access = bcache_ticks;

    /*
     * A no-op today - bcache_get_lru_slot() only ever returns a clean slot, and
     * nothing invalidates a slot after init. Routed through mark_clean() anyway
     * so that the dirty counter cannot silently drift out of step with the flags
     * if that ever stops being true.
     */
    mark_clean(slot);

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
    cache[slot].last_access = bcache_ticks;
    ft_memcpy(cache[slot].data, buffer, 512);
    mark_dirty(slot);

    /*
     * Write behind once too much is outstanding. Safe to do the I/O right here:
     * bcache_write_sector() is only ever reached from normal kernel context -
     * the VFS paths - never from an interrupt handler, and ata_write_sector()
     * waits on the ATA IRQ with hlt, which needs interrupts enabled.
     *
     * No recursion: bcache_flush() calls ata_write_sector() directly and never
     * comes back through here.
     */
    if (dirty_count >= BCACHE_DIRTY_HIGH_WATER) {
        bcache_flush();
    }
}

/**
 * @brief Flush all dirty cache slots to disk.
 */
void bcache_flush(void) {
    int flushed = 0;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].is_dirty) {
            ata_write_sector(cache[i].sector, cache[i].data);
            mark_clean(i);
            flushed++;
        }
    }
    if (flushed > 0) {
        klog_int(LOG_LEVEL_INFO, "BCACHE", "Dirty caches synchronized to disk (Flush)", flushed);
    }
}

/**
 * @brief Flushes only when the dirty data has been waiting long enough.
 *
 * Called from sys_yield(), which the idle task drives in a tight Ring 3 loop, so
 * on any system that ever goes idle the deadline is checked continuously. That
 * placement is forced rather than chosen: ata_wait_irq() waits for the ATA
 * interrupt with hlt, so the flush cannot run from the timer handler - with
 * interrupts masked that hlt would never return.
 *
 * LIMIT: a system that never idles and never yields will not reach this. The
 * volume bound in bcache_write_sector() still applies there, and a task busy
 * enough to starve the idle loop without making syscalls is not writing to the
 * disk either, so it creates no new dirty data. Explicit sync() covers the rest.
 */
void bcache_flush_if_due(void) {
    if (bcache_flush_is_due()) {
        bcache_flush();
    }
}