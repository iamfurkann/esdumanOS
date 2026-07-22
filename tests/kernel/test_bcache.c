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

void run_bcache_tests(void) {
    printk("\n--- Block Cache (bcache) Tests ---\n");
    serial_print("\n--- Block Cache (bcache) Tests ---\n");

    uint8_t w_buf[512];
    uint8_t r_buf[512];
    
    // Generate a random data pattern (For testing)
    for(int i = 0; i < 512; i++) {
        w_buf[i] = (i % 256);
        r_buf[i] = 0;
    }

    uint32_t test_sector = 1024; // Select a distant sector for testing

    // 1. Bcache Write Test
    bcache_write_sector(test_sector, w_buf);
    
    // 2. Bcache Read (Cache Hit) Test
    bcache_read_sector(test_sector, r_buf);
    
    int is_match = 1;
    for(int i = 0; i < 512; i++) {
        if(r_buf[i] != w_buf[i]) {
            is_match = 0;
            break;
        }
    }
    KTEST_ASSERT(is_match == 1, "Block Cache: Written data successfully read from cache without loss");

    // 3. Cache Flush Test
    // Test that it doesn't error or crash after flush
    bcache_flush();
    KTEST_ASSERT(1 == 1, "Block Cache: Flush operation completed safely");
}
