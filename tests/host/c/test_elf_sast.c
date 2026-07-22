/*
 * File: test_elf_sast.c
 * Purpose: ELF Static Analyzer (SAST) for analyzing userland applications for standard compliance.
 *
 * This file is part of the esdumanOS test suite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define RESET "\033[0m"

int check_pattern(const char *filename, const char *pattern) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *comment = strstr(line, "//");
        char *match = strstr(line, pattern);
        
        if (match != NULL) {
            if (comment != NULL && comment < match) continue;
            
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

void run_static_test(const char *filename, const char *pattern, const char *desc, int *fail_count) {
    int res = check_pattern(filename, pattern);
    if (res == 1) {
        printf("  %s[PASS]%s %s\n", GREEN, RESET, desc);
    } else if (res == 0) {
        printf("  %s[FAIL]%s %s (Missing: '%s')\n", RED, RESET, desc, pattern);
        (*fail_count)++;
    } else {
        printf("  %s[ERROR]%s File %s not found!\n", RED, RESET, filename);
        (*fail_count)++;
    }
}

int main() {
    int fail_count = 0;
    
    printf("\n======================================================\n");
    printf("       esdumanOS - ELF Static Analyzer (SAST)         \n");
    printf("======================================================\n");

    // ---------------------------------------------------------
    // 1. ECHO.C STATIC BEHAVIOR TESTS
    // ---------------------------------------------------------
    run_static_test("apps/bin/echo.c", "syscall(42", 
                    "echo.c -> Uses SYSCALL_GET_ARGS (42) to read arguments", &fail_count);
                    
    run_static_test("apps/bin/echo.c", "syscall(4", 
                    "echo.c -> Uses SYSCALL_WRITE (4) to print text to screen", &fail_count);
                    
    run_static_test("apps/bin/echo.c", "syscall(1", 
                    "echo.c -> Uses SYSCALL_EXIT (1) for safe termination", &fail_count);

    // ---------------------------------------------------------
    // 2. CLEAR.C STATIC BEHAVIOR TESTS
    // ---------------------------------------------------------
    run_static_test("apps/bin/clear.c", "syscall(10", 
                    "clear.c -> Uses SYSCALL_CLEAR_SCREEN (10) to clear screen", &fail_count);
                    
    run_static_test("apps/bin/clear.c", "syscall(1", 
                    "clear.c -> Uses SYSCALL_EXIT (1) for safe termination", &fail_count);

    printf("======================================================\n");
    if (fail_count == 0) {
        printf("RESULT: %sALL STATIC TESTS PASSED.%s New ELF programs comply with standards.\n\n", GREEN, RESET);
        return 0;
    } else {
        printf("RESULT: %s%d STATIC TEST(S) FAILED!%s ELF programs violate rules.\n\n", RED, fail_count, RESET);
        return 1;
    }
}