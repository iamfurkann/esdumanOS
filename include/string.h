/**
 * @file string.h
 * @brief String manipulation functions.
 */
#ifndef STRING_H
#define STRING_H

#include "types.h"

/**
 * @brief Calculates the length of a string.
 * 
 * @param str The string whose length is to be calculated.
 * @return The length of the string.
 */
size_t strlen(const char *str);

/**
 * @brief Compares two strings.
 * 
 * @param s1 The first string.
 * @param s2 The second string.
 * @return An integer less than, equal to, or greater than zero if s1 is found,
 * respectively, to be less than, to match, or be greater than s2.
 */
int strcmp(const char *s1, const char *s2);

/**
 * @brief Fills memory with a constant byte.
 * 
 * @param s Pointer to the memory area to fill.
 * @param c The byte to fill the memory with.
 * @param n Number of bytes to fill.
 * @return A pointer to the memory area s.
 */
void *ft_memset(void *s, int c, size_t n);

#endif