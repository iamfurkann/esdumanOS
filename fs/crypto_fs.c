/*
 * File: crypto_fs.c
 * Purpose: Cryptographic file system functions using AES and SHA-256.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "aes.h"
#include "fs.h"
#include "crypto.h"
#include "security.h"
#include "errno.h"
#include "stdio.h"
#include "kheap.h"
#include "rtc.h"
#include "hmac.h"
static int rdrand_supported = -1;

/**
 * @brief Check if the CPU supports the RDRAND instruction.
 * @return 1 if supported, 0 otherwise.
 */
static int check_rdrand_support(void) {
    uint32_t ecx;
    asm volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        : "=c"(ecx)
        :
        : "eax", "ebx", "edx"
    );
    return (ecx & (1 << 30)) != 0;
}

/**
 * @brief Get a random number using hardware TRNG.
 * @return Random 32-bit integer.
 */
static uint32_t get_hardware_rand(void) {
    uint32_t val;
    uint8_t ok;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
        if (ok) return val;
    }
    return timer_ticks;
}

/**
 * @brief Create an encrypted file on the file system.
 * @param name The name of the file.
 * @param data The plaintext data to write.
 * @param len Length of the data.
 * @param key The 32-byte AES key.
 * @param parent_id The ID of the parent directory.
 * @return Status code (e.g., E_OK or error).
 */
int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id) {
    uint32_t payload_len = 40 + len;
    uint32_t padded_len = (payload_len + 15) & ~15; 
    uint32_t total_len = 16 + padded_len;

    uint8_t *enc_buffer = (uint8_t *)kmalloc(total_len);
    if (!enc_buffer) return E_NOMEM;

    if (rdrand_supported == -1) {
        rdrand_supported = check_rdrand_support();
        if (rdrand_supported) {
            printk("[CRYPTO] TRNG (RDRAND) detected! Cryptographic IV generation active.\n");
        } else {
            printk("[CRYPTO] No RDRAND support. Software PRNG (LCG) in use.\n");
        }
    }

    if (rdrand_supported) {
        uint32_t *iv_ptr = (uint32_t *)enc_buffer;
        for (int i = 0; i < 4; i++) {
            iv_ptr[i] = get_hardware_rand();
        }
    } else {
        static uint32_t global_iv_counter = 0;
        uint32_t stack_noise = 0;
        global_iv_counter++;
        uint8_t accumulator = 0x5A;

        for(int i = 0; i < 16; i++) {
            uint32_t entropy = timer_ticks ^ global_iv_counter ^ ((uint32_t)&stack_noise) ^ (i * 0x1337);
            entropy = (entropy * 1103515245) + 12345;
            accumulator ^= (entropy >> 16) & 0xFF;
            accumulator = (accumulator << 5) | (accumulator >> 3); 
            enc_buffer[i] = accumulator;
        }
    }

    uint32_t checksum = 5381;
    for (uint32_t i = 0; i < len; i++) {
        checksum = ((checksum << 5) + checksum) + data[i];
    }

    uint32_t *hdr = (uint32_t *)(enc_buffer + 16); 
    hdr[0] = 0x53414645; // "SAFE"
    hdr[1] = len;        
    hmac_sha256(key, 32, data, len, (uint8_t *)&hdr[2]);

    for(uint32_t i = 0; i < len; i++) {
        enc_buffer[56 + i] = data[i];
    }
    
    for(uint32_t i = 40 + len; i < padded_len; i++) {
        enc_buffer[16 + i] = 0;
    }
    
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, enc_buffer); 
    AES_CBC_encrypt_buffer(&ctx, enc_buffer + 16, padded_len); 

    int ret = fs_create_file_raw(name, enc_buffer, total_len, parent_id);
    kfree(enc_buffer);
    return ret;
}

/**
 * @brief Read an encrypted file from the file system.
 * @param file The file entry pointer.
 * @param buffer Buffer to store the decrypted data.
 * @param size Number of bytes to read.
 * @param key The 32-byte AES key.
 * @return Number of bytes read.
 */
int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]) {
    if (file->file_size <= 56) return 0; 

    uint8_t *temp_buf = (uint8_t *)kmalloc(file->file_size);
    if (!temp_buf) return 0;

    uint32_t requested_offset = file->current_offset;
    file->current_offset = 0;

    int read_bytes = fs_read_raw(file, temp_buf, file->file_size);
    if (read_bytes <= 56) { 
        file->current_offset = requested_offset; 
        kfree(temp_buf); 
        return 0; 
    }

    uint32_t cipher_len = read_bytes - 16;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, temp_buf);
    AES_CBC_decrypt_buffer(&ctx, temp_buf + 16, cipher_len);

    uint32_t *hdr = (uint32_t *)(temp_buf + 16);
    uint32_t magic = hdr[0];
    uint32_t orig_len = hdr[1];
    uint8_t *stored_hash = (uint8_t *)&hdr[2];

    if (magic != 0x53414645) {
        printk("[CRYPTO ERROR] Magic Number mismatch! Incorrect password or corrupted data.\n");
        kfree(temp_buf);
        return 0;
    }

    uint32_t max_possible_len = cipher_len - 40;
    if (orig_len > max_possible_len) {
        printk("[CRYPTO CRITICAL ERROR] File size specified in header is larger than data!\n");
        kfree(temp_buf);
        return 0;
    }

    uint8_t *plaintext = temp_buf + 56;
    uint8_t calc_hash[32];
    hmac_sha256(key, 32, plaintext, orig_len, calc_hash);

    int hash_ok = 1;
    for (int i = 0; i < 32; i++) {
        if (calc_hash[i] != stored_hash[i]) hash_ok = 0;
    }

    if (!hash_ok) {
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[CRYPTO CRITICAL ERROR] INTEGRITY VIOLATION!\n");
        printk("File has been tampered with or SHA-256 Hash mismatch!\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        kfree(temp_buf);
        return 0;
    }

    uint32_t bytes_to_copy = size;
    if (requested_offset >= orig_len) {
        bytes_to_copy = 0; 
    } else if (requested_offset + bytes_to_copy > orig_len) {
        bytes_to_copy = orig_len - requested_offset;
    }

    for(uint32_t i = 0; i < bytes_to_copy; i++) {
        buffer[i] = plaintext[requested_offset + i];
    }

    file->current_offset = requested_offset + bytes_to_copy;
    kfree(temp_buf);

    return bytes_to_copy;
}