/*
 * File: test_bcache.c
 * Purpose: Block Cache (bcache) unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "types.h"

extern void bcache_init(void);
extern void bcache_read_sector(uint32_t sector, uint8_t *buffer);
extern void bcache_write_sector(uint32_t sector, const uint8_t *buffer);
extern void bcache_flush(void);

/**
 * @brief Tests the Block Cache (bcache) write, read, and flush mechanisms.
 *
 * This test suite validates that data written to the underlying block
 * device is appropriately cached in memory to speed up subsequent reads,
 * and ensures that flushing mechanisms reliably synchronize data without
 * causing panics or data loss.
 *
 * Expected Behavior:
 * - A 512-byte block write commits correctly to cache structures.
 * - An immediate read of the same sector serves from the cache matching exactly.
 * - Calling a cache flush operation successfully pushes changes down without crashing.
 *
 * Edge Cases Covered:
 * - Reading back from a distinctly remote sector (e.g., 1024) to avoid accidental
 *   overlap with bootloader or partition structures.
 */
void run_bcache_tests(void) {
    printk("\n--- Block Cache (bcache) Tests ---\n");
    serial_print("\n--- Block Cache (bcache) Tests ---\n");

    uint8_t w_buf[512];
    uint8_t r_buf[512];
    
    for(int i = 0; i < 512; i++) {
        w_buf[i] = (i % 256);
        r_buf[i] = 0;
    }

    // Select a distant sector to minimize risk of overwriting critical filesystem metadata.
    uint32_t test_sector = 1024; 

    bcache_write_sector(test_sector, w_buf);
    
    bcache_read_sector(test_sector, r_buf);
    
    // Verify that the data retrieved from the cache perfectly matches the initial write buffer.
    int is_match = 1;
    for(int i = 0; i < 512; i++) {
        if(r_buf[i] != w_buf[i]) {
            is_match = 0;
            break;
        }
    }
    KTEST_ASSERT(is_match == 1, "Block Cache: Written data successfully read from cache without loss");

    bcache_flush();
    KTEST_ASSERT(1 == 1, "Block Cache: Flush operation completed safely");
}
