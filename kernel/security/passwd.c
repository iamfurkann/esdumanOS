/*
 * File: passwd.c
 * Purpose: User password verification and shadow file management.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "types.h"
#include "fs.h"
#include "errno.h"
#include "klog.h"
#include "crypto.h"
#include "libft.h"
#include "pbkdf2.h"
#include "entropy.h"

static int constant_time_cmp_bytes(const uint8_t *a, const uint8_t *b, uint32_t len) {
    volatile int diff = 0;
    for (uint32_t i = 0; i < len; i++) {
        diff |= (a[i] ^ b[i]);
    }
    return diff;
}

static void uid_to_str(int uid, char *buf) {
    if (uid == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int temp = uid;
    int len = 0;
    while (temp > 0) {
        len++;
        temp /= 10;
    }
    buf[len] = '\0';
    temp = uid;
    for (int i = len - 1; i >= 0; i--) {
        buf[i] = (temp % 10) + '0';
        temp /= 10;
    }
}

static uint32_t simple_strlen(const char *str) {
    uint32_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

static void simple_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

/**
 * @brief Derives the salt for one shadow entry.
 *
 * A salt has to be unique - that is the whole of its job, since it defeats
 * precomputed tables and stops two users with the same password from producing
 * the same hash. It does not have to be secret. Uniqueness is therefore what is
 * guaranteed here, and it is guaranteed without depending on entropy quality:
 * the pool's monotonic extraction counter makes every draw distinct, and folding
 * the username in means two different accounts cannot collide even if the pool
 * were producing a constant.
 *
 * @param username Account name, mixed in for cross-account separation.
 * @param salt_out 16-byte output.
 * @return 0 on success, -1 when the entropy pool refuses to serve.
 */
static int derive_shadow_salt(const char *username, uint8_t salt_out[16]) {
    uint8_t pool_bytes[32];
    int quality = generate_random_bytes(pool_bytes, sizeof(pool_bytes));

    /*
     * ENTROPY_FAIL means the pool was never seeded, so the counter that
     * guarantees uniqueness has not been initialised either. A shadow entry with
     * a possibly-repeated salt is worse than no entry at all: it would look valid
     * forever while quietly sharing a salt with another account.
     */
    if (quality == ENTROPY_FAIL) return -1;

    if (quality == ENTROPY_WEAK) {
        /*
         * Once per boot, not once per account - a warning that repeats on every
         * call is a warning nobody reads.
         */
        static int warned = 0;
        if (!warned) {
            warned = 1;
            klog(LOG_LEVEL_WARN, "SEC",
                 "Shadow salts derived without RDRAND: unique by construction, "
                 "but not unpredictable. Not cryptographic-grade entropy.");
        }
    }

    sha256_ctx_t ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, pool_bytes, sizeof(pool_bytes));
    sha256_update(&ctx, (const uint8_t *)username, simple_strlen(username));
    sha256_final(&ctx, digest);

    for (int i = 0; i < 16; i++) salt_out[i] = digest[i];

    ft_memset(pool_bytes, 0, sizeof(pool_bytes));
    ft_memset(digest, 0, sizeof(digest));
    return 0;
}

/**
 * @brief Creates a new shadow file entry with PBKDF2-HMAC-SHA256.
 * Generates random salt, computes PBKDF2 hash, formats as $v1$ shadow line.
 *
 * @param username Username
 * @param password Plaintext password
 * @param uid User ID
 * @param out_buf Output buffer for the shadow line
 * @param buf_size Size of output buffer
 * @return 0 on success, negative on error
 */
int create_shadow_entry(const char *username, const char *password, int uid, char *out_buf, int buf_size) {
    uint8_t salt[16];
    uint8_t dk[32];
    char salt_hex[33];
    char dk_hex[65];
    char uid_str[16];

    if (derive_shadow_salt(username, salt) != 0) return -1;

    pbkdf2_hmac_sha256((const uint8_t *)password, simple_strlen(password), salt, 16, PBKDF2_DEFAULT_ITERATIONS, dk, 32);

    bytes_to_hex(salt, 16, salt_hex);
    bytes_to_hex(dk, 32, dk_hex);
    uid_to_str(uid, uid_str);
    
    out_buf[0] = '\0';
    
    const char *v1_prefix = ":$v1$";
    // Convert iteration count to string dynamically
    char iters_buf[16];
    int iter_val = PBKDF2_DEFAULT_ITERATIONS;
    int iter_pos = 0;
    if (iter_val == 0) { iters_buf[iter_pos++] = '0'; }
    else {
        char tmp[16]; int tmp_pos = 0;
        while (iter_val > 0) { tmp[tmp_pos++] = '0' + (iter_val % 10); iter_val /= 10; }
        while (tmp_pos > 0) { iters_buf[iter_pos++] = tmp[--tmp_pos]; }
    }
    iters_buf[iter_pos++] = '$';
    iters_buf[iter_pos] = '\0';
    const char *iters = iters_buf;
    
    uint32_t total_len = simple_strlen(username) + simple_strlen(v1_prefix) + simple_strlen(iters) + 32 + 1 + 64 + 1 + simple_strlen(uid_str);
    if ((int)total_len >= buf_size) {
        return -1;
    }
    
    int pos = 0;
    simple_strcpy(out_buf + pos, username); pos += simple_strlen(username);
    simple_strcpy(out_buf + pos, v1_prefix); pos += simple_strlen(v1_prefix);
    simple_strcpy(out_buf + pos, iters); pos += simple_strlen(iters);
    simple_strcpy(out_buf + pos, salt_hex); pos += 32;
    out_buf[pos++] = '$';
    simple_strcpy(out_buf + pos, dk_hex); pos += 64;
    out_buf[pos++] = ':';
    simple_strcpy(out_buf + pos, uid_str); pos += simple_strlen(uid_str);
    out_buf[pos] = '\0';
    
    volatile uint8_t *p = salt; for (int i = 0; i < 16; i++) p[i] = 0;
    p = dk; for (int i = 0; i < 32; i++) p[i] = 0;
    
    return 0;
}

/**
 * @brief Verifies a user's password against the /etc/shadow file.
 *
 * @param username The name of the user.
 * @param password The provided password to verify.
 * @return The user ID (UID) on successful verification, or a negative error code.
 */
int verify_user_password(const char *username, const char *password) {
    int etc_idx = fs_get_entry_idx("etc", 0);
    if (etc_idx == -1) {
        klog(LOG_LEVEL_ERROR, "PASSWD", "/etc directory not found!");
        return E_NOENT;
    }

    int etc_id = dir_table[etc_idx].entry_id;
    vfs_file_t p_file;
    if (fs_open("shadow", etc_id, &p_file) != 0) {
        klog(LOG_LEVEL_ERROR, "PASSWD", "/etc/shadow file missing or inaccessible!");
        return E_NOENT; 
    }

    char buf[1024];
    int bytes = fs_read(&p_file, (uint8_t *)buf, 1023);

    if (bytes < 0) {
        klog(LOG_LEVEL_ERROR, "PASSWD", "Failed to read /etc/shadow file!");
        return E_IO;
    }

    buf[bytes] = '\0';

    int line_start = 0;
    for (int i = 0; i <= bytes; i++) {
        if (buf[i] == '\n' || buf[i] == '\0') {
            char db_user[32] = {0};
            char db_hash[150] = {0};
            char db_uid[16] = {0};

            int colon = 0, k = 0;
            for (int j = line_start; j < i; j++) {
                if (buf[j] == ':') { colon++; k = 0; continue; }
                if (colon == 0 && k < 31) db_user[k++] = buf[j];
                else if (colon == 1 && k < 149) db_hash[k++] = buf[j];
                else if (colon == 2 && k < 15) db_uid[k++] = buf[j];
            }
            
            if (ft_strcmp(username, db_user) == 0) {
                if (db_hash[0] != '$' || db_hash[1] != 'v' || db_hash[2] != '1' || db_hash[3] != '$') {
                    klog(LOG_LEVEL_ERROR, "PASSWD", "Invalid shadow format");
                    return E_INVAL;
                }
                
                int iter_start = 4;
                int iter_end = iter_start;
                while (db_hash[iter_end] != '$' && db_hash[iter_end] != '\0') iter_end++;
                
                if (db_hash[iter_end] != '$') return E_INVAL;
                
                /*
                 * The iteration count is read back out of /etc/shadow, so it is
                 * attacker-controlled the moment anything can write that file.
                 * Bound the digit count before parsing - ten digits overflow a
                 * 32-bit accumulator - and the value afterwards. A line reading
                 * "$v1$2000000000$..." would otherwise pin the CPU inside the
                 * kernel for hours: interrupts stay on, but nothing else can be
                 * scheduled while a syscall is running.
                 */
                int iter_digits = iter_end - iter_start;
                if (iter_digits <= 0 || iter_digits > 8) return E_INVAL;

                uint32_t iterations = 0;
                for (int j = iter_start; j < iter_end; j++) {
                    if (db_hash[j] < '0' || db_hash[j] > '9') return E_INVAL;
                    iterations = iterations * 10 + (uint32_t)(db_hash[j] - '0');
                }

                if (!pbkdf2_iterations_are_acceptable(iterations)) {
                    klog_int(LOG_LEVEL_ERROR, "PASSWD",
                             "Shadow entry rejected: iteration count outside the accepted range",
                             (int)iterations);
                    return E_INVAL;
                }
                
                int salt_start = iter_end + 1;
                int salt_end = salt_start;
                while (db_hash[salt_end] != '$' && db_hash[salt_end] != '\0') salt_end++;
                
                if (db_hash[salt_end] != '$' || (salt_end - salt_start) != 32) return E_INVAL;
                
                int dk_start = salt_end + 1;
                int dk_end = dk_start;
                while (db_hash[dk_end] != '\0') dk_end++;
                
                if ((dk_end - dk_start) != 64) return E_INVAL;
                
                char salt_hex[33] = {0};
                for (int j = 0; j < 32; j++) salt_hex[j] = db_hash[salt_start + j];
                
                char dk_hex[65] = {0};
                for (int j = 0; j < 64; j++) dk_hex[j] = db_hash[dk_start + j];
                
                uint8_t salt_bytes[16];
                uint8_t stored_dk[32];
                uint8_t computed_dk[32];
                
                if (hex_to_bytes(salt_hex, salt_bytes, 16) != 0) return E_INVAL;
                if (hex_to_bytes(dk_hex, stored_dk, 32) != 0) return E_INVAL;
                
                pbkdf2_hmac_sha256((const uint8_t *)password, simple_strlen(password), salt_bytes, 16, iterations, computed_dk, 32);
                
                int match = constant_time_cmp_bytes(stored_dk, computed_dk, 32);
                
                volatile uint8_t *p = salt_bytes; for (int j = 0; j < 16; j++) p[j] = 0;
                p = stored_dk; for (int j = 0; j < 32; j++) p[j] = 0;
                p = computed_dk; for (int j = 0; j < 32; j++) p[j] = 0;
                
                if (match == 0) {
                    int uid = 0;
                    for(int u = 0; db_uid[u] != '\0'; u++) {
                        uid = (uid * 10) + (db_uid[u] - '0');
                    }
                    return uid;
                } else {
                    klog(LOG_LEVEL_WARN, "PASSWD", "Invalid password provided for user");
                    return E_ACCES;
                }
            }
            line_start = i + 1;
        }
        if (buf[i] == '\0') break;
    }
    klog(LOG_LEVEL_WARN, "PASSWD", "User not found in shadow file");
    return E_NOENT;
}

/*
 * update_passwd_file() was here and is gone.
 *
 * It rewrote /etc/passwd through fs_atomic_update() and had no caller: there is
 * no useradd and no passwd command, and the accounts this system has are created
 * once at first boot. It was also declared in no header, so nothing outside this
 * file could have called it without a warning.
 *
 * Worth removing rather than leaving: it took a buffer and a length and wrote the
 * password database with no check of any kind - not the caller's uid, not the
 * file's mode, not the security level. Dead code that would have been a hole the
 * day somebody revived it, which is the worst kind to leave lying about.
 */