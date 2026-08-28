#ifndef KEYSLOT_H
#define KEYSLOT_H

#include "types.h"

/*
 * The passphrase-protected key slot that guards the file system's data key.
 *
 * Until v1.1.0 the file system was encrypted under a key compiled into the
 * kernel through -DELF_ENCRYPTION_KEY. The comment above that code said what
 * that was worth: tamper resistance, and explicitly "NOT at-rest
 * confidentiality. Anyone who can read the kernel binary can extract this key."
 * That is fine for an ISO you boot in an emulator and useless for a machine you
 * carry around, which is what this release is aimed at.
 *
 * Two levels, not one. The passphrase derives a key-encryption key (KEK), the
 * KEK wraps a randomly generated data-encryption key (DEK), and the DEK is what
 * every file is encrypted under. A single level would tie the passphrase
 * directly to the file contents, so changing it would mean decrypting and
 * re-encrypting the entire disk - a job that cannot be made atomic and that
 * fails halfway with the disk in neither state. Re-wrapping is one sector.
 *
 * The slot is stored in the superblock, which is why it is packed and why its
 * size is asserted: it is on-disk layout, not a runtime convenience.
 */

/** @brief Salt bytes fed to PBKDF2. Well above PBKDF2_MIN_SALT_LEN. */
#define KEYSLOT_SALT_LEN 32
/** @brief AES-CBC initialisation vector for the wrap. */
#define KEYSLOT_IV_LEN   16
/** @brief Length of both the KEK and the DEK. AES-256 either way. */
#define KEYSLOT_KEY_LEN  32
/** @brief HMAC-SHA256 output length. */
#define KEYSLOT_TAG_LEN  32

/**
 * @brief Longest passphrase accepted.
 *
 * The same 64 the account passwords use, and for the same reason: it is what
 * early_read_password() fills, and a limit the two halves disagree about is a
 * passphrase that can be set and then never entered again.
 */
#define KEYSLOT_MAX_PASSPHRASE 64

/**
 * @brief One passphrase's worth of access to the data key.
 *
 * There is exactly one of these. A second slot - so that two passphrases could
 * open the same disk, or so that a recovery key could exist - is a deliberate
 * omission in v1.1.0 and is written down as a limitation rather than left to be
 * discovered: forgetting the passphrase means losing the disk.
 */
typedef struct {
    uint8_t  salt[KEYSLOT_SALT_LEN];    /**< PBKDF2 salt, fresh per wrap.      */
    uint32_t iterations;                /**< PBKDF2 count this slot was made with. */
    uint8_t  iv[KEYSLOT_IV_LEN];        /**< CBC IV for the wrap, fresh per wrap.  */
    uint8_t  wrapped[KEYSLOT_KEY_LEN];  /**< The DEK under AES-256-CBC(KEK).   */
    uint8_t  tag[KEYSLOT_TAG_LEN];      /**< HMAC-SHA256(KEK, salt||iv||wrapped). */
} __attribute__((packed)) keyslot_t;

/**
 * @brief Bytes a keyslot_t occupies on disk. Asserted by the test suite.
 *
 * Written out rather than left to sizeof so that a field added without thinking
 * about the superblock fails a test instead of quietly moving every byte after
 * it. The superblock has its own assertion for the same reason.
 */
#define KEYSLOT_DISK_SIZE 116

/**
 * @brief Builds a fresh slot around a data key.
 *
 * Generates a new salt and IV, derives the KEK from the passphrase, wraps the
 * DEK and computes the tag. Used both when a blank disk is formatted and when
 * the passphrase is changed - the difference is only whether the caller passes a
 * newly generated DEK or the one it just unwrapped.
 *
 * @param slot       Receives the completed slot.
 * @param passphrase Null-terminated, at most KEYSLOT_MAX_PASSPHRASE-1 bytes.
 * @param dek        The data key to protect.
 * @return E_OK, E_INVAL for an empty or over-long passphrase, or E_IO when the
 *         entropy pool could not supply a salt and IV.
 */
int keyslot_create(keyslot_t *slot, const char *passphrase, const uint8_t dek[KEYSLOT_KEY_LEN]);

/**
 * @brief Recovers the data key from a slot.
 *
 * The tag is what decides. A wrong passphrase produces a wrong KEK, a wrong KEK
 * produces a different tag, and the comparison is constant-time - a wrapped key
 * that leaked how many leading bytes of the tag were right would be a wrapped
 * key an attacker could open one byte at a time.
 *
 * The tag covers the salt and the IV as well as the ciphertext, so a slot whose
 * parameters were edited fails here rather than unwrapping to a different key.
 *
 * @param slot       The slot, as read from the superblock.
 * @param passphrase Null-terminated.
 * @param dek_out    Receives the data key on success; untouched otherwise.
 * @return E_OK, E_ACCES when the tag does not match, or E_INVAL when the slot's
 *         iteration count is outside what this build will accept.
 */
int keyslot_unwrap(const keyslot_t *slot, const char *passphrase, uint8_t dek_out[KEYSLOT_KEY_LEN]);

/**
 * @brief Whether a slot holds anything at all.
 *
 * A slot of all zeroes is what a disk formatted by an older kernel would appear
 * to have, since save_superblock() zeroes the tail of the sector. This kernel
 * refuses such a disk by version rather than trusting this test, but the test
 * exists so that "no passphrase set" can never be mistaken for a valid slot
 * whose tag happens to be zero.
 *
 * @param slot The slot.
 * @return 1 when the slot carries key material, 0 when it is empty.
 */
int keyslot_is_present(const keyslot_t *slot);

#endif // KEYSLOT_H
