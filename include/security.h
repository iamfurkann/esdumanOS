#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

// Define the default kernel password hash (SHA-256 of "1234")
#ifndef KERNEL_PASSWORD_HASH
#define KERNEL_PASSWORD_HASH "03ac674216f3e15c761ee1a5e255f067953623c8b388b4459e13f978d7c846f4"
#endif

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
 * @brief Derives a master cryptographic key from a provided password.
 * @param password The null-terminated password string.
 */
void derive_master_key(const char *password);


// --- Added by Refactor Script ---
extern uint8_t kernel_master_key[32];
extern int validate_user_pointer(const void *ptr, size_t size);
extern void init_security(void *mboot_ptr);
extern int verify_user_password(const char *username, const char *password);


// --- Added by Refactor Script 2 ---
extern uint32_t auth_fail_ticks[16];

#endif // SECURITY_H