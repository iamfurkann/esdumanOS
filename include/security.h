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
 * @brief Initializes the ELF encryption master key from build-time constant.
 */
void init_elf_master_key(void);


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


// --- Added by Refactor Script 2 ---
extern uint32_t auth_fail_ticks[16];

#endif // SECURITY_H