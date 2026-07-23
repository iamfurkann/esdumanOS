/*
 * File: test_crypto.c
 * Purpose: Correctness test for AES-256-CBC encryption and decryption using NIST FIPS-197 standards.
 *
 * This file is part of the esdumanOS test suite.
 */
// [ARCHITECTURE PATCH]: Removed Ubuntu's system headers (<stdio.h>, <stdint.h>, etc.)!
// We only use our own Kernel types now to avoid conflicts.
#include "types.h"
#include "aes.h"

// Externally declaring printf from Ubuntu's libc just to print to the screen
extern int printf(const char *format, ...);

// =========================================================================
// MOCKING LAYER: 
// Mocking the kernel memory functions required by aes.c for testing.
// =========================================================================

/*
 * Function: ft_memcpy
 * Purpose: Mocks the kernel's memory copy functionality to support the AES algorithm testing on the host.
 * Mechanism: Iterates byte-by-byte from the source pointer to the destination pointer for the specified length.
 * Expected Behavior: Returns a pointer to the destination memory block after all bytes are copied.
 * Edge Cases: Handles zero-length copies without side effects. Expects non-overlapping memory regions.
 */
void *ft_memcpy(void *dest, const void *src, uint32_t n) {
    // Cast the void destination pointer to a byte-sized pointer for accurate arithmetic.
    uint8_t *d = (uint8_t *)dest;
    // Cast the void source pointer to a read-only byte-sized pointer.
    const uint8_t *s = (const uint8_t *)src;
    // Sequentially copy each byte from the source memory region to the destination.
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    // Return the original destination pointer as required by standard memcpy behavior.
    return dest;
}

/*
 * Function: ft_memcmp
 * Purpose: Mocks the kernel's memory comparison functionality to validate AES outputs against expected vectors.
 * Mechanism: Compares the byte values at two memory locations sequentially up to n bytes.
 * Expected Behavior: Returns 0 if the memory blocks are identical. Returns the arithmetic difference of the first 
 *                    diverging byte pair if they differ.
 * Edge Cases: Handles zero-length comparisons by returning 0 safely.
 */
int ft_memcmp(const void *s1, const void *s2, uint32_t n) {
    // Cast the first void pointer to a read-only byte-sized pointer.
    const uint8_t *p1 = (const uint8_t *)s1;
    // Cast the second void pointer to a read-only byte-sized pointer.
    const uint8_t *p2 = (const uint8_t *)s2;
    // Iterate through both memory regions byte-by-byte for up to n bytes.
    for (uint32_t i = 0; i < n; i++) {
        // If a mismatch is detected, return the arithmetic difference between the two bytes immediately.
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    // All compared bytes were identical, so return 0 to indicate a perfect match.
    return 0;
}

// =========================================================================
// NIST SP 800-38A (AES-256 CBC) Standard Test Vector (F.2.5)
// =========================================================================
const uint8_t nist_key[32] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};
const uint8_t nist_iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};
const uint8_t nist_plaintext[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
};
const uint8_t nist_expected_ciphertext[16] = {
    0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba, 0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6
};

/*
 * Function: main
 * Purpose: Executes the AES-256-CBC correctness verification using predefined NIST FIPS-197 test vectors.
 * Mechanism: Encrypts the standard NIST plaintext block with the NIST key and initialization vector (IV), 
 *            compares the output against the expected NIST ciphertext, and then decrypts it to verify the original plaintext is recovered.
 * Expected Behavior: Outputs success messages and returns 0 if both encryption and decryption stages yield perfectly matching outputs. 
 *                    If either stage fails, prints an error log and exits with 1.
 * Edge Cases: Ensures the IV is correctly reset before the decryption phase, as CBC mode mutates the IV during operation.
 */
int main(void) {
    // Print the header to the host terminal to indicate the start of the crypto correctness test suite.
    printf("\n--- Host Crypto (AES-256-CBC) Correctness Test ---\n");

    uint8_t buffer[16];
    // Copy the standardized NIST plaintext into the working buffer, simulating a user payload preparing for encryption.
    ft_memcpy(buffer, nist_plaintext, 16); 
    
    struct AES_ctx ctx;
    uint8_t temp_iv[16];
    // Copy the NIST IV into a temporary buffer since the AES algorithm will mutate the IV during CBC mode encryption.
    ft_memcpy(temp_iv, nist_iv, 16);

    // Initialize the AES context structure with the 256-bit NIST key and the temporary IV.
    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    // Execute the AES-CBC encryption routine on the working buffer in place, transforming the plaintext to ciphertext.
    AES_CBC_encrypt_buffer(&ctx, buffer, 16);

    // Compare the newly generated ciphertext in the buffer against the mathematically proven NIST standard ciphertext.
    if (ft_memcmp(buffer, nist_expected_ciphertext, 16) != 0) {
        // The comparison failed, indicating a fatal logical flaw in the kernel's AES implementation.
        printf("[FAIL] AES Encryption DOES NOT COMPLY with NIST Standards!\n");
        return 1;
    }
    // The encryption stage perfectly matched the test vector. Log the success.
    printf("[PASS] AES-256 Encryption Complies %s with NIST FIPS-197 Standards.\n", "100%%");

    // Reset the temporary IV back to its original NIST value for the decryption phase, as it was altered previously.
    ft_memcpy(temp_iv, nist_iv, 16); 
    // Re-initialize the AES context with the key and the freshly reset IV to prepare for decryption.
    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    // Execute the AES-CBC decryption routine on the buffer in place, converting the ciphertext back to plaintext.
    AES_CBC_decrypt_buffer(&ctx, buffer, 16);

    // Compare the newly decrypted plaintext against the original NIST plaintext to verify a lossless round-trip.
    if (ft_memcmp(buffer, nist_plaintext, 16) != 0) {
        // The comparison failed, meaning the AES decryption logic corrupted the data structure.
        printf("❌ [FAIL] Data corrupted during AES Decryption phase!\n");
        return 1;
    }
    // The decryption stage recovered the exact original data. Log the final success.
    printf("[PASS] AES-256 Decryption Complies %s with NIST Standards.\n", "100%%");
    printf("====================================================\n\n");
    // Terminate the crypto test suite with a clean exit code.
    return 0; 
}