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
void *ft_memcpy(void *dest, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

int ft_memcmp(const void *s1, const void *s2, uint32_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
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

int main(void) {
    printf("\n--- Host Crypto (AES-256-CBC) Correctness Test ---\n");

    uint8_t buffer[16];
    ft_memcpy(buffer, nist_plaintext, 16); 
    
    struct AES_ctx ctx;
    uint8_t temp_iv[16];
    ft_memcpy(temp_iv, nist_iv, 16);

    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    AES_CBC_encrypt_buffer(&ctx, buffer, 16);

    if (ft_memcmp(buffer, nist_expected_ciphertext, 16) != 0) {
        printf("[FAIL] AES Encryption DOES NOT COMPLY with NIST Standards!\n");
        return 1;
    }
    printf("[PASS] AES-256 Encryption Complies %s with NIST FIPS-197 Standards.\n", "100%%");

    ft_memcpy(temp_iv, nist_iv, 16); 
    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    AES_CBC_decrypt_buffer(&ctx, buffer, 16);

    if (ft_memcmp(buffer, nist_plaintext, 16) != 0) {
        printf("❌ [FAIL] Data corrupted during AES Decryption phase!\n");
        return 1;
    }
    printf("[PASS] AES-256 Decryption Complies %s with NIST Standards.\n", "100%%");
    printf("====================================================\n\n");
    return 0; 
}