#ifndef CHACHA20_H
#define CHACHA20_H

#include "types.h"

typedef struct {
    uint32_t state[16];
} chacha20_ctx_t;

/**
 * @brief Initialize ChaCha20 context with a 256-bit key and a 64-bit nonce
 */
void chacha20_init(chacha20_ctx_t *ctx, const uint8_t key[32], const uint8_t nonce[8]);

/**
 * @brief Extract the next 64 bytes of pseudo-random stream
 */
void chacha20_next_block(chacha20_ctx_t *ctx, uint8_t out[64]);

#endif
