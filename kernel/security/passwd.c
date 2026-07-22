#include "types.h"
#include "fs.h"
#include "stdio.h"

extern disk_file_entry_t dir_table[];
extern void sha256_to_hex(const char *input, char *output_hex);

static int constant_time_cmp(const char *s1, const char *s2) {
    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (s1[i] ^ s2[i]);
    }
    return diff;
}

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

    if (bytes < 0) {
        return -1;
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

            extern int ft_strcmp(const char *s1, const char *s2);
            if (ft_strcmp(username, db_user) == 0) {
                char computed_hash[65];
                char salted_pass[256];
                
                int sp_idx = 0;
                
                // [DÜZELTME]: Rastgele sistem tuzu KALDIRILDI. 
                // Çünkü rastgele tuz, önceden oluşturulmuş hash'lerle uyuşmaz.
                // Sadece kullanıcı adını (username) tuz olarak kullanıyoruz.
                for(int u = 0; username[u] && sp_idx < 95; u++) 
                    salted_pass[sp_idx++] = username[u];
                    
                salted_pass[sp_idx++] = ':';
                
                for(int p = 0; password[p] && sp_idx < 254; p++) 
                    salted_pass[sp_idx++] = password[p];
                    
                salted_pass[sp_idx] = '\0';

                // KDF İterasyon Düzeltmesi: Tıpkı master key gibi, kullanıcı şifrelerini de
                // zorlaştırmak (Brute-force'u engellemek) için döngü eklemeliyiz. Ancak
                // senin kernel.c'deki hash'lerin tek turlu sha256. Eğer kernel.c'deki hash'leri 
                // değiştirmek istemiyorsan burası sadece 1 tur kalmalı.
                sha256_to_hex(salted_pass, computed_hash);
                
                if (constant_time_cmp(db_hash, computed_hash) == 0) {
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

int update_passwd_file(const char *new_passwd_content, uint32_t size) {
    extern int fs_get_entry_idx(const char *, uint8_t);
    int etc_idx = fs_get_entry_idx("etc", 0);
    if (etc_idx == -1) return -1;

    extern disk_file_entry_t dir_table[];
    int etc_id = dir_table[etc_idx].entry_id;
    extern int fs_atomic_update(const char *name, const uint8_t *content, uint32_t size, uint8_t parent_id);
    return fs_atomic_update("passwd", (const uint8_t *)new_passwd_content, size, etc_id);
}