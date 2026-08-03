/**
 * @file chacha20.c
 * @brief Implementation of the ChaCha20 cipher
 */
#include "chacha20.h"

#define ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define QR(a, b, c, d) ( \
    a += b, d ^= a, d = ROTL(d, 16), \
    c += d, b ^= c, b = ROTL(b, 12), \
    a += b, d ^= a, d = ROTL(d, 8), \
    c += d, b ^= c, b = ROTL(b, 7))

/**
 * @brief Initialize ChaCha20 context
 * @param ctx Pointer to the ChaCha20 context
 * @param key Pointer to the 32-byte key
 * @param nonce Pointer to the 8-byte nonce
 * @return None
 */
void chacha20_init(chacha20_ctx_t *ctx, const uint8_t key[32], const uint8_t nonce[8]) {
    const char *constants = "expand 32-byte k";
    ctx->state[0] = ((uint32_t*)constants)[0];
    ctx->state[1] = ((uint32_t*)constants)[1];
    ctx->state[2] = ((uint32_t*)constants)[2];
    ctx->state[3] = ((uint32_t*)constants)[3];
    for (int i = 0; i < 8; i++) {
        ctx->state[4 + i] = ((uint32_t*)key)[i];
    }
    ctx->state[12] = 0; // counter
    ctx->state[13] = 0; // counter
    ctx->state[14] = ((uint32_t*)nonce)[0];
    ctx->state[15] = ((uint32_t*)nonce)[1];
}

/**
 * @brief Generate the next 64-byte block of keystream
 * @param ctx Pointer to the ChaCha20 context
 * @param out Pointer to the 64-byte output buffer
 * @return None
 */
void chacha20_next_block(chacha20_ctx_t *ctx, uint8_t out[64]) {
    uint32_t working_state[16];
    for (int i = 0; i < 16; i++) {
        working_state[i] = ctx->state[i];
    }
    
    for (int i = 0; i < 10; i++) {
        QR(working_state[0], working_state[4], working_state[8],  working_state[12]);
        QR(working_state[1], working_state[5], working_state[9],  working_state[13]);
        QR(working_state[2], working_state[6], working_state[10], working_state[14]);
        QR(working_state[3], working_state[7], working_state[11], working_state[15]);
        QR(working_state[0], working_state[5], working_state[10], working_state[15]);
        QR(working_state[1], working_state[6], working_state[11], working_state[12]);
        QR(working_state[2], working_state[7], working_state[8],  working_state[13]);
        QR(working_state[3], working_state[4], working_state[9],  working_state[14]);
    }
    
    for (int i = 0; i < 16; i++) {
        working_state[i] += ctx->state[i];
        ((uint32_t*)out)[i] = working_state[i];
    }
    
    ctx->state[12]++;
    if (ctx->state[12] == 0) ctx->state[13]++;
}
