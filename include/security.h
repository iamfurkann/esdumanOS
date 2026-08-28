#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

// Password hashes are no longer hardcoded. They are generated at first boot
// using PBKDF2-HMAC-SHA256 and stored in /etc/shadow.

/**
 * @brief Enumeration of system security levels.
 * Controls the global security behavior of the kernel.
 */
typedef enum {
    SEC_LEVEL_NORMAL          = 0, // Standard operation (Encryption is optional)
    SEC_LEVEL_CRYPTO_ENFORCED = 1, // ALL reads/writes on VFS MUST be encrypted
    SEC_LEVEL_LOCKDOWN        = 2, // No new tasks can be started, Key in RAM is erased (Zeroized)
    SEC_LEVEL_IMMUTABLE       = 3  // Writing to disk is completely disabled, Kernel enters "Read-Only" mode
} security_level_t;

extern security_level_t current_sec_level;

/**
 * @brief Sets the system's global security level.
 * @param level The target security level to apply.
 */
void set_security_level(security_level_t level);

/**
 * @brief Loads the key the build encrypted the embedded programs with.
 *
 * This was init_elf_master_key(), and it filled kernel_master_key - which made
 * one key do two unrelated jobs. tools/encrypt_tool encrypts every user program
 * at build time with ESDUMAN_ELF_KEY_HEX, and the results are embedded in the
 * kernel image as init_elf[], sh_elf[] and the rest; that key has to be in the
 * kernel because the ciphertext is in the kernel. What it must not also be is
 * the key the user's files are encrypted under, because a key that ships inside
 * the binary protects nothing once somebody has the binary - the old comment
 * here said exactly that, and then used it for the file system anyway.
 *
 * It now decrypts only bytes that arrived in the same image it did. See
 * elf_asset_key below and fs_install_image_asset() in fs.h.
 */
void init_image_asset_key(void);

/**
 * @brief The build-time key the embedded programs were encrypted with.
 *
 * Not a secret from anybody holding the kernel image, and not treated as one:
 * it never touches a file the user created, and it is not destroyed on LOCKDOWN
 * because there is nothing to destroy - the same bytes are sitting in the
 * .rodata of the running kernel either way.
 */
extern uint8_t elf_asset_key[32];

/**
 * @brief Whether the embedded-asset key was parsed successfully.
 * @return 1 when the programs in the image can be decrypted, 0 otherwise.
 */
int elf_asset_key_available(void);

/**
 * @brief Hands the file system's data key to the kernel.
 *
 * Called once per boot, after the passphrase has opened the key slot, and again
 * by nothing - the data key does not change when the passphrase does, which is
 * the entire reason the slot wraps a key rather than being one.
 *
 * @param key The 32-byte data key recovered from the slot.
 */
void install_master_key(const uint8_t key[32]);


// --- Added by Refactor Script ---
extern uint8_t kernel_master_key[32];

/**
 * @brief Whether the in-RAM master key still holds real key material.
 * @return 1 while the key is usable, 0 once it has been destroyed.
 */
int kernel_master_key_available(void);

/**
 * @brief Whether the VFS can perform encrypted I/O right now.
 *
 * True either because encryption is not required at the current security level,
 * or because it is required and the key is still present. False means encrypted
 * operations must be refused rather than attempted with a zeroed key.
 */
int crypto_fs_key_is_usable(void);
extern int validate_user_pointer(const void *ptr, size_t size);
extern void init_security(void *mboot_ptr);
extern int verify_user_password(const char *username, const char *password);
extern int create_shadow_entry(const char *username, const char *password, int uid, char *out_buf, int buf_size);


/*
 * "extern uint32_t auth_fail_ticks[16];" used to sit here and had no definition
 * anywhere - a leftover from when the lockout counter was a global table indexed
 * by pid. It is a per-process field now (process_t::auth_fail_ticks), which is
 * what every user in sys_sec.c actually reads. Left in place it invited someone
 * to write auth_fail_ticks[pid] and get a link error, or worse, to read it in
 * review as though the global still existed.
 */

#endif // SECURITY_H