#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

// İşletim sistemimizin AES başlık dosyasını dahil ediyoruz
// (Makefile'da -I bayrağı ile yolları belirteceğiz)
#include "aes.h" 

// =========================================================================
// NIST SP 800-38A (AES-256 CBC) Standart Test Vektörü (F.2.5)
// =========================================================================
const uint8_t nist_key[32] = {
    0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
    0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
    0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
    0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4
};

const uint8_t nist_iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

const uint8_t nist_plaintext[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
};

// Çıkan şifreli metnin KESİNLİKLE bu olması gerekiyor!
const uint8_t nist_expected_ciphertext[16] = {
    0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
    0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6
};

int main(void) {
    printf("\n--- Host Crypto (AES-256-CBC) Dogruluk Testi ---\n");

    // 1. ŞİFRELEME (ENCRYPTION) TESTİ
    uint8_t buffer[16];
    memcpy(buffer, nist_plaintext, 16); // Veriyi buffera alıyoruz çünkü AES in-place (yerinde) şifreler
    
    // Geçici context ve IV (Çünkü fonksiyonlar IV'yi değiştirir)
    struct AES_ctx ctx;
    uint8_t temp_iv[16];
    memcpy(temp_iv, nist_iv, 16);

    // Kendi Kernelimizin kripto algoritmasını başlatıyoruz
    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    AES_CBC_encrypt_buffer(&ctx, buffer, 16);

    // NIST Ciphertext ile birebir aynı mı?
    if (memcmp(buffer, nist_expected_ciphertext, 16) != 0) {
        printf("❌ [FAIL] AES Sifreleme NIST Standartlarina UYMUYOR!\n");
        return 1;
    }
    printf("✅ [PASS] AES-256 Sifreleme NIST FIPS-197 Standartlarina %100 Uygun.\n");

    // 2. ŞİFRE ÇÖZME (DECRYPTION) TESTİ
    memcpy(temp_iv, nist_iv, 16); // IV'yi sıfırlamak zorundayız
    AES_init_ctx_iv(&ctx, nist_key, temp_iv);
    AES_CBC_decrypt_buffer(&ctx, buffer, 16);

    // İlk baştaki Plaintext'e geri dönebildik mi?
    if (memcmp(buffer, nist_plaintext, 16) != 0) {
        printf("❌ [FAIL] AES Sifre Cozme (Decryption) asamasinda veri bozuldu!\n");
        return 1;
    }
    printf("✅ [PASS] AES-256 Sifre Cozme (Decryption) NIST Standartlarina %100 Uygun.\n");

    printf("====================================================\n\n");
    return 0; // 0 dönmek Makefile için "Testler Geçti" demektir.
}