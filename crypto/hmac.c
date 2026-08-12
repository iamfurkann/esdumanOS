/**
 * @file hmac.c
 * @brief Implementation of HMAC-SHA256
 */
#include "hmac.h"
#include "crypto.h"
#include "libft.h"

/**
 * @brief Compute HMAC-SHA256
 *
 * Streams "key_pad || message" through an incremental SHA-256 context rather
 * than assembling the two into one buffer first. The old form allocated
 * 64 + data_len bytes on every call, which put a kmalloc()/kfree() pair - each
 * with its own cli/sti and first-fit free-list walk - inside every iteration of
 * PBKDF2. At the production 600000 iterations that was 1.2 million heap
 * operations per password check. It also silently returned without writing
 * @p out when the allocation failed, handing the caller an uninitialised hash.
 * Nothing here can fail now.
 *
 * @param key Pointer to the cryptographic key.
 * @param key_len Length of the key in bytes.
 * @param data Pointer to the input data.
 * @param data_len Length of the input data in bytes.
 * @param out Buffer to store the 32-byte HMAC-SHA256 result. May alias @p data:
 *            the message is fully absorbed before anything is written back.
 */
void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out) {
    uint8_t k_pad[64];
    uint8_t i_key_pad[64];
    uint8_t o_key_pad[64];
    uint8_t inner_hash[32];
    sha256_ctx_t ctx;

    ft_memset(k_pad, 0, 64);
    if (key_len > 64) {
        sha256_binary(key, key_len, k_pad);
    } else {
        ft_memcpy(k_pad, key, key_len);
    }

    for (int i = 0; i < 64; i++) {
        i_key_pad[i] = k_pad[i] ^ 0x36;
        o_key_pad[i] = k_pad[i] ^ 0x5c;
    }

    // Inner hash: H(i_key_pad || data)
    sha256_init(&ctx);
    sha256_update(&ctx, i_key_pad, 64);
    sha256_update(&ctx, data, (uint32_t)data_len);
    sha256_final(&ctx, inner_hash);

    // Outer hash: H(o_key_pad || inner_hash)
    sha256_init(&ctx);
    sha256_update(&ctx, o_key_pad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}
