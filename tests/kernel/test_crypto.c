/*
 * File: test_crypto.c
 * Purpose: Crypto and CryptoFS integration tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "crypto.h"
#include "hmac.h"
#include "pbkdf2.h"
#include "fs.h"
#include "security.h"
#include "libft.h"

/**
 * @brief Compares two byte strings.
 * @return 1 when the first n bytes are identical.
 */
static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}
/**
 * @brief Tests the cryptographic hashing and encrypted file system (CryptoFS) integration.
 *
 * This test suite validates the internal SHA-256 implementation against standard
 * NIST basic and multi-block test vectors. It also evaluates the CryptoFS layer
 * by writing data with the kernel master key and retrieving it to ensure a lossless,
 * secure round-trip decryption.
 *
 * Expected Behavior:
 * - SHA-256 hashing correctly produces standard NIST hash outputs for given inputs.
 * - Multi-block inputs force the internal padding mechanism to operate correctly.
 * - fs_create_encrypted safely writes ciphertext to the underlying disk.
 * - fs_read_encrypted successfully decrypts the payload matching the original string.
 *
 * Edge Cases Covered:
 * - Hash inputs strictly exceeding 56 bytes to force a block boundary split.
 * - In-place buffer reading and decryption overlapping securely.
 */
void run_crypto_tests(void) {
    printk("\n--- Crypto & CryptoFS Tests ---\n");
    serial_print("\n--- Crypto & CryptoFS Tests ---\n");

    /*
     * Full-digest comparisons, not just the leading bytes. The padding and
     * length encoding live in the last block, so a prefix check cannot see a
     * fault there - and that code was just rewritten onto an incremental
     * context.
     */
    const char *test_msg = "abc";
    static const uint8_t sha_abc[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    uint8_t hash_out[32];
    sha256_binary((const uint8_t *)test_msg, 3, hash_out);
    KTEST_ASSERT(bytes_equal(hash_out, sha_abc, 32),
                 "[STRICT] SHA-256: 'abc' matches the reference digest in full");

    // 56 bytes: the padding no longer fits in the final block, forcing an extra one.
    const char *multi_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const uint8_t sha_multi[32] = {
        0x24,0x8d,0x6a,0x61,0xd2,0x06,0x38,0xb8,0xe5,0xc0,0x26,0x93,0x0c,0x3e,0x60,0x39,
        0xa3,0x3c,0xe4,0x59,0x64,0xff,0x21,0x67,0xf6,0xec,0xed,0xd4,0x19,0xdb,0x06,0xc1
    };
    uint8_t multi_hash[32];
    sha256_binary((const uint8_t *)multi_msg, 56, multi_hash);
    KTEST_ASSERT(bytes_equal(multi_hash, sha_multi, 32),
                 "[STRICT] SHA-256: 56-byte message matches the reference digest in full");

    /*
     * Streaming must agree with the one-shot form byte for byte. HMAC now feeds
     * "key_pad || message" in two pieces instead of concatenating them, so a
     * bug in the buffering would corrupt every hash in the system.
     */
    sha256_ctx_t ctx;
    uint8_t streamed[32];

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)"a", 1);
    sha256_update(&ctx, (const uint8_t *)"b", 1);
    sha256_update(&ctx, (const uint8_t *)"c", 1);
    sha256_final(&ctx, streamed);
    KTEST_ASSERT(bytes_equal(streamed, sha_abc, 32),
                 "[STRICT] SHA-256: byte-at-a-time streaming equals the one-shot digest");

    // Split across a block boundary: 1 + 55 leaves the buffer exactly full.
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)multi_msg, 1);
    sha256_update(&ctx, (const uint8_t *)multi_msg + 1, 55);
    sha256_final(&ctx, streamed);
    KTEST_ASSERT(bytes_equal(streamed, sha_multi, 32),
                 "[STRICT] SHA-256: streaming across a block boundary equals the one-shot digest");

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)multi_msg, 0);
    sha256_update(&ctx, 0, 16);
    sha256_update(&ctx, (const uint8_t *)multi_msg, 56);
    sha256_final(&ctx, streamed);
    KTEST_ASSERT(bytes_equal(streamed, sha_multi, 32),
                 "SHA-256: empty and null updates are ignored");

    /* --- HMAC-SHA256, RFC 4231 cases 1 and 2 --- */
    uint8_t hmac_out[32];

    uint8_t hk1[20];
    for (int i = 0; i < 20; i++) hk1[i] = 0x0b;
    static const uint8_t hmac_case1[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    hmac_sha256(hk1, 20, (const uint8_t *)"Hi There", 8, hmac_out);
    KTEST_ASSERT(bytes_equal(hmac_out, hmac_case1, 32),
                 "[STRICT] HMAC-SHA256: RFC 4231 case 1 matches");

    static const uint8_t hmac_case2[32] = {
        0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
        0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
    };
    hmac_sha256((const uint8_t *)"Jefe", 4,
                (const uint8_t *)"what do ya want for nothing?", 28, hmac_out);
    KTEST_ASSERT(bytes_equal(hmac_out, hmac_case2, 32),
                 "[STRICT] HMAC-SHA256: RFC 4231 case 2 matches");

    /*
     * PBKDF2-HMAC-SHA256 end to end. These exercise the whole chain - streaming
     * SHA-256, HMAC over it, and the iteration/XOR loop on top - against
     * reference outputs, which is what makes the rewrite safe to trust.
     */
    uint8_t dk[32];

    static const uint8_t pbkdf2_c1[32] = {
        0x12,0x0f,0xb6,0xcf,0xfc,0xf8,0xb3,0x2c,0x43,0xe7,0x22,0x52,0x56,0xc4,0xf8,0x37,
        0xa8,0x65,0x48,0xc9,0x2c,0xcc,0x35,0x48,0x08,0x05,0x98,0x7c,0xb7,0x0b,0xe1,0x7b
    };
    pbkdf2_hmac_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 1, dk, 32);
    KTEST_ASSERT(bytes_equal(dk, pbkdf2_c1, 32),
                 "[STRICT] PBKDF2-HMAC-SHA256: reference vector, 1 iteration");

    static const uint8_t pbkdf2_c2[32] = {
        0xae,0x4d,0x0c,0x95,0xaf,0x6b,0x46,0xd3,0x2d,0x0a,0xdf,0xf9,0x28,0xf0,0x6d,0xd0,
        0x2a,0x30,0x3f,0x8e,0xf3,0xc2,0x51,0xdf,0xd6,0xe2,0xd8,0x5a,0x95,0x47,0x4c,0x43
    };
    pbkdf2_hmac_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 2, dk, 32);
    KTEST_ASSERT(bytes_equal(dk, pbkdf2_c2, 32),
                 "[STRICT] PBKDF2-HMAC-SHA256: reference vector, 2 iterations (XOR chain)");

    static const uint8_t pbkdf2_c4096[32] = {
        0xc5,0xe4,0x78,0xd5,0x92,0x88,0xc8,0x41,0xaa,0x53,0x0d,0xb6,0x84,0x5c,0x4c,0x8d,
        0x96,0x28,0x93,0xa0,0x01,0xce,0x4e,0x11,0xa4,0x96,0x38,0x73,0xaa,0x98,0x13,0x4a
    };
    pbkdf2_hmac_sha256((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 4096, dk, 32);
    KTEST_ASSERT(bytes_equal(dk, pbkdf2_c4096, 32),
                 "[STRICT] PBKDF2-HMAC-SHA256: reference vector, 4096 iterations");

    /* --- stored iteration counts are bounded --- */
    KTEST_ASSERT(pbkdf2_iterations_are_acceptable(PBKDF2_DEFAULT_ITERATIONS),
                 "PBKDF2: this build's own iteration count is accepted");
    KTEST_ASSERT(!pbkdf2_iterations_are_acceptable(0),
                 "PBKDF2: a zero iteration count is rejected");
    KTEST_ASSERT(!pbkdf2_iterations_are_acceptable(1),
                 "[STRICT] PBKDF2: a weakened iteration count is rejected");
    KTEST_ASSERT(!pbkdf2_iterations_are_acceptable(2000000000u),
                 "[STRICT] PBKDF2: an absurd iteration count is rejected (denial of service)");
    KTEST_ASSERT(!pbkdf2_iterations_are_acceptable(PBKDF2_MAX_ITERATIONS + 1),
                 "PBKDF2: the ceiling is exclusive above PBKDF2_MAX_ITERATIONS");

    const char *secret_data = "TOP SECRET DAT";
    int e_res = fs_create_encrypted("crypto_test.txt", (const uint8_t *)secret_data, 15, kernel_master_key, 0);
    KTEST_ASSERT(e_res == 0, "CryptoFS: Encrypted file successfully written to disk");

    vfs_file_t file;
    int o_res = fs_open("crypto_test.txt", 0, &file);
    KTEST_ASSERT(o_res == 0, "CryptoFS: Encrypted file successfully opened");

    uint8_t read_buf[20];
    int r_res = fs_read_encrypted(&file, read_buf, 15, kernel_master_key);
    KTEST_ASSERT(r_res == 15, "CryptoFS: Encrypted file read size returned correctly");
    
    // Verify data integrity post-decryption.
    KTEST_ASSERT(ft_strcmp((char*)read_buf, secret_data) == 0, "CryptoFS: Decrypted read data is exactly the same as original");

    fs_delete("crypto_test.txt", 0);
}
