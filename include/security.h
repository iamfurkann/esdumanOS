#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

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

#endif // SECURITY_H