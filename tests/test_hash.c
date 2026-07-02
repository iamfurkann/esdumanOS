#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

// Test edilecek bağımsız fonksiyonlarımız
uint32_t hash_djb2_salted(const char *str) {
    uint32_t hash = 5381;
    while (*str) { hash = ((hash << 5) + hash) + *str++; }
    hash = ((hash << 5) + hash) + '4';
    hash = ((hash << 5) + hash) + '2';
    return hash;
}

int main() {
    printf("[TEST] Basliyor: Host-Side Unit Tests\n");
    
    uint32_t root_hash = hash_djb2_salted("root");
    uint32_t esduman_hash = hash_djb2_salted("esduman");
    assert(root_hash == 0x1E8CF78F); 
    assert(esduman_hash == 0x7DD17035);
    printf("  [OK] Hash algoritmasi (hash_djb2_salted) dogrulandi.\n");
    
    printf("[BASARILI] Tum Unit Testler Gecti!\n");
    return 0;
}