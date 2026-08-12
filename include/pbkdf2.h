#ifndef PBKDF2_H
#define PBKDF2_H

#include "types.h"

#ifdef PBKDF2_DEV_ITERATIONS
#define PBKDF2_DEFAULT_ITERATIONS PBKDF2_DEV_ITERATIONS
#else
#define PBKDF2_DEFAULT_ITERATIONS 600000
#endif
/**
 * @brief Lowest iteration count accepted when verifying a stored hash.
 *
 * Tracks what this build itself produces, so a kernel never accepts a hash
 * weaker than the ones it writes. Raising PBKDF2_DEFAULT_ITERATIONS therefore
 * also invalidates hashes written by older builds - which is the correct
 * direction: they would have to be re-derived at the next password change.
 */
#define PBKDF2_MIN_ITERATIONS     PBKDF2_DEFAULT_ITERATIONS

/**
 * @brief Highest iteration count accepted when verifying a stored hash.
 *
 * The count is read back out of /etc/shadow, so it is attacker-controlled the
 * moment anything can write that file. Without a ceiling a single line reading
 * "$v1$2000000000$..." pins the CPU inside the kernel for hours.
 */
#define PBKDF2_MAX_ITERATIONS     10000000u
#define PBKDF2_MIN_SALT_LEN       16
#define PBKDF2_OUTPUT_LEN         32

/**
 * @file pbkdf2.h
 * @brief PBKDF2-HMAC-SHA256 key derivation function (RFC 2898)
 */

/**
 * @brief Derives a cryptographic key from a password using PBKDF2-HMAC-SHA256.
 *
 * Implements RFC 2898 Section 5.2 using HMAC-SHA256 as the PRF.
 * Single block output (dkLen <= 32 bytes).
 *
 * @param password     Password bytes
 * @param password_len Password length in bytes
 * @param salt         Salt bytes (minimum PBKDF2_MIN_SALT_LEN bytes)
 * @param salt_len     Salt length in bytes
 * @param iterations   Iteration count (minimum PBKDF2_MIN_ITERATIONS)
 * @param output       Output buffer for derived key
 * @param output_len   Desired output length in bytes (max 32)
 */
/**
 * @brief Checks a stored iteration count against this build's accepted range.
 *
 * @param iterations Count parsed from a shadow entry.
 * @return 1 when the count is usable, 0 when the entry must be rejected.
 */
int pbkdf2_iterations_are_acceptable(uint32_t iterations);

void pbkdf2_hmac_sha256(
    const uint8_t *password, uint32_t password_len,
    const uint8_t *salt, uint32_t salt_len,
    uint32_t iterations,
    uint8_t *output, uint32_t output_len
);

#endif // PBKDF2_H
