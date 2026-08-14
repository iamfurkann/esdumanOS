#ifndef CRYPTO_H
#define CRYPTO_H

/* arch.h was included here and used for nothing. The #if defined(ARCH_RISCV64)
 * further down tests a -D macro from the Makefile, not anything arch.h defines. */
#include "types.h"

/**
 * @brief AES block size in bytes.
 */
#define AES_BLOCK_SIZE 16

/**
 * @brief Context structure for AES-256 operations.
 * Holds the expanded round keys and the original key.
 */
typedef struct {
    uint8_t round_key[240];  // AES-256: 14 rounds × 16 bytes
    uint8_t key[32];         // 256-bit Key
} aes256_ctx_t;

// AES-256
/**
 * @brief Initializes an AES-256 context with the given key.
 * @param ctx Pointer to the AES context.
 * @param key Pointer to the 32-byte key.
 */
void aes256_init(aes256_ctx_t *ctx, const uint8_t *key);

/**
 * @brief Encrypts a single 16-byte block using AES-256.
 * @param ctx Pointer to the initialized AES context.
 * @param in Pointer to the 16-byte input plaintext.
 * @param out Pointer to the 16-byte output ciphertext.
 */
void aes256_encrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

/**
 * @brief Decrypts a single 16-byte block using AES-256.
 * @param ctx Pointer to the initialized AES context.
 * @param in Pointer to the 16-byte input ciphertext.
 * @param out Pointer to the 16-byte output plaintext.
 */
void aes256_decrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

// AES-CBC Mode
/**
 * @brief Encrypts data using AES-256 in Cipher Block Chaining (CBC) mode.
 * @param ctx Pointer to the AES context.
 * @param iv Pointer to the 16-byte Initialization Vector.
 * @param in Pointer to the input plaintext.
 * @param out Pointer to the output ciphertext buffer.
 * @param len Length of the data in bytes (must be a multiple of 16).
 */
void aes256_cbc_encrypt(aes256_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, uint32_t len);

/**
 * @brief Decrypts data using AES-256 in Cipher Block Chaining (CBC) mode.
 * @param ctx Pointer to the AES context.
 * @param iv Pointer to the 16-byte Initialization Vector.
 * @param in Pointer to the input ciphertext.
 * @param out Pointer to the output plaintext buffer.
 * @param len Length of the data in bytes (must be a multiple of 16).
 */
void aes256_cbc_decrypt(aes256_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, uint32_t len);

// SHA-256 [MISSING DEFINITION ADDED]
/**
 * @brief Computes a SHA-256 hash of the input string and outputs it as hex.
 * @param input Null-terminated input string.
 * @param output_hex Buffer to hold the resulting 64-character hex string.
 */
/**
 * @brief Incremental SHA-256 state.
 *
 * Exists so that a message can be hashed in pieces. HMAC uses it to absorb
 * "key_pad || message" without building the concatenation in memory first.
 */
typedef struct {
    uint32_t state[8];
    uint32_t total_len;   /**< Bytes absorbed so far. */
    uint32_t buffer_len;  /**< Bytes currently held in buffer. */
    uint8_t  buffer[64];
} sha256_ctx_t;

/**
 * @brief Starts a new incremental SHA-256 computation.
 * @param ctx Context to initialise.
 */
void sha256_init(sha256_ctx_t *ctx);

/**
 * @brief Feeds more data into a SHA-256 computation.
 * @param ctx Context previously passed to sha256_init().
 * @param data Bytes to absorb; ignored when len is 0.
 * @param len Number of bytes.
 */
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);

/**
 * @brief Appends the padding and produces the digest.
 * @param ctx Context to finalise.
 * @param output_binary 32-byte output buffer.
 */
void sha256_final(sha256_ctx_t *ctx, uint8_t *output_binary);

void sha256_to_hex(const char *input, char *output_hex);

/* RISC-V HARDWARE ACCELERATION */
#if defined(ARCH_RISCV64) && defined(HAS_ZKN_EXTENSION)
    /**
     * @brief Hardware-accelerated AES-256 block encryption.
     * @param ctx Pointer to the AES context.
     * @param in Pointer to the 16-byte input plaintext.
     * @param out Pointer to the 16-byte output ciphertext.
     */
    void aes256_encrypt_hw(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

    /**
     * @brief Hardware-accelerated AES-256 block decryption.
     * @param ctx Pointer to the AES context.
     * @param in Pointer to the 16-byte input ciphertext.
     * @param out Pointer to the 16-byte output plaintext.
     */
    void aes256_decrypt_hw(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
#endif


// --- Added by Refactor Script ---
extern void sha256_binary(const uint8_t *input, uint32_t len, uint8_t *output_binary);

#endif