#include <stdio.h>
#include <stdint.h>
int main() {
    char *str = "1234"; // Buraya parolanı yaz
    uint32_t hash = 5381;
    while (*str) hash = ((hash << 5) + hash) + *str++;
    hash = ((hash << 5) + hash) + '4'; // Tuz 1
    hash = ((hash << 5) + hash) + '2'; // Tuz 2
    printf("Yeni Hash: 0x%X\n", hash);
}