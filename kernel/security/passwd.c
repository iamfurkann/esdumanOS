#include "types.h"
#include "fs.h"
#include "stdio.h"

extern int ft_strcmp(const char *s1, const char *s2);
extern disk_file_entry_t dir_table[];
extern void sha256_to_hex(const char *input, char *output_hex);

int verify_user_password(const char *username, const char *password) {
    extern int fs_get_entry_idx(const char *, uint8_t);
    int etc_idx = fs_get_entry_idx("etc", 0);
    if (etc_idx == -1) return -1;

    int etc_id = dir_table[etc_idx].entry_id;
    vfs_file_t p_file;
    extern int fs_open(const char *, uint8_t, vfs_file_t *);

    if (fs_open("shadow", etc_id, &p_file) != 0) return -1; 

    char buf[1024];
    extern int fs_read(vfs_file_t *, uint8_t *, uint32_t);
    int bytes = fs_read(&p_file, (uint8_t *)buf, 1023);
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
                sha256_to_hex(password, computed_hash);
                
                if (ft_strcmp(db_hash, computed_hash) == 0) {
                    int uid = 0;
                    for(int u = 0; db_uid[u] != '\0'; u++) {
                        uid = (uid * 10) + (db_uid[u] - '0');
                    }
                    return uid;
                } else {
                    return -1;
                }
            }
            line_start = i + 1;
        }
        if (buf[i] == '\0') break;
    }
    return -1;
}