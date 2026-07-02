#include <stdio.h>
#include <assert.h>
#include <stdint.h>

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
    
    printf("[HOST TEST] Hash algoritmasi KUSURSUZ!\n");
    return 0;
}