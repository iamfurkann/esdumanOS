#include "security.h"
#include "stdio.h"
#include "io.h"
#include "klog.h" 

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
} local_multiboot_info_t;

security_level_t current_sec_level = SEC_LEVEL_CRYPTO_ENFORCED;

uint8_t kernel_master_key[32];
char current_system_salt[32] = {0};

static void generate_random_salt(void) {
    uint32_t seed = 0;

    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    seed ^= lo ^ hi;

    outb(0x70, 0x00); uint8_t s = inb(0x71);
    outb(0x70, 0x02); uint8_t m = inb(0x71);
    seed ^= (s << 16) | (m << 8);

    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    for(int i = 0; i < 31; i++) {
        seed = (seed * 1103515245 + 12345); // LCG (Linear Congruential Generator) algoritması
        current_system_salt[i] = charset[seed % (sizeof(charset) - 1)];
    }
    current_system_salt[31] = '\0';
    
    klog(LOG_LEVEL_INFO, "SEC", "Sisteme ozel rastgele SALT (Tuz) uretildi.");
}

void derive_master_key(const char *password) {
    const char *host_salt = "esdumanOS_Super_Secret_Salt_42!";
    char buffer[128];
    int idx = 0;

    while(password[idx] && idx < 64) { buffer[idx] = password[idx]; idx++; }
    int s_idx = 0;
    while(host_salt[s_idx] && idx < 127) { buffer[idx++] = host_salt[s_idx++]; }
    buffer[idx] = '\0';

    extern void sha256_binary(const uint8_t *data, uint32_t len, uint8_t *hash_out);
    
    uint8_t raw_hash[32];
    uint32_t buf_len = 0;
    while(buffer[buf_len]) buf_len++;
    
    sha256_binary((uint8_t*)buffer, buf_len, raw_hash);

    for(int round = 0; round < 5000; round++) {
        sha256_binary(raw_hash, 32, raw_hash);
    }
    for(int i = 0; i < 32; i++) {
        kernel_master_key[i] = raw_hash[i];
    }
    
    klog(LOG_LEVEL_INFO, "SEC", "AES Master Key 5000-Turlu RAW SHA256 KDF ile uretildi.");
}
void init_security(void *mboot_ptr) {
    (void)mboot_ptr;
    
    // Boot olur olmaz ilk iş rastgele tuzumuzu üretelim
    generate_random_salt();

}

void set_security_level(security_level_t level) {
    if (level < current_sec_level) {
        klog(LOG_LEVEL_ERROR, "SEC", "Guvenlik seviyesi asagi cekilemez!");
        return;
    }
    
    current_sec_level = level;
    
    switch (level) {
        case SEC_LEVEL_LOCKDOWN:
            klog(LOG_LEVEL_CRITICAL, "SEC", "LOCKDOWN MODU AKTIF! Yeni processler engellendi.");
            {
                volatile uint8_t *key_ptr = kernel_master_key;
                for (int i = 0; i < 32; i++) {
                    key_ptr[i] = 0;
                }
                klog(LOG_LEVEL_INFO, "SEC", "Master Key RAM'den guvenli bir sekilde imha edildi (Zeroized)!");
            }
            break;

        case SEC_LEVEL_CRYPTO_ENFORCED:
            klog(LOG_LEVEL_INFO, "SEC", "CRYPTO-ENFORCED MOD AKTIF! Sifreli disk erisimi devrede.");
            break;

        case SEC_LEVEL_IMMUTABLE:
            klog(LOG_LEVEL_INFO, "SEC", "IMMUTABLE MOD AKTIF! Disk SALT-OKUNUR yapildi.");
            break;

        default:
            break;
    }
}