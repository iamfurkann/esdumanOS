#include "aes.h"
#include "fs.h"
#include "crypto.h"
#include "security.h"
#include "errno.h"
#include "stdio.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern uint32_t timer_ticks;

#include "aes.h"
#include "fs.h"
#include "crypto.h"
#include "security.h"
#include "errno.h"
#include "stdio.h"

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern uint32_t timer_ticks;

static int rdrand_supported = -1;

static int check_rdrand_support(void) {
    uint32_t ecx;
    asm volatile(
        "mov $1, %%eax\n"
        "cpuid\n"
        : "=c"(ecx)
        :
        : "eax", "ebx", "edx"
    );
    return (ecx & (1 << 30)) != 0;
}

static uint32_t get_hardware_rand(void) {
    uint32_t val;
    uint8_t ok;
    for (int i = 0; i < 10; i++) {
        asm volatile("rdrand %0; setc %1" : "=r"(val), "=qm"(ok));
        if (ok) return val;
    }
    return timer_ticks;
}

int fs_create_encrypted(const char *name, const uint8_t *data, uint32_t len, const uint8_t key[32], uint8_t parent_id) {
    uint32_t payload_len = 12 + len;
    uint32_t padded_len = (payload_len + 15) & ~15; 
    uint32_t total_len = 16 + padded_len; 

    uint8_t *enc_buffer = (uint8_t *)kmalloc(total_len);
    if (!enc_buffer) return E_NOMEM;

    if (rdrand_supported == -1) {
        rdrand_supported = check_rdrand_support();
        if (rdrand_supported) {
            printk("[CRYPTO] TRNG (RDRAND) tespit edildi! Kriptografik IV uretimi aktif.\n");
        } else {
            printk("[CRYPTO] RDRAND destegi yok. Yazilimsal PRNG (LCG) kullaniliyor.\n");
        }
    }

    if (rdrand_supported) {
        uint32_t *iv_ptr = (uint32_t *)enc_buffer;
        for (int i = 0; i < 4; i++) {
            iv_ptr[i] = get_hardware_rand();
        }
    } else {
        static uint32_t global_iv_counter = 0;
        uint32_t stack_noise = 0;
        global_iv_counter++;
        uint8_t accumulator = 0x5A;

        for(int i = 0; i < 16; i++) {
            uint32_t entropy = timer_ticks ^ global_iv_counter ^ ((uint32_t)&stack_noise) ^ (i * 0x1337);
            entropy = (entropy * 1103515245) + 12345;
            accumulator ^= (entropy >> 16) & 0xFF;
            accumulator = (accumulator << 5) | (accumulator >> 3); 
            enc_buffer[i] = accumulator;
        }
    }

    uint32_t checksum = 5381;
    for (uint32_t i = 0; i < len; i++) {
        checksum = ((checksum << 5) + checksum) + data[i];
    }

    uint32_t *hdr = (uint32_t *)(enc_buffer + 16); 
    hdr[0] = 0x53414645; // "SAFE"
    hdr[1] = len;        
    hdr[2] = checksum;   

    for(uint32_t i = 0; i < len; i++) {
        enc_buffer[28 + i] = data[i];
    }
    
    for(uint32_t i = 12 + len; i < padded_len; i++) {
        enc_buffer[16 + i] = 0;
    }
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, enc_buffer); 
    AES_CBC_encrypt_buffer(&ctx, enc_buffer + 16, padded_len); 

    int ret = fs_create_file_raw(name, enc_buffer, total_len, parent_id);
    kfree(enc_buffer);
    return ret;
}

int fs_read_encrypted(vfs_file_t *file, uint8_t *buffer, uint32_t size, const uint8_t key[32]) {
    // 1. Dosya IV (16) + Header (12) boyutundan küçükse geçersizdir
    if (file->file_size <= 28) return 0; 

    uint8_t *temp_buf = (uint8_t *)kmalloc(file->file_size);
    if (!temp_buf) return 0;

    uint32_t requested_offset = file->current_offset;
    file->current_offset = 0;

    int read_bytes = fs_read_raw(file, temp_buf, file->file_size);
    if (read_bytes <= 28) { 
        file->current_offset = requested_offset; 
        kfree(temp_buf); 
        return 0; 
    }

    uint32_t cipher_len = read_bytes - 16;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, temp_buf);
    AES_CBC_decrypt_buffer(&ctx, temp_buf + 16, cipher_len);

    uint32_t *hdr = (uint32_t *)(temp_buf + 16);
    uint32_t magic = hdr[0];
    uint32_t orig_len = hdr[1];
    uint32_t stored_checksum = hdr[2];

    if (magic != 0x53414645) {
        printk("[CRYPTO HATA] Magic Number uyumsuzlugu! Parola yanlis veya veri bozuk.\n");
        kfree(temp_buf);
        return 0;
    }

    uint8_t *plaintext = temp_buf + 28;
    uint32_t calc_checksum = 5381;
    for (uint32_t i = 0; i < orig_len; i++) {
        calc_checksum = ((calc_checksum << 5) + calc_checksum) + plaintext[i];
    }

    if (calc_checksum != stored_checksum) {
        terminal_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        printk("\n[CRYPTO KRITIK HATA] BÜTÜNLÜK (INTEGRITY) İHLALİ!\n");
        printk("Dosya uzerinde oynanmis veya disk sektörleri bozulmus!\n");
        terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        kfree(temp_buf);
        return 0;
    }

    uint32_t bytes_to_copy = size;
    if (requested_offset >= orig_len) {
        bytes_to_copy = 0; 
    } else if (requested_offset + bytes_to_copy > orig_len) {
        bytes_to_copy = orig_len - requested_offset;
    }

    for(uint32_t i = 0; i < bytes_to_copy; i++) {
        buffer[i] = plaintext[requested_offset + i];
    }

    file->current_offset = requested_offset + bytes_to_copy;
    kfree(temp_buf);

    return bytes_to_copy;
}