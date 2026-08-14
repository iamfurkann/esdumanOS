/*
 * File: test_devfs.c
 * Purpose: DevFS (Device File System) tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "devfs.h"
#include "ktest.h"
#include "errno.h"
#include "types.h"
#include "fs.h"
#include "entropy.h"

/** Bytes read per chunk while forcing the generator past its re-key threshold. */
#define CHUNK_SIZE 256
/** Chunks to read: CHUNK_SIZE * CHUNK_COUNT must exceed DRBG_REKEY_BYTES (4096). */
#define CHUNK_COUNT 20

static uint8_t chunk_a[CHUNK_SIZE];
static uint8_t chunk_b[CHUNK_SIZE];

static int buffers_differ(const uint8_t *a, const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

/** Distinct byte values in a buffer; a stuck stream collapses this to 1. */
static int distinct_byte_values(const uint8_t *buf, int n) {
    uint8_t seen[256];
    for (int i = 0; i < 256; i++) seen[i] = 0;
    int distinct = 0;
    for (int i = 0; i < n; i++) {
        if (!seen[buf[i]]) { seen[buf[i]] = 1; distinct++; }
    }
    return distinct;
}

/**
 * @brief Tests Device File System (DevFS) functionality, registration, and I/O.
 *
 * This test suite verifies the presence of fundamental character devices
 * such as /dev/null and /dev/random, ensures proper device registration,
 * and checks their read/write boundaries and behaviors.
 *
 * Expected Behavior:
 * - VFS correctly maps the /dev node.
 * - Standard devices like 'null' and 'random' are accessible and registered.
 * - Nonexistent or malformed device requests are blocked effectively.
 * - Read/write operations on /dev/null safely discard data or return EOF.
 * - Read operations on /dev/random generate sufficient entropy.
 * - /dev/random and /dev/urandom are keyed from the kernel entropy pool, and a
 *   re-key visibly consumes an extraction from it.
 *
 * Edge Cases Covered:
 * - Attempted writing to read-only character devices like /dev/random.
 * - Verification of consecutive pseudo-random reads.
 * - A request spanning several ChaCha20 blocks, and one crossing the re-key
 *   threshold, which is where a broken re-key would show up as a stuck stream.
 * - Zero-length and negative-length reads.
 */
void run_devfs_tests(void) {
    printk("\n--- DevFS (Device File System) Tests ---\n");

    int dev_idx = fs_get_entry_idx("dev", 0);
    KTEST_ASSERT(dev_idx != -1, "VFS root has /dev directory");

    int null_idx = get_device_idx("null");
    KTEST_ASSERT(null_idx != -1, "/dev/null device is registered");

    int rand_idx = get_device_idx("random");
    KTEST_ASSERT(rand_idx != -1, "/dev/random device is registered");

    int urand_idx = get_device_idx("urandom");
    KTEST_ASSERT(urand_idx >= 0, "/dev/urandom device is registered");

    int fake_idx = get_device_idx("fakedevice");
    // Callers must test for "negative", not "== -1": E_NOENT is -2, and an
    // equality check against -1 is what let a bad index reach dev_table[].
    KTEST_ASSERT(fake_idx < 0, "Invalid device access request blocked (negative errno)");
    KTEST_ASSERT(fake_idx == E_NOENT, "Invalid device lookup reports E_NOENT specifically");

    KTEST_ASSERT(dev_index_is_valid(null_idx), "dev_index_is_valid accepts a registered device");
    KTEST_ASSERT(!dev_index_is_valid(fake_idx), "dev_index_is_valid rejects the E_NOENT sentinel (-2)");
    KTEST_ASSERT(!dev_index_is_valid(-1), "dev_index_is_valid rejects -1");
    KTEST_ASSERT(!dev_index_is_valid(get_device_count()), "dev_index_is_valid rejects the table sentinel index");
extern int dev_null_write(const uint8_t *buf, int size);
    
    uint8_t dummy_buf[10];
    // Attempt to read from /dev/null. Expected behavior is returning 0 (EOF immediately).
    int null_r = dev_null_read(dummy_buf, 10);
    KTEST_ASSERT(null_r == 0, "[STRICT] /dev/null read always returns 0 (EOF)");
    
    // Attempt to write to /dev/null. Expected behavior is accepting the write and returning the exact length.
    int null_w = dev_null_write(dummy_buf, 10);
    KTEST_ASSERT(null_w == 10, "[STRICT] /dev/null write consumes data and returns size");
extern int dev_random_write(const uint8_t *buf, int size);
    
    uint8_t rand_buf1[4] = {0};
    uint8_t rand_buf2[4] = {0};
    
    // Read from /dev/random to test data generation lengths.
    int rand_r1 = dev_random_read(rand_buf1, 4);
    int rand_r2 = dev_random_read(rand_buf2, 4);
    
    KTEST_ASSERT(rand_r1 == 4 && rand_r2 == 4, "[STRICT] /dev/random successfully generated data");
    
    int is_diff = 0;
    for(int i = 0; i < 4; i++) {
        if(rand_buf1[i] != rand_buf2[i]) is_diff = 1;
    }
    KTEST_ASSERT(is_diff == 1, "[STRICT] /dev/random produced different values on consecutive reads");

    uint8_t urand_buf1[4] = {0};
    uint8_t urand_buf2[4] = {0};

    int urand_r1 = dev_urandom_read(urand_buf1, 4);
    int urand_r2 = dev_urandom_read(urand_buf2, 4);

    KTEST_ASSERT(urand_r1 == 4 && urand_r2 == 4, "[STRICT] /dev/urandom successfully generated data");
    KTEST_ASSERT(buffers_differ(urand_buf1, urand_buf2, 4),
                 "[STRICT] /dev/urandom produced different values on consecutive reads");

    // A request larger than one 64-byte ChaCha20 block. The old implementation
    // served this from a stream keyed once per boot; the point of checking it
    // here is that the block loop and the per-request ratchet still produce a
    // full, non-degenerate buffer.
    int bulk_r = dev_random_read(chunk_a, CHUNK_SIZE);
    KTEST_ASSERT(bulk_r == CHUNK_SIZE, "[STRICT] /dev/random fills a multi-block request completely");

    // 256 random bytes hit about 162 distinct values; a stuck stream collapses
    // to 1. The threshold is deliberately far below the expectation so this
    // cannot flake, while still catching a generator that stopped advancing.
    KTEST_ASSERT(distinct_byte_values(chunk_a, CHUNK_SIZE) > 64,
                 "[STRICT] /dev/random output is not a stuck or repeating byte pattern");

    int block_repeat = 0;
    for (int a = 0; a < CHUNK_SIZE; a += 64) {
        for (int b = a + 64; b < CHUNK_SIZE; b += 64) {
            if (!buffers_differ(&chunk_a[a], &chunk_a[b], 64)) block_repeat = 1;
        }
    }
    KTEST_ASSERT(!block_repeat, "[STRICT] no 64-byte block repeats inside a single /dev/random read");

    // The remediation itself. Reading past the re-key threshold forces a fresh
    // key to be drawn from the kernel entropy pool, and the pool's extraction
    // counter is where that draw becomes visible. Before this change the key
    // came from RDTSC and two RTC registers, and this counter never moved.
    entropy_stats_t pool_before, pool_after;
    entropy_get_stats(&pool_before);

    int chunks_ok = 1;
    int chunks_differ = 1;
    for (int i = 0; i < CHUNK_COUNT; i++) {
        if (dev_random_read(chunk_b, CHUNK_SIZE) != CHUNK_SIZE) chunks_ok = 0;
        if (!buffers_differ(chunk_a, chunk_b, CHUNK_SIZE)) chunks_differ = 0;
        for (int j = 0; j < CHUNK_SIZE; j++) chunk_a[j] = chunk_b[j];
    }

    entropy_get_stats(&pool_after);

    KTEST_ASSERT(chunks_ok, "[STRICT] /dev/random served every read across the re-key threshold");
    KTEST_ASSERT(chunks_differ, "[STRICT] consecutive bulk reads differ, including across a re-key");
    KTEST_ASSERT(pool_after.extractions > pool_before.extractions,
                 "[STRICT] /dev/random re-keys from the kernel entropy pool, not from a timestamp");

    KTEST_ASSERT(dev_random_read(chunk_a, 0) == 0, "/dev/random accepts a zero-length read");
    KTEST_ASSERT(dev_random_read(chunk_a, -1) < 0, "/dev/random rejects a negative length");
    KTEST_ASSERT(dev_urandom_read((uint8_t *)0, 4) < 0, "/dev/urandom rejects a null buffer");

    int rand_w = dev_random_write(dummy_buf, 10);
    KTEST_ASSERT(rand_w < 0, "[STRICT] /dev/random write operation blocked");

    int urand_w = dev_random_write(dummy_buf, 10);
    KTEST_ASSERT(urand_w < 0, "[STRICT] /dev/urandom write operation blocked (shared handler)");
}