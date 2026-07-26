/*
 * File: hash.c
 * Purpose: Simple hash generator for tests and tools.
 *
 * This file is part of the esdumanOS test suite.
 */
#include <stdio.h>
#include <stdint.h>

/**
 * @brief Main function of the hash tool.
 * 
 * @return 0 on success.
 */
int main() {
    char *str = "1234"; // Write your password here
    uint32_t hash = 5381;
    while (*str) hash = ((hash << 5) + hash) + *str++;
    hash = ((hash << 5) + hash) + '4'; // Salt 1
    hash = ((hash << 5) + hash) + '2'; // Salt 2
    printf("New Hash: 0x%X\n", hash);
    return 0;
}