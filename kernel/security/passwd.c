/*
 * File: passwd.c
 * Purpose: User password verification and shadow file management.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "types.h"
#include "fs.h"
#include "stdio.h"
#include "errno.h"
#include "klog.h"
#include "crypto.h"
#include "libft.h"
/**
 * @brief Performs a constant-time comparison of two strings.
 *
 * @param s1 The first string to compare.
 * @param s2 The second string to compare.
 * @return 0 if the strings are identical, non-zero otherwise.
 */
static int constant_time_cmp(const char *s1, const char *s2) {
    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (s1[i] ^ s2[i]);
    }
    return diff;
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
            char db_hash[70] = {0};
            char db_uid[16] = {0};

            int colon = 0, k = 0;
            for (int j = line_start; j < i; j++) {
                if (buf[j] == ':') { colon++; k = 0; continue; }
                if (colon == 0 && k < 31) db_user[k++] = buf[j];
                else if (colon == 1 && k < 69) db_hash[k++] = buf[j];
                else if (colon == 2 && k < 15) db_uid[k++] = buf[j];
            }
if (ft_strcmp(username, db_user) == 0) {
                char computed_hash[65];
                char salted_pass[256];
                
                int sp_idx = 0;
                
                // [FIX]: Random system salt has been REMOVED.
                // Random salt does not match previously generated hashes.
                // We only use the username as the salt.
                for(int u = 0; username[u] && sp_idx < 95; u++) 
                    salted_pass[sp_idx++] = username[u];
                    
                salted_pass[sp_idx++] = ':';
                
                for(int p = 0; password[p] && sp_idx < 254; p++) 
                    salted_pass[sp_idx++] = password[p];
                    
                salted_pass[sp_idx] = '\0';

                // KDF Iteration Fix: Similar to the master key, user passwords should
                // be hashed repeatedly to prevent brute-force attacks. However,
                // the hashes in kernel.c use single-round SHA256. If you do not want
                // to change the hashes in kernel.c, this should remain a single round.
                sha256_to_hex(salted_pass, computed_hash);
                
                if (constant_time_cmp(db_hash, computed_hash) == 0) {
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

/**
 * @brief Updates the /etc/passwd file with new content.
 *
 * @param new_passwd_content The new content to write to the passwd file.
 * @param size The size of the new content in bytes.
 * @return 0 on success, or a negative error code on failure.
 */
int update_passwd_file(const char *new_passwd_content, uint32_t size) {
int etc_idx = fs_get_entry_idx("etc", 0);
    if (etc_idx == -1) {
        klog(LOG_LEVEL_ERROR, "PASSWD", "/etc directory not found for updating passwd");
        return E_NOENT;
    }
int etc_id = dir_table[etc_idx].entry_id;
return fs_atomic_update("passwd", (const uint8_t *)new_passwd_content, size, etc_id);
}