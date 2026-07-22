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

void run_crypto_tests(void) {
    printk("\n--- Crypto & CryptoFS Tests ---\n");
    serial_print("\n--- Crypto & CryptoFS Tests ---\n");

    // 1. SHA-256 Test (NIST Basic Test Vector)
    // The SHA256 hash of "abc" is a known standard.
    const char *test_msg = "abc";
    uint8_t hash_out[32];
    extern void sha256_binary(const uint8_t *input, uint32_t len, uint8_t *output_binary);
    sha256_binary((const uint8_t *)test_msg, 3, hash_out);
    
    // Let's check the first 4 bytes: 
    // "abc" -> ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
    KTEST_ASSERT(hash_out[0] == 0xba && hash_out[1] == 0x78 && 
                 hash_out[2] == 0x16 && hash_out[3] == 0xbf, 
                 "SHA-256: Test vector ('abc') hashed successfully");

    // 1.5 SHA-256 NIST Multi-Block Test (>56 bytes, forces padding to next block)
    const char *multi_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // 56 bytes
    uint8_t multi_hash[32];
    sha256_binary((const uint8_t *)multi_msg, 56, multi_hash);
    // Expected Hash: 248d6a61 d20638b8 e5c02693 ...
    KTEST_ASSERT(multi_hash[0] == 0x24 && multi_hash[1] == 0x8d &&
                 multi_hash[2] == 0x6a && multi_hash[3] == 0x61,
                 "SHA-256: Multi-block test vector (>56 bytes) hashed successfully");

    // 2. CryptoFS Encrypted File Creation and Reading (Round-Trip)
    const char *secret_data = "TOP SECRET DAT";
    int e_res = fs_create_encrypted("crypto_test.txt", (const uint8_t *)secret_data, 15, kernel_master_key, 0);
    KTEST_ASSERT(e_res == 0, "CryptoFS: Encrypted file successfully written to disk");

    vfs_file_t file;
    int o_res = fs_open("crypto_test.txt", 0, &file);
    KTEST_ASSERT(o_res == 0, "CryptoFS: Encrypted file successfully opened");

    uint8_t read_buf[20];
    int r_res = fs_read_encrypted(&file, read_buf, 15, kernel_master_key);
    KTEST_ASSERT(r_res == 15, "CryptoFS: Encrypted file read size returned correctly");
    
    KTEST_ASSERT(ft_strcmp((char*)read_buf, secret_data) == 0, "CryptoFS: Decrypted read data is exactly the same as original");

    fs_delete("crypto_test.txt", 0);
}
