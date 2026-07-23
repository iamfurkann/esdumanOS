#ifndef _AES_H_
#define _AES_H_

#include "types.h"

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES encryption in CBC-mode of operation.
// CTR enables encryption in counter-mode.
// ECB enables the basic ECB 16-byte block algorithm. All can be enabled simultaneously.

// The #ifndef-guard allows it to be configured before #include'ing or at compile time.
/**
 * @brief Enables AES encryption in Cipher Block Chaining (CBC) mode.
 */
#ifndef CBC
  #define CBC 1
#endif

/**
 * @brief Enables AES encryption in Electronic Codebook (ECB) mode.
 */
#ifndef ECB
  #define ECB 1
#endif

/**
 * @brief Enables AES encryption in Counter (CTR) mode.
 */
#ifndef CTR
  #define CTR 1
#endif

//#define AES128 1
//#define AES192 1

/**
 * @brief Enables AES-256 (256-bit key).
 */
#define AES256 1

/**
 * @brief AES block length in bytes. AES always uses a 128-bit (16-byte) block.
 */
#define AES_BLOCKLEN 16

#if defined(AES256) && (AES256 == 1)
    /** @brief Key length in bytes for AES-256. */
    #define AES_KEYLEN 32
    /** @brief Key expansion size in bytes for AES-256. */
    #define AES_keyExpSize 240
#elif defined(AES192) && (AES192 == 1)
    /** @brief Key length in bytes for AES-192. */
    #define AES_KEYLEN 24
    /** @brief Key expansion size in bytes for AES-192. */
    #define AES_keyExpSize 208
#else
    /** @brief Key length in bytes for AES-128. */
    #define AES_KEYLEN 16
    /** @brief Key expansion size in bytes for AES-128. */
    #define AES_keyExpSize 176
#endif

/**
 * @brief AES context structure holding the round keys and initialization vector (IV).
 */
struct AES_ctx
{
  uint8_t RoundKey[AES_keyExpSize];
#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
  uint8_t Iv[AES_BLOCKLEN];
#endif
};

/**
 * @brief Initializes the AES context with the given key.
 * @param ctx Pointer to the AES context.
 * @param key Pointer to the cryptographic key.
 */
void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key);

#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
/**
 * @brief Initializes the AES context with the given key and Initialization Vector (IV).
 * @param ctx Pointer to the AES context.
 * @param key Pointer to the cryptographic key.
 * @param iv Pointer to the Initialization Vector.
 */
void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv);

/**
 * @brief Sets the Initialization Vector (IV) in the AES context.
 * @param ctx Pointer to the AES context.
 * @param iv Pointer to the Initialization Vector.
 */
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv);
#endif

#if defined(ECB) && (ECB == 1)
/**
 * @brief Encrypts a single block using AES in ECB mode.
 * @note ECB mode is considered insecure for most uses.
 * @param ctx Pointer to the initialized AES context.
 * @param buf Buffer containing exactly AES_BLOCKLEN bytes to encrypt in place.
 */
void AES_ECB_encrypt(const struct AES_ctx* ctx, uint8_t* buf);

/**
 * @brief Decrypts a single block using AES in ECB mode.
 * @note ECB mode is considered insecure for most uses.
 * @param ctx Pointer to the initialized AES context.
 * @param buf Buffer containing exactly AES_BLOCKLEN bytes to decrypt in place.
 */
void AES_ECB_decrypt(const struct AES_ctx* ctx, uint8_t* buf);
#endif // #if defined(ECB) && (ECB == !)

#if defined(CBC) && (CBC == 1)
/**
 * @brief Encrypts a buffer using AES in CBC mode.
 * @param ctx Pointer to the initialized AES context (must have IV set).
 * @param buf Buffer to encrypt in place.
 * @param length Length of the buffer in bytes (must be a multiple of AES_BLOCKLEN).
 */
void AES_CBC_encrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);

/**
 * @brief Decrypts a buffer using AES in CBC mode.
 * @param ctx Pointer to the initialized AES context (must have IV set).
 * @param buf Buffer to decrypt in place.
 * @param length Length of the buffer in bytes (must be a multiple of AES_BLOCKLEN).
 */
void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);
#endif // #if defined(CBC) && (CBC == 1)

#if defined(CTR) && (CTR == 1)
/**
 * @brief Encrypts or decrypts a buffer using AES in CTR mode.
 * @param ctx Pointer to the initialized AES context (must have IV set).
 * @param buf Buffer to process in place.
 * @param length Length of the buffer in bytes.
 */
void AES_CTR_xcrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length);

#endif // #if defined(CTR) && (CTR == 1)


#endif // _AES_H_