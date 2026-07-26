#ifndef HMAC_H
#define HMAC_H

#include "types.h"

/**
 * @brief Computes the HMAC-SHA256 of the given data.
 * 
 * @param key Pointer to the cryptographic key.
 * @param key_len Length of the key in bytes.
 * @param data Pointer to the input data.
 * @param data_len Length of the input data in bytes.
 * @param out Buffer to store the 32-byte HMAC-SHA256 result.
 */
void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out);

#endif
