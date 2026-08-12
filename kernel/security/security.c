/*
 * File: security.c
 * Purpose: Security policies, master key generation, and protection levels.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "security.h"
#include "stdio.h"
#include "io.h"
#include "klog.h" 
#include "crypto.h"
#include "entropy.h"

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
} local_multiboot_info_t;

security_level_t current_sec_level = SEC_LEVEL_CRYPTO_ENFORCED;

uint8_t kernel_master_key[32];

/*
 * Whether kernel_master_key holds real key material.
 *
 * The key is destroyed on LOCKDOWN, but LOCKDOWN outranks CRYPTO_ENFORCED, so
 * the "level >= CRYPTO_ENFORCED" test that selects the encrypted VFS path stays
 * true afterwards. Without this flag every read after lockdown decrypted with
 * an all-zero key and every write encrypted with one, quietly destroying the
 * filesystem instead of reporting that it had become inaccessible.
 */
static int master_key_available = 0;

int kernel_master_key_available(void) {
    return master_key_available;
}

int crypto_fs_key_is_usable(void) {
    /* Below CRYPTO_ENFORCED the VFS stores plaintext, so the key is irrelevant. */
    if (current_sec_level < SEC_LEVEL_CRYPTO_ENFORCED) return 1;
    return master_key_available;
}

/*
 * generate_random_bytes() lives in kernel/security/entropy.c now. It used to
 * read every one of its inputs at the moment of the call - RDTSC, three RTC
 * registers, the address of a local - which is why it was worth only twenty or
 * thirty bits. It is now backed by a pool fed from interrupt timing, and it
 * guarantees uniqueness per extraction independently of how good those sources
 * turned out to be. See entropy.h for what that does and does not promise.
 */

void bytes_to_hex(const uint8_t *bytes, uint32_t len, char *hex_out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++) {
        hex_out[i * 2]     = hex_chars[(bytes[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_chars[bytes[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}

int hex_to_bytes(const char *hex, uint8_t *out, uint32_t out_len) {
    for (uint32_t i = 0; i < out_len; i++) {
        uint8_t hi_nibble, lo_nibble;
        char ch = hex[i * 2];
        char cl = hex[i * 2 + 1];
        if (ch == '\0' || cl == '\0') return -1;
        if (ch >= '0' && ch <= '9') hi_nibble = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi_nibble = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi_nibble = ch - 'A' + 10;
        else return -1;
        if (cl >= '0' && cl <= '9') lo_nibble = cl - '0';
        else if (cl >= 'a' && cl <= 'f') lo_nibble = cl - 'a' + 10;
        else if (cl >= 'A' && cl <= 'F') lo_nibble = cl - 'A' + 10;
        else return -1;
        out[i] = (hi_nibble << 4) | lo_nibble;
    }
    return 0;
}

/**
 * @brief Initializes the ELF encryption master key from the build-time constant.
 *
 * Converts the ELF_ENCRYPTION_KEY hex string (compiled into the kernel via
 * -DELF_ENCRYPTION_KEY=...) directly into the 32-byte kernel_master_key.
 *
 * NOTE: This key is embedded in the kernel binary. This provides tamper
 * resistance (making it harder to extract/modify encrypted assets), but
 * NOT at-rest confidentiality. Anyone who can read the kernel binary can
 * extract this key.
 */
void init_elf_master_key(void) {
#ifdef ELF_ENCRYPTION_KEY
    const char *hex_key = ELF_ENCRYPTION_KEY;
    if (hex_to_bytes(hex_key, kernel_master_key, 32) != 0) {
        klog(LOG_LEVEL_ERROR, "SEC", "Failed to parse ELF_ENCRYPTION_KEY! Invalid hex.");
        // Zero the key on failure
        volatile uint8_t *p = kernel_master_key;
        for (int i = 0; i < 32; i++) p[i] = 0;
        master_key_available = 0;
        return;
    }
    master_key_available = 1;
    klog(LOG_LEVEL_INFO, "SEC", "ELF encryption key loaded from build-time constant.");
    klog(LOG_LEVEL_WARN, "SEC", "NOTE: Key is embedded in kernel binary. Tamper resistance only, NOT at-rest confidentiality.");
#else
    klog(LOG_LEVEL_ERROR, "SEC", "No ELF_ENCRYPTION_KEY defined! Encrypted binaries will fail to load.");
    volatile uint8_t *p = kernel_master_key;
    for (int i = 0; i < 32; i++) p[i] = 0;
    master_key_available = 0;
#endif
}
/**
 * @brief init_security
 * @param mboot_ptr
 */
void init_security(void *mboot_ptr) {
    (void)mboot_ptr;

    /*
     * Seed the entropy pool before anything can ask for random bytes.
     * generate_random_bytes() refuses outright until this has run, so the order
     * matters: /etc/shadow creation and the first encrypted write both come
     * later in the boot, and both would fail rather than use a zeroed pool.
     */
    entropy_init();

    if (entropy_quality() == ENTROPY_OK) {
        klog(LOG_LEVEL_INFO, "SEC", "Security subsystem initialized (RDRAND available).");
    } else {
        klog(LOG_LEVEL_WARN, "SEC",
             "Security subsystem initialized WITHOUT RDRAND. Entropy is not "
             "cryptographic; IVs and salts stay unique by construction.");
    }
}

/**
 * @brief set_security_level
 * @param level
 */
void set_security_level(security_level_t level) {
    if (level < current_sec_level) {
        klog(LOG_LEVEL_ERROR, "SEC", "Security level cannot be downgraded!");
        return;
    }
    
    current_sec_level = level;
    
    switch (level) {
        case SEC_LEVEL_LOCKDOWN:
            klog(LOG_LEVEL_CRITICAL, "SEC", "LOCKDOWN MODE ACTIVE! New processes blocked.");
            {
                volatile uint8_t *key_ptr = kernel_master_key;
                for (int i = 0; i < 32; i++) {
                    key_ptr[i] = 0;
                }
                master_key_available = 0;
                klog(LOG_LEVEL_INFO, "SEC", "Master Key safely destroyed from RAM (Zeroized)!");
                klog(LOG_LEVEL_CRITICAL, "SEC", "Encrypted filesystem access is now refused, not silently downgraded.");
            }
            break;

        case SEC_LEVEL_CRYPTO_ENFORCED:
            klog(LOG_LEVEL_INFO, "SEC", "CRYPTO-ENFORCED MODE ACTIVE! Encrypted disk access enabled.");
            break;

        case SEC_LEVEL_IMMUTABLE:
            klog(LOG_LEVEL_INFO, "SEC", "IMMUTABLE MODE ACTIVE! Disk is READ-ONLY.");
            break;

        default:
            break;
    }
}