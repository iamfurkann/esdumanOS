/*
 * File: bcache.c
 * Purpose: Block cache system implementation (Write-Back) for disk sector caching.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "bcache.h"
#include "blockdev.h"
#include "errno.h"
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
 * Two independent flush triggers apply when device I/O succeeds:
 *
 *   By volume, BCACHE_DIRTY_HIGH_WATER requests write-back. On failure,
 *   accepted data remains dirty and may occupy all BCACHE_SIZE slots.
 *
 *   By time, BCACHE_FLUSH_INTERVAL_TICKS requests write-back on a later yield.
 *   Without it a single sector written once and never followed by another write
 *   stays in RAM indefinitely, which is the common case for a small edit.
 */
static uint32_t dirty_count = 0;
static uint32_t oldest_dirty_tick = 0;

/* Defined below; bcache_write_sector() reaches the high-water path before it. */
static int bcache_flush_reporting(int log_level);

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
        cache[i].dev = 0;
        ft_memset(cache[i].data, 0, 512);
    }
    bcache_ticks = 0;
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
 * @brief Find a reusable slot without losing a failed write.
 *
 * Prefer empty slots, then the oldest clean slot. If all slots are dirty, try
 * them in LRU order, at most once each. One failing device must not block a
 * healthy device's slot, and a failure must never make accepted data disposable.
 */
static int bcache_get_lru_slot(void) {
    int clean = -1;
    uint32_t clean_age = 0;
    uint8_t tried[BCACHE_SIZE] = {0};

    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].is_valid) return i;
        uint32_t age = bcache_ticks - cache[i].last_access;
        if (!cache[i].is_dirty && (clean < 0 || age > clean_age)) {
            clean = i;
            clean_age = age;
        }
    }
    if (clean >= 0) return clean;

    for (int attempt = 0; attempt < BCACHE_SIZE; attempt++) {
        int oldest = -1;
        uint32_t oldest_age = 0;
        for (int i = 0; i < BCACHE_SIZE; i++) {
            uint32_t age = bcache_ticks - cache[i].last_access;
            if (!tried[i] && (oldest < 0 || age > oldest_age)) {
                oldest = i;
                oldest_age = age;
            }
        }
        if (oldest < 0) break;
        tried[oldest] = 1;

        if (blockdev_write(cache[oldest].dev, cache[oldest].sector,
                           cache[oldest].data) == E_OK) {
            mark_clean(oldest);
            return oldest;
        }
    }

    klog(LOG_LEVEL_ERROR, "BCACHE", "No writable slot; all accepted data kept dirty.");
    return -1;
}

/**
 * @brief Read a sector from the block cache or disk.
 *
 * Returns a status as of v1.2.0, and the reason is the whole of this release. It
 * used to return void, call ata_read_sector() and discard the result. The driver
 * zeroes its buffer on failure, so a disk that could not be read produced 512
 * zero bytes, and those went into the cache marked valid. Nothing above could
 * tell them from a sector that really was zero - and one of the callers is the
 * check that decides whether a disk is blank and should be formatted.
 *
 * A failed read now leaves nothing behind: the slot is not marked valid, so the
 * next attempt goes back to the device rather than being served a cached lie.
 *
 * @param sector The sector number to read.
 * @param buffer The buffer to store the sector data.
 * @return E_OK, or a negative errno from the device.
 */
int bcache_read_sector(blockdev_t *dev, uint32_t sector, uint8_t *buffer) {
    int check = blockdev_check_read(dev, sector, buffer);
    if (check != E_OK) return check;
    if (dev->sector_size != sizeof(cache[0].data)) return E_INVAL;

    bcache_ticks++;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].dev == dev && cache[i].sector == sector) {
            ft_memcpy(buffer, cache[i].data, 512);
            cache[i].last_access = bcache_ticks;
            return E_OK;
        }
    }

    int slot = bcache_get_lru_slot();
    if (slot < 0) return E_IO;

    int res = blockdev_read(dev, sector, cache[slot].data);
    if (res != E_OK) {
        /*
         * Not cached, and the caller's buffer is zeroed rather than left holding
         * whatever it had. The status is what says which of those it is.
         */
        cache[slot].is_valid = 0;
        ft_memset(buffer, 0, 512);
        return res;
    }

    cache[slot].dev = dev;
    cache[slot].sector = sector;
    cache[slot].is_valid = 1;
    cache[slot].last_access = bcache_ticks;

    /*
     * A no-op today - bcache_get_lru_slot() only ever returns a clean slot, and
     * failed reads and per-device flushes invalidate only clean slots. Routed
     * through mark_clean() anyway
     * so that the dirty counter cannot silently drift out of step with the flags
     * if that ever stops being true.
     */
    mark_clean(slot);

    ft_memcpy(buffer, cache[slot].data, 512);
    return E_OK;
}

/**
 * @brief Write a sector to the block cache.
 *
 * The write itself is deferred - this is a write-back cache - so E_OK here means
 * the sector was taken, not that it reached the disk. Invalid requests are
 * rejected before cache lookup. E_IO means every slot held dirty data that
 * could not be written; bcache_flush() reports deferred device failures.
 *
 * @param sector The sector number to write.
 * @param buffer The buffer containing the sector data.
 * @return E_OK, a request-validation errno, or E_IO when no slot could be freed.
 */
int bcache_write_sector(blockdev_t *dev, uint32_t sector, uint8_t *buffer) {
    int check = blockdev_check_write(dev, sector, buffer);
    if (check != E_OK) return check;
    if (dev->sector_size != sizeof(cache[0].data)) return E_INVAL;

    bcache_ticks++;

    int slot = -1;
    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].dev == dev && cache[i].sector == sector) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        slot = bcache_get_lru_slot();
        if (slot < 0) return E_IO;
    }

    cache[slot].dev = dev;
    cache[slot].sector = sector;
    cache[slot].is_valid = 1;
    cache[slot].last_access = bcache_ticks;
    ft_memcpy(cache[slot].data, buffer, 512);
    mark_dirty(slot);

    /*
     * Write behind once too much is outstanding. Safe to do the I/O right here:
     * bcache_write_sector() is only ever reached from normal kernel context -
     * the VFS paths - never from an interrupt handler, and the device underneath
     * may block. The ATA driver does: it waits on its IRQ with hlt, which needs
     * interrupts enabled. That constraint belongs to whatever is registered, and
     * this cache has no way to check it, so the rule is that a block device's
     * handlers are called from ordinary kernel context and nowhere else.
     *
     * No recursion: bcache_flush() goes straight to the device and never comes
     * back through here.
     */
    if (dirty_count >= BCACHE_DIRTY_HIGH_WATER) {
        bcache_flush_reporting(LOG_LEVEL_DEBUG);   /* policy, not a request */
    }

    return E_OK;
}

/**
 * @brief Flushes every dirty slot, reporting at the given log level.
 *
 * The level is a parameter because the same work means different things
 * depending on who asked. A flush the write-back policy performed on its own is
 * routine housekeeping and should not announce itself - once the five-second
 * deadline existed, an INFO line here meant a message on the console every five
 * seconds for as long as anything was being written. A flush somebody asked for,
 * or one taken on the way down, is worth a line.
 *
 * @param log_level Level to report the flushed count at; DEBUG is suppressed.
 */
static int bcache_flush_reporting(int log_level) {
    int flushed = 0;
    int failed = 0;

    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (cache[i].is_valid && cache[i].is_dirty) {
            /* Every accepted sector survives a failed write, regardless of
             * errno. E_NODEV can mean an unplugged device, and E_ROFS can mean
             * media that became write-protected after cache admission. Neither
             * is evidence that the caller's data may be discarded. */
            int res = blockdev_write(cache[i].dev, cache[i].sector, cache[i].data);

            if (res != E_OK) {
                failed++;
                continue;
            }
            mark_clean(i);
            flushed++;
        }
    }

    if (flushed > 0) {
        klog_int(log_level, "BCACHE", "Dirty caches synchronized to disk (Flush)", flushed);
    }
    if (failed > 0) {
        klog_int(LOG_LEVEL_ERROR, "BCACHE", "Sectors the disk would not take", failed);
        return E_IO;
    }
    return E_OK;
}

/**
 * @brief Flush all dirty cache slots to disk.
 *
 * The explicit path: sync(), reboot and halt. Reports what it wrote.
 */
int bcache_flush(void) {
    return bcache_flush_reporting(LOG_LEVEL_INFO);
}

int bcache_flush_dev(blockdev_t *dev) {
    int failed = 0;

    if (dev == 0) return E_NODEV;

    for (int i = 0; i < BCACHE_SIZE; i++) {
        if (!cache[i].is_valid || cache[i].dev != dev) continue;

        if (cache[i].is_dirty) {
            int res = blockdev_write(cache[i].dev, cache[i].sector, cache[i].data);

            if (res != E_OK) {
                /* Failed unmount must retain both the bytes and their device. */
                failed++;
                continue;
            }
            mark_clean(i);
        }

        /*
         * Dropped as well as written, and that is what makes this different
         * from flushing everything. A device that has been unmounted may be
         * unplugged; a cached sector of a disk that is no longer there is a
         * sector nothing can reconcile, and a later read of a *different* disk
         * that happens to reuse the pointer would find it.
         */
        cache[i].is_valid = 0;
        cache[i].dev = 0;
    }

    if (failed > 0) {
        klog_int(LOG_LEVEL_ERROR, "BCACHE",
                 "Sectors the disk would not take on unmount", failed);
        return E_IO;
    }
    return E_OK;
}

/**
 * @brief Flushes only when the dirty data has been waiting long enough.
 *
 * Called from sys_yield(), which the idle task drives in a tight Ring 3 loop, so
 * on any system that ever goes idle the deadline is checked continuously. That
 * placement is forced rather than chosen: the ATA driver waits for its interrupt
 * with hlt, so the flush cannot run from the timer handler - with interrupts
 * masked that hlt would never return. Any device that can block has the same
 * requirement, which is why it is stated as a rule about block devices rather
 * than a fact about ATA.
 *
 * LIMIT: a system that never idles and never yields will not reach this. The
 * volume bound in bcache_write_sector() still applies there, and a task busy
 * enough to starve the idle loop without making syscalls is not writing to the
 * disk either, so it creates no new dirty data. Explicit sync() covers the rest.
 */
void bcache_flush_if_due(void) {
    if (bcache_flush_is_due()) {
        bcache_flush_reporting(LOG_LEVEL_DEBUG);   /* policy, not a request */
    }
}
