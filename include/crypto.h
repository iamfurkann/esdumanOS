#ifndef CRYPTO_H
#define CRYPTO_H

/* arch.h was included here and used for nothing. The one thing that looked like
 * it might have needed it - an #if defined(ARCH_RISCV64) block further down - is
 * gone too; see below. */
#include "types.h"

/*
 * There was an AES-256 interface declared here - aes256_ctx_t, aes256_init(),
 * aes256_encrypt(), aes256_decrypt(), aes256_cbc_encrypt(),
 * aes256_cbc_decrypt() - and an AES_BLOCK_SIZE beside it. None of the six was
 * ever implemented and nothing ever called one. Anybody opening this header to
 * find out how the system does AES would have found a complete, documented,
 * entirely fictional API.
 *
 * The real one is in aes.h and crypto/aes.c: struct AES_ctx, AES_init_ctx_iv(),
 * AES_CBC_encrypt_buffer() and AES_CBC_decrypt_buffer(), with AES_BLOCKLEN for
 * the block size. That is what CryptoFS uses.
 */

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

/*
 * A RISC-V hardware-acceleration block sat here, guarded by
 * #if defined(ARCH_RISCV64) && defined(HAS_ZKN_EXTENSION). It declared
 * aes256_encrypt_hw() and aes256_decrypt_hw() in terms of the fictional context
 * type above, so it could not have compiled even on the port it was written for -
 * and there is no port: the Makefile has a RISC-V branch and arch/riscv/ does not
 * exist. It goes with the type it depended on.
 */


// --- Added by Refactor Script ---
extern void sha256_binary(const uint8_t *input, uint32_t len, uint8_t *output_binary);

#endif