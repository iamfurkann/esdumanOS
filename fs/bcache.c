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
 * @brief Whether a refused write could ever be accepted if it were tried again.
 *
 * Everything blockdev_write() rejects before the driver sees it describes the
 * *request* rather than the device's mood: a sector past the end of a disk is
 * past the end at every later attempt, and a device with no write handler will
 * not grow one. Keeping such a sector dirty is not persistence - it is a slot
 * that can never be freed, and because a slot nothing can write is also never
 * touched again, it becomes the least recently used one and every eviction from
 * then on picks it, fails, and hands back -1. One sector the device would never
 * have taken stops the whole cache from giving out slots.
 *
 * The default is to keep, not to drop. An unrecognised code is more likely a
 * device having a bad moment than a request that is malformed, and writing a
 * sector late is better than losing it. The four listed here are the ones this
 * tree actually produces, and each of them answers the same request the same way
 * however many times it is asked.
 *
 * @param err A negative errno from blockdev_write().
 * @return Non-zero when retrying is pointless.
 */
static int write_is_hopeless(int err) {
    return err == E_INVAL || err == E_ROFS || err == E_NODEV || err == E_NXIO;
}

/**
 * @brief Gives up a slot the device will never accept, and says so once.
 *
 * The log line lives here rather than at the three call sites so that "once" is
 * a property of the code rather than a promise: the slot is invalidated in the
 * same breath, so there is nothing left to report a second time.
 */
static void drop_hopeless(int slot) {
    klog_int(LOG_LEVEL_ERROR, "BCACHE",
             "The device can never take this sector; dropped rather than retried forever. Sector",
             (int)cache[slot].sector);
    mark_clean(slot);
    cache[slot].is_valid = 0;
    cache[slot].dev = 0;
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
        /*
         * An eviction the device refused does not free the slot.
         *
         * This used to write and then mark clean unconditionally, so a failed
         * write lost the sector twice over: the data never reached the disk, and
         * the slot was handed to a different sector with the dirty flag cleared,
         * so nothing would ever try again. Refusing the slot keeps the data in
         * the cache where a later flush can still write it, and the caller finds
         * out rather than being handed somebody else's storage.
         *
         * Unless the device was never going to take it. Then keeping the slot is
         * not patience but a deadlock: an unwritable slot is never touched again,
         * so it stays the least recently used one, so every eviction after this
         * picks it and fails the same way. See write_is_hopeless().
         */
        int res = blockdev_write(cache[oldest_idx].dev, cache[oldest_idx].sector,
                                 cache[oldest_idx].data);

        if (res != E_OK) {
            if (!write_is_hopeless(res)) {
                klog_int(LOG_LEVEL_ERROR, "BCACHE",
                         "Eviction write failed; slot kept dirty. Sector",
                         (int)cache[oldest_idx].sector);
                return -1;
            }
            drop_hopeless(oldest_idx);
            return oldest_idx;
        }
        mark_clean(oldest_idx);
    }

    return oldest_idx;
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
    if (dev == 0) return E_NODEV;

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
     * nothing invalidates a slot after init. Routed through mark_clean() anyway
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
 * the sector was taken, not that it reached the disk. What it can report is a
 * cache that could not take it: every slot occupied by dirty data the device
 * refused to accept. bcache_flush() is where a write failure is reported.
 *
 * @param sector The sector number to write.
 * @param buffer The buffer containing the sector data.
 * @return E_OK, or E_IO when no slot could be freed.
 */
int bcache_write_sector(blockdev_t *dev, uint32_t sector, uint8_t *buffer) {
    if (dev == 0) return E_NODEV;

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
            /*
             * A sector the device refused stays dirty. Marking it clean anyway -
             * which this did until v1.2.0 - is how a flush reports success over
             * data that never left RAM, and it also guarantees nothing will try
             * again: the next flush skips it because it looks written.
             *
             * A sector the device could never take is the other case, and it is
             * counted here and dropped: it is still a failure to report, but
             * keeping it would leave the eviction path with a slot it can never
             * free. Either way this returns E_IO, because the caller's question
             * is whether everything reached the disk and the answer is no; which
             * of the two it was belongs to the retry policy, not to the caller.
             */
            int res = blockdev_write(cache[i].dev, cache[i].sector, cache[i].data);

            if (res != E_OK) {
                failed++;
                if (write_is_hopeless(res)) drop_hopeless(i);
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
                /*
                 * Kept, and kept dirty. The same refusal bcache_flush() has: a
                 * sector marked clean over a write that did not happen is a
                 * sector nothing will ever try again. Dropping it would be
                 * worse still, because unmounting is exactly when the last
                 * chance to write it is being taken.
                 *
                 * That argument only holds while a later chance exists. For a
                 * sector this device could never accept there is no later
                 * chance, and holding it would leave a slot pinned against a
                 * device the caller is in the middle of forgetting.
                 */
                failed++;
                if (write_is_hopeless(res)) drop_hopeless(i);
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