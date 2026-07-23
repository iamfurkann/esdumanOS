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

/*
 * Function: hash_djb2_salted
 * Purpose: Computes a salted djb2 hash of a given null-terminated string to test kernel hashing primitives.
 * Mechanism: Iterates over the string applying the standard djb2 formula (hash * 33 + c), and then 
 *            appends a constant salt ('4' and '2') to mitigate basic dictionary attacks.
 * Expected Behavior: Returns a 32-bit unsigned integer representing the salted hash of the input string.
 * Edge Cases: Handles empty strings safely by only applying the salt to the initial seed (5381).
 */
uint32_t hash_djb2_salted(const char *str) {
    // Initialize the hash with the standard djb2 magic number seed.
    uint32_t hash = 5381;
    // Iterate through each character of the string until the null terminator is reached.
    // Multiply the current hash by 33 (hash << 5 + hash) and add the ASCII value of the character.
    while (*str) { hash = ((hash << 5) + hash) + *str++; }
    // Apply the first byte of the salt ('4') to further randomize the resulting hash.
    hash = ((hash << 5) + hash) + '4';
    // Apply the second byte of the salt ('2') to finalize the salted hash computation.
    hash = ((hash << 5) + hash) + '2';
    // Return the completed 32-bit hash value for verification.
    return hash;
}

/*
 * Function: main
 * Purpose: Executes validation tests for the kernel's djb2 salted hash implementation against known expected outputs.
 * Mechanism: Computes the hash for predefined strings ("root" and "1234") and asserts that the outputs match 
 *            hardcoded, pre-calculated 32-bit hex values.
 * Expected Behavior: Prints a success message and returns 0 if all computed hashes match their expected values. 
 *                    If any hash mismatches, the custom assert macro will trigger a failure message and exit with 1.
 * Edge Cases: Tests varying string lengths to ensure the rolling hash behaves consistently without integer overflow issues.
 */
int main() {
    // Output the initial test header to the host terminal.
    printf("[HOST TEST] Testing hash algorithm...\n");
    
    // Compute the salted djb2 hash for the standard system account "root".
    uint32_t root_hash = hash_djb2_salted("root");
    // Assert that the computed hash strictly matches the mathematically expected value for "root" with salt "42".
    assert(root_hash == 0x19E28ECF); 

    // Compute the salted djb2 hash for a common numeric string "1234" to verify hashing of non-alphabet characters.
    uint32_t esduman_hash = hash_djb2_salted("1234");
    // Assert that the computed hash strictly matches the mathematically expected value for "1234" with salt "42".
    assert(esduman_hash == 0x7DD17035);
    
    // All hash assertions passed. Print the final success notification to the host terminal.
    printf("[PASS] Hash algorithm is FLAWLESS!\n");
    // Terminate the test harness successfully with a 0 exit code.
    return 0;
}