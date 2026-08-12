/*
 * File: encrypt_tool.c
 * Purpose: Tool for encrypting ELF files using AES-256 and SHA-256.
 *
 * This file is part of the esdumanOS test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <openssl/hmac.h>

/**
 * @brief Computes the raw (binary) SHA-256 hash of the given input.
 */
void sha256_binary_local(const unsigned char *input, size_t input_len, unsigned char *output_binary) {
    unsigned int lengthOfHash = 0;
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, EVP_sha256(), NULL);
    EVP_DigestUpdate(context, input, input_len);
    EVP_DigestFinal_ex(context, output_binary, &lengthOfHash);
    EVP_MD_CTX_free(context);
}

/**
 * @brief Computes the HMAC-SHA256 hash of the given input to match the kernel side.
 */
void hmac_sha256_local(const unsigned char *key, size_t key_len, const unsigned char *input, size_t input_len, unsigned char *output_binary) {
    unsigned int lengthOfHash = 0;
    HMAC(EVP_sha256(), key, key_len, input, input_len, output_binary, &lengthOfHash);
}

/**
 * @brief Main function of the encrypt tool.
 * 
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on error.
 */
int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s <input> <output> <hex_key_64chars>\n", argv[0]);
        return 1;
    }


    FILE *fin = fopen(argv[1], "rb");
    if (!fin) { perror("Failed to open input file"); return 1; }
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *file_data = malloc(file_size);
    fread(file_data, 1, file_size, fin);
    fclose(fin);


    // Direct hex key — no KDF needed. Key must be exactly 64 hex chars (32 bytes).
    char *hex_key = argv[3];
    if (strlen(hex_key) != 64) {
        printf("ERROR: Key must be exactly 64 hex characters (32 bytes).\n");
        free(file_data);
        return 1;
    }
    
    uint8_t master_key[32];
    for (int i = 0; i < 32; i++) {
        unsigned int byte_val;
        sscanf(&hex_key[i * 2], "%2x", &byte_val);
        master_key[i] = (uint8_t)byte_val;
    }

    uint32_t payload_len = 40 + file_size;
    uint32_t padded_len = (payload_len + 15) & ~15;
    uint8_t *padded_data = calloc(1, padded_len);
    
    uint32_t *hdr = (uint32_t *)padded_data;
    hdr[0] = 0x53414645; // "SAFE" Magic Number
    hdr[1] = (uint32_t)file_size;
    
    hmac_sha256_local(master_key, 32, file_data, file_size, (unsigned char *)&hdr[2]);

    memcpy(padded_data + 40, file_data, file_size);
    free(file_data);

    uint8_t iv[16];
    if (!RAND_bytes(iv, 16)) {
        printf("ERROR: Failed to generate cryptographically secure random IV!\n");
        free(padded_data);
        return 1;
    }

    uint8_t *encrypted_data = malloc(padded_len);
    int out_len1 = 0, out_len2 = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, master_key, iv);
    EVP_CIPHER_CTX_set_padding(ctx, 0); 
    EVP_EncryptUpdate(ctx, encrypted_data, &out_len1, padded_data, padded_len);
    EVP_EncryptFinal_ex(ctx, encrypted_data + out_len1, &out_len2);
    EVP_CIPHER_CTX_free(ctx);

    FILE *fout = fopen(argv[2], "wb");
    fwrite(iv, 1, 16, fout); 
    
    fwrite(encrypted_data, 1, padded_len, fout);
    fclose(fout);

    free(padded_data);
    free(encrypted_data);

    printf("[+] %s successfully encrypted with AES-256 (direct hex key).\n", argv[1]);
    return 0;
}