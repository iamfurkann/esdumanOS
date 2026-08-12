/**
 * @file pbkdf2.c
 * @brief PBKDF2-HMAC-SHA256 key derivation function (RFC 2898)
 */

#include "pbkdf2.h"
#include "hmac.h"
#include "libft.h"

int pbkdf2_iterations_are_acceptable(uint32_t iterations) {
    return iterations >= PBKDF2_MIN_ITERATIONS && iterations <= PBKDF2_MAX_ITERATIONS;
}

void pbkdf2_hmac_sha256(
    const uint8_t *password, uint32_t password_len,
    const uint8_t *salt, uint32_t salt_len,
    uint32_t iterations,
    uint8_t *output, uint32_t output_len
) {
    uint8_t initial_salt[256];
    uint8_t u[32];
    uint8_t dk[32];
    /* RFC 2898 requires at least one iteration; treat 0 as 1 rather than
     * silently producing U1 through an empty loop. */
    if (iterations == 0) iterations = 1;

    uint32_t actual_salt_len = salt_len;

    if (actual_salt_len > sizeof(initial_salt) - 4) {
        actual_salt_len = sizeof(initial_salt) - 4;
    }

    ft_memcpy(initial_salt, salt, actual_salt_len);
    initial_salt[actual_salt_len] = 0;
    initial_salt[actual_salt_len + 1] = 0;
    initial_salt[actual_salt_len + 2] = 0;
    initial_salt[actual_salt_len + 3] = 1;

    hmac_sha256(password, password_len, initial_salt, actual_salt_len + 4, u);
    ft_memcpy(dk, u, 32);

    for (uint32_t i = 1; i < iterations; i++) {
        hmac_sha256(password, password_len, u, 32, u);
        for (uint32_t j = 0; j < 32; j++) {
            dk[j] ^= u[j];
        }
    }

    uint32_t copy_len = output_len;
    if (copy_len > 32) {
        copy_len = 32;
    }
    ft_memcpy(output, dk, copy_len);
}
