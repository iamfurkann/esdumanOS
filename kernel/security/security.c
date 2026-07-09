#include "security.h"
#include "stdio.h"

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
} local_multiboot_info_t;

security_level_t current_sec_level = SEC_LEVEL_CRYPTO_ENFORCED;

uint8_t kernel_master_key[32];

void init_security(void *mboot_ptr) {
    local_multiboot_info_t *mboot_info = (local_multiboot_info_t *)mboot_ptr;

    const char *boot_seed = "esdumanOS_Super_Secret_Salt_42!";
    char *cmdline = 0;
    char *pass_ptr = 0;
    int pass_len = 0;

    if (mboot_info != 0 && (mboot_info->flags & (1 << 2))) {
        cmdline = (char *)mboot_info->cmdline;
    }

    if (cmdline) {
        for (int i = 0; cmdline[i] != '\0'; i++) {
            int match = 1;
            const char *target = "kernel_pass=";
            for (int j = 0; target[j] != '\0'; j++) {
                if (cmdline[i + j] != target[j]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                pass_ptr = &cmdline[i + 12];
                break;
            }
        }
    }
    if (pass_ptr) {
        while (pass_ptr[pass_len] != '\0' && 
               pass_ptr[pass_len] != ' ' && 
               pass_ptr[pass_len] != '\n' && 
               pass_ptr[pass_len] != '\r' && 
               pass_len < 64) {
            pass_len++;
        }
    }

    if (pass_len > 0) {
        printk("[SEC] GRUB cmdline uzerinden 'kernel_pass' entropisi yakalandi!\n");
        for(int i = 0; i < 32; i++) {
            kernel_master_key[i] = boot_seed[i % 31];
        }

        uint8_t accumulator = 0x5A;
        for(int round = 0; round < 1024; round++) {
            for(int i = 0; i < 32; i++) {
                accumulator ^= pass_ptr[(i + round) % pass_len];
                accumulator = (accumulator << 5) | (accumulator >> 3);       
                kernel_master_key[i] ^= accumulator;
                kernel_master_key[i] = (kernel_master_key[i] << 1) | (kernel_master_key[i] >> 7);
            }
        }
        
        printk("[SEC] Master Key basariyla turetildi. Uzunluk: %d karakter.\n", pass_len);
    }
}
void set_security_level(security_level_t level) {
    if (level < current_sec_level) {
        printk("[GUVENLIK REDDI] Guvenlik seviyesi asagi cekilemez!\n");
        return;
    }
    
    current_sec_level = level;
    
    switch (level) {
        case SEC_LEVEL_LOCKDOWN:
            printk("[SISTEM UYARISI] LOCKDOWN MODU AKTIF! Yeni processler engellendi.\n");
            {
                volatile uint8_t *key_ptr = kernel_master_key;
                for (int i = 0; i < 32; i++) {
                    key_ptr[i] = 0;
                }
                printk("[SEC] Master Key RAM'den guvenli bir sekilde imha edildi (Zeroized)!\n");
            }
            break;

        case SEC_LEVEL_CRYPTO_ENFORCED:
            printk("[SISTEM UYARISI] CRYPTO-ENFORCED MOD AKTIF! Sifreli disk erisimi devrede.\n");
            break;

        case SEC_LEVEL_IMMUTABLE:
            printk("[SISTEM UYARISI] IMMUTABLE MOD AKTIF! Disk SALT-OKUNUR yapildi.\n");
            break;

        default:
            break;
    }
}