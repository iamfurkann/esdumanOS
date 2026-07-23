/*
 * File: test_string.c
 * Purpose: Testing suite for libft (string and memory manipulation functions).
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "libft.h" // Including the header directly instead of using extern

/**
 * @brief Transformation function to convert a lowercase character to uppercase.
 * 
 * Used as a callback for string mapping operations. It iterates over characters 
 * and applies ASCII transformations.
 * 
 * @param i The index of the character within the string.
 * @param c The character to evaluate and potentially transform.
 * @return The uppercase equivalent if applicable, otherwise the original character.
 */
static char map_to_upper(unsigned int i, char c) {
    (void)i;
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/**
 * @brief Mutation function to increment a character's ASCII value by one.
 * 
 * Used as an in-place callback for string iteration operations.
 * 
 * @param i The index of the character within the string.
 * @param c A pointer to the character to be mutated.
 */
static void iter_add_one(unsigned int i, char *c) {
    (void)i;
    if (*c) (*c) += 1;
}

/**
 * @brief Validates character classification functions provided by the kernel's libft.
 *
 * This test suite evaluates character type checking (e.g., alpha, digit, alnum) 
 * and character case conversion utilities.
 *
 * @expected All classification conditions must accurately identify standard ASCII inputs, 
 *           while properly rejecting invalid or out-of-bounds characters. Case conversions 
 *           should strictly toggle cases.
 */
static void test_ctype(void) {
    printk("\n--- Ctype Tests ---\n");
    KTEST_ASSERT(ft_isalpha('A') != 0, "ft_isalpha finds letter");
    KTEST_ASSERT(ft_isalpha('1') == 0, "ft_isalpha filters number");
    KTEST_ASSERT(ft_isdigit('5') != 0, "ft_isdigit finds digit");
    KTEST_ASSERT(ft_isdigit('a') == 0, "ft_isdigit filters letter");
    KTEST_ASSERT(ft_isalnum('Z') != 0 && ft_isalnum('9') != 0, "ft_isalnum finds alphanumeric");
    KTEST_ASSERT(ft_isalnum('?') == 0, "ft_isalnum filters special character");
    KTEST_ASSERT(ft_toupper('a') == 'A', "ft_toupper converts to uppercase");
    KTEST_ASSERT(ft_tolower('Z') == 'z', "ft_tolower converts to lowercase");
}

/**
 * @brief Tests basic string manipulation and searching functions.
 *
 * Evaluates core functionalities such as length calculation, string comparison, 
 * bounded copying, bounded concatenation, and substring/character searching.
 *
 * @expected Copy and concatenation functions should correctly terminate strings and return
 *           appropriate lengths without buffer overflows. Search functions should return
 *           valid pointers on hits and NULL on misses.
 */
static void test_string_basics(void) {
    printk("\n--- String (Basic & Search) Tests ---\n");
    
    KTEST_ASSERT(ft_strlen("42Kocaeli") == 9, "ft_strlen works correctly");
    KTEST_ASSERT(ft_strncmp("abc", "abc", 3) == 0, "ft_strncmp equal strings");
    KTEST_ASSERT(ft_strncmp("abc", "abd", 3) < 0, "ft_strncmp case difference");
    KTEST_ASSERT(ft_strcmp("test", "test") == 0, "ft_strcmp equal");

    char buf1[20] = "Welcome";
    // Attempt to copy "Test" into the buffer with a strict size limit.
    KTEST_ASSERT(ft_strlcpy(buf1, "Test", 5) == 4, "ft_strlcpy return value (source length)");
    KTEST_ASSERT(ft_strcmp(buf1, "Test") == 0, "ft_strlcpy copy successful");
    
    char buf2[20] = "42";
    // Concatenate strings while strictly respecting buffer capacity.
    KTEST_ASSERT(ft_strlcat(buf2, "KFS", 10) == 5, "ft_strlcat return value correct");
    KTEST_ASSERT(ft_strcmp(buf2, "42KFS") == 0, "ft_strlcat concatenation successful");

    KTEST_ASSERT(ft_strchr("Kocaeli", 'c') != NULL, "ft_strchr finds character");
    KTEST_ASSERT(ft_strchr("Kocaeli", 'z') == NULL, "ft_strchr non-existent character");
    KTEST_ASSERT(ft_strrchr("Kocaeli", 'i') != NULL, "ft_strrchr reverse search");
    KTEST_ASSERT(ft_strstr("Hello World", "World") != NULL, "ft_strstr finds word");
    // Ensure bounds-checked searching operates correctly over string slices.
    KTEST_ASSERT(ft_strnstr("Hello World", "World", 15) != NULL, "ft_strnstr finds within limit");
    KTEST_ASSERT(ft_strnstr("Hello World", "World", 5) == NULL, "ft_strnstr doesn't find outside limit");
}

/**
 * @brief Validates low-level memory manipulation functions.
 *
 * Tests the kernel's implementations of `memset`, `bzero`, `memcpy`, `memmove`, 
 * `memcmp`, and `memchr`. Crucially checks `memmove` for overlapping regions.
 *
 * @expected Memory blocks should reflect precise byte-level operations. Operations on
 *           overlapping memory must cleanly execute without destructive data corruption.
 */
static void test_memory(void) {
    printk("\n--- Memory Tests ---\n");
    
    char mem1[10] = "123456789";
    char mem2[10] = "000000000";

    ft_memset(mem1, 'A', 3);
    KTEST_ASSERT(ft_strncmp(mem1, "AAA456", 6) == 0, "ft_memset successful");
    
    // Ensure bzero properly null-terminates the specified region.
    ft_bzero(mem1, 2);
    KTEST_ASSERT(mem1[0] == 0 && mem1[1] == 0 && mem1[2] == 'A', "ft_bzero successful");
    
    ft_memcpy(mem2, "Test", 4);
    KTEST_ASSERT(ft_strncmp(mem2, "Test00", 6) == 0, "ft_memcpy successful");

    char move_buf[20] = "123456789";
    // Overlap the region by moving data forward into a space already occupied by the source.
    ft_memmove(move_buf + 2, move_buf, 5); // Forward overlap (121234589)
    KTEST_ASSERT(ft_strncmp(move_buf, "1212345", 7) == 0, "ft_memmove overlapping memory (forward)");

    KTEST_ASSERT(ft_memcmp("abc", "abc", 3) == 0, "ft_memcmp equal");
    KTEST_ASSERT(ft_memcmp("abc", "abz", 3) != 0, "ft_memcmp different");
    KTEST_ASSERT(ft_memchr("42Kocaeli", 'K', 9) != NULL, "ft_memchr finds byte");
}

/**
 * @brief Tests string conversions and operations requiring dynamic memory allocation.
 *
 * Evaluates functions like `atoi`, `itoa`, and dynamically allocated string manipulations
 * such as `strdup`, `substr`, `strjoin`, `strtrim`, `split`, `strmapi`, and `striteri`.
 *
 * @expected Type conversions must parse edge cases successfully (e.g., negative numbers, whitespaces).
 *           Dynamic allocations must return valid heap pointers containing accurately processed strings.
 */
static void test_conversion_and_alloc(void) {
    printk("\n--- Conversion and Dynamic Memory Tests ---\n");
    
    KTEST_ASSERT(ft_atoi("42") == 42, "ft_atoi positive number");
    KTEST_ASSERT(ft_atoi("   -42") == -42, "ft_atoi negative number with spaces");
    
    char *itoa_res = ft_itoa(-123);
    if (itoa_res) {
        KTEST_ASSERT(ft_strcmp(itoa_res, "-123") == 0, "ft_itoa works");
        // Ensure to reclaim memory to prevent leaks (commented out here based on testing framework constraints).
        // kfree(itoa_res); // Should use kheap kfree/free to prevent memory leak
    }

    char *dup = ft_strdup("Kernel");
    if (dup) {
        KTEST_ASSERT(ft_strcmp(dup, "Kernel") == 0, "ft_strdup works");
    }
    
    char *sub = ft_substr("42Kocaeli", 2, 7);
    if (sub) {
        KTEST_ASSERT(ft_strcmp(sub, "Kocaeli") == 0, "ft_substr successful");
    }
    
    char *join = ft_strjoin("42", "KFS");
    if (join) {
        KTEST_ASSERT(ft_strcmp(join, "42KFS") == 0, "ft_strjoin successful");
    }

    char *trim = ft_strtrim("xxX42KocaeliXxx", "xX");
    if (trim) {
        KTEST_ASSERT(ft_strcmp(trim, "42Kocaeli") == 0, "ft_strtrim successful");
    }

    char **split = ft_split("a b c", ' ');
    if (split) {
        KTEST_ASSERT(ft_strcmp(split[0], "a") == 0 && ft_strcmp(split[2], "c") == 0, "ft_split successful");
    }

    char *mapi_res = ft_strmapi("abcd", map_to_upper);
    if (mapi_res) {
        KTEST_ASSERT(ft_strcmp(mapi_res, "ABCD") == 0, "ft_strmapi works");
    }

    char iter_buf[] = "abc";
    ft_striteri(iter_buf, iter_add_one);
    KTEST_ASSERT(ft_strcmp(iter_buf, "bcd") == 0, "ft_striteri works");
}

/**
 * @brief Executes the comprehensive libft string and memory test suite.
 *
 * This acts as the main runner orchestrating all sub-tests relevant to string manipulations, 
 * character typing, memory operations, and dynamically allocated processing.
 *
 * @expected All underlying test phases (ctype, basics, memory, conversion) complete successfully 
 *           and log their progress.
 */
void run_string_tests(void) {
    printk("\n===================================\n");
    printk("       LIBFT TEST SUITE STARTING   \n");
    printk("===================================\n");

    test_ctype();
    test_string_basics();
    test_memory();
    test_conversion_and_alloc();

    printk("\n===================================\n");
    printk("       LIBFT TEST SUITE FINISHED   \n");
    printk("===================================\n");
}