#include "types.h"

// Ubuntu terminaline log basmak için printf'i dışarıdan (extern) bildiriyoruz:
extern int printf(const char *format, ...);

// =========================================================================
// MOCKING: Kendi Assert Makromuz
// Ubuntu'nun <assert.h> dosyasına muhtaç kalmamak için kendi kontrolümüzü yazıyoruz.
// =========================================================================
#define assert(expr) \
    do { \
        if (!(expr)) { \
            printf("[FAIL] Beklenmeyen Deger! Hata su satirda: %s\n", #expr); \
            return 1; \
        } \
    } while(0)

uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) { hash = ((hash << 5) + hash) + *str++; }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

int main() {
    printf("[HOST TEST] Hash algoritmasi test ediliyor...\n");
    
    uint32_t root_hash = hash_djb2_salted("root");
    assert(root_hash == 0x19E28ECF); 

    uint32_t esduman_hash = hash_djb2_salted("1234");
    assert(esduman_hash == 0x7DD17035);
    
    printf("[PASS] Hash algoritmasi KUSURSUZ!\n");
    return 0;
}