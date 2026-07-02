#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

int main(int argc, char **argv) {
    // 4 argüman bekliyoruz: program_adı, giriş_dosyası, çıkış_dosyası, parola
    if (argc < 4) {
        printf("Kullanim: %s <giris_elf> <cikis_sifreli_elf> <passphrase>\n", argv[0]);
        return 1;
    }
    
    char *passphrase = argv[3];
    size_t pass_len = strlen(passphrase);

    FILE *fin = fopen(argv[1], "rb");
    if (!fin) { perror("Giris dosyasi acilamadi"); return 1; }
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *file_data = malloc(file_size);
    fread(file_data, 1, file_size, fin);
    fclose(fin);

    const char *boot_seed = "esdumanOS_Super_Secret_Salt_42!";
    uint8_t master_key[32];
    
    for (int i = 0; i < 32; i++) {
        master_key[i] = boot_seed[i % 31];
    }

    uint8_t accumulator = 0x5A;
    for(int round = 0; round < 1024; round++) {
        for(int i = 0; i < 32; i++) {
            accumulator ^= passphrase[(i + round) % pass_len];
            accumulator = (accumulator << 5) | (accumulator >> 3); 
            master_key[i] ^= accumulator;
            master_key[i] = (master_key[i] << 1) | (master_key[i] >> 7);
        }
    }

    uint32_t checksum = 5381;
    for (size_t i = 0; i < file_size; i++) {
        checksum = ((checksum << 5) + checksum) + file_data[i];
    }

    uint32_t payload_len = 12 + file_size;
    uint32_t padded_len = (payload_len + 15) & ~15;
    uint8_t *padded_data = calloc(1, padded_len);
    uint32_t *hdr = (uint32_t *)padded_data;
    hdr[0] = 0x53414645;
    hdr[1] = (uint32_t)file_size;
    hdr[2] = checksum;

    memcpy(padded_data + 12, file_data, file_size);
    free(file_data);

    uint8_t iv[16];
    if (!RAND_bytes(iv, 16)) {
        printf("HATA: Kriptografik olarak guvenli rastgele IV uretilemedi!\n");
        free(padded_data);
        return 1;
    }

    uint8_t *encrypted_data = malloc(padded_len);
    int out_len1 = 0, out_len2 = 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, master_key, iv);
    EVP_CIPHER_CTX_set_padding(ctx, 0); 
    EVP_EncryptUpdate(ctx, encrypted_data, &out_len1, padded_data, padded_len);
    EVP_EncryptFinal_ex(ctx, encrypted_data + out_len1, &out_len2);
    EVP_CIPHER_CTX_free(ctx);

    FILE *fout = fopen(argv[2], "wb");
    fwrite(iv, 1, 16, fout); 
    
    fwrite(encrypted_data, 1, padded_len, fout);
    fclose(fout);

    free(padded_data);
    free(encrypted_data);

    printf("[+] %s basariyla AES-256 ile sifrelendi ve %s olusturuldu.\n", argv[1], argv[2]);
    return 0;
}