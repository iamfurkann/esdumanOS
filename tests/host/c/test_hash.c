/*
 * File: test_hash.c
 * Purpose: Tests the djb2 salted hash algorithm implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "types.h"

// Declare printf externally to print logs to the Ubuntu terminal:
extern int printf(const char *format, ...);

// =========================================================================
// MOCKING: Custom Assert Macro
// Custom assertion to avoid dependency on the host's <assert.h>.
// =========================================================================
#define assert(expr) \
    do { \
        if (!(expr)) { \
            printf("[FAIL] Unexpected Value! Error at line: %s\n", #expr); \
            return 1; \
        } \
    } while(0)

uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) { hash = ((hash << 5) + hash) + *str++; }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

int main() {
    printf("[HOST TEST] Testing hash algorithm...\n");
    
    uint32_t root_hash = hash_djb2_salted("root");
    assert(root_hash == 0x19E28ECF); 

    uint32_t esduman_hash = hash_djb2_salted("1234");
    assert(esduman_hash == 0x7DD17035);
    
    printf("[PASS] Hash algorithm is FLAWLESS!\n");
    return 0;
}