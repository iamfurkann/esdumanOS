#include "hmac.h"
#include "crypto.h"
#include "kernel.h"
#include "kheap.h"
#include "libft.h"

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out) {
    uint8_t k_pad[64];
    uint8_t o_key_pad[64];
    uint8_t i_key_pad[64];
    uint8_t inner_hash[32];

    ft_memset(k_pad, 0, 64);
    if (key_len > 64) {
        sha256_binary(key, key_len, k_pad);
    } else {
        ft_memcpy(k_pad, key, key_len);
    }

    for (int i = 0; i < 64; i++) {
        o_key_pad[i] = k_pad[i] ^ 0x5c;
        i_key_pad[i] = k_pad[i] ^ 0x36;
    }

    // Inner hash: H(i_key_pad || data)
    uint8_t *inner_data = (uint8_t*)kmalloc(64 + data_len);
    if (!inner_data) return; // OOM fallback
    
    ft_memcpy(inner_data, i_key_pad, 64);
    ft_memcpy(inner_data + 64, data, data_len);
    sha256_binary(inner_data, 64 + data_len, inner_hash);
    kfree(inner_data);

    // Outer hash: H(o_key_pad || inner_hash)
    uint8_t outer_data[64 + 32];
    ft_memcpy(outer_data, o_key_pad, 64);
    ft_memcpy(outer_data + 64, inner_hash, 32);
    sha256_binary(outer_data, 64 + 32, out);
}
