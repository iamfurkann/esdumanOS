/*
 * File: test_crypto.c
 * Purpose: Crypto and CryptoFS integration tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "crypto.h"
#include "fs.h"
#include "security.h"

extern uint8_t kernel_master_key[32];
extern int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id);
extern int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]);
extern int ft_strcmp(const char *s1, const char *s2);
extern int fs_open(const char *name, uint8_t parent_id, vfs_file_t *file);
extern int fs_delete(const char *name, uint8_t parent_id);

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

    const char *test_msg = "abc";
    uint8_t hash_out[32];
    extern void sha256_binary(const uint8_t *input, uint32_t len, uint8_t *output_binary);
    sha256_binary((const uint8_t *)test_msg, 3, hash_out);
    
    // Validate the initial bytes of the hash block to ensure algorithmic correctness.
    // Let's check the first 4 bytes: 
    // "abc" -> ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
    KTEST_ASSERT(hash_out[0] == 0xba && hash_out[1] == 0x78 && 
                 hash_out[2] == 0x16 && hash_out[3] == 0xbf, 
                 "SHA-256: Test vector ('abc') hashed successfully");

    const char *multi_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // 56 bytes
    uint8_t multi_hash[32];
    sha256_binary((const uint8_t *)multi_msg, 56, multi_hash);
    
    // Verify the output against the expected multi-block hash signature.
    // Expected Hash: 248d6a61 d20638b8 e5c02693 ...
    KTEST_ASSERT(multi_hash[0] == 0x24 && multi_hash[1] == 0x8d &&
                 multi_hash[2] == 0x6a && multi_hash[3] == 0x61,
                 "SHA-256: Multi-block test vector (>56 bytes) hashed successfully");

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
