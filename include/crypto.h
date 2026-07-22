#ifndef CRYPTO_H
#define CRYPTO_H

#include "types.h"
#include "arch.h"

#define AES_BLOCK_SIZE 16

typedef struct {
    uint8_t round_key[240];  // AES-256: 14 round × 16 byte
    uint8_t key[32];         // 256-bit Key
} aes256_ctx_t;

// AES-256
void aes256_init(aes256_ctx_t *ctx, const uint8_t *key);
void aes256_encrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
void aes256_decrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

// AES-CBC Mod
void aes256_cbc_encrypt(aes256_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, uint32_t len);
void aes256_cbc_decrypt(aes256_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, uint32_t len);

// SHA-256 [EKSİK OLAN TANIM EKLENDİ]
void sha256_to_hex(const char *input, char *output_hex);

/* RISC-V DONANIM HIZLANDIRMASI*/
#if defined(ARCH_RISCV64) && defined(HAS_ZKN_EXTENSION)
    void aes256_encrypt_hw(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
    void aes256_decrypt_hw(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
#endif

#endif