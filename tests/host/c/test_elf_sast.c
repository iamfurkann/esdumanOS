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

/*
 * Function: check_pattern
 * Purpose: Tests the source code of userland ELF applications for specific patterns, bypassing commented lines.
 * Mechanism: Parses the given source file line by line to locate required system call patterns, ensuring the 
 *            userland program statically requests expected kernel services.
 * Expected Behavior: Returns 1 if the pattern is found within an uncommented line, 0 if the pattern is 
 *                    missing, and -1 if the file cannot be accessed or read.
 * Edge Cases: Handles lines where the required pattern is commented out via `//`, ensuring it only matches 
 *             active code. Handles missing files gracefully.
 */
int check_pattern(const char *filename, const char *pattern) {
    // Attempt to open the target source file in read mode to begin static analysis.
    FILE *f = fopen(filename, "r");
    // Ensure the file was successfully opened. If it fails, return -1 indicating a file access error.
    if (!f) return -1;

    char line[512];
    // Iterate through the source file line by line, loading up to 511 characters per line into the buffer.
    while (fgets(line, sizeof(line), f)) {
        // Locate any `//` substring on the current line to identify inline comments.
        char *comment = strstr(line, "//");
        // Search for the required system call pattern or keyword within the line.
        char *match = strstr(line, pattern);
        
        // Verify if the expected pattern exists anywhere on the current line.
        if (match != NULL) {
            // Check for false positives by confirming if the pattern is located after a comment marker.
            // If the comment precedes the match, ignore it as it is commented out code.
            if (comment != NULL && comment < match) continue;
            
            // The active pattern was successfully found. Close the file handler to prevent descriptor leaks.
            fclose(f);
            return 1;
        }
    }
    // Reached the end of the file without finding the required pattern in any active code block.
    // Close the file handler and return 0.
    fclose(f);
    return 0;
}

/*
 * Function: run_static_test
 * Purpose: Executes a single static analysis check against a userland file and records the outcome.
 * Mechanism: Invokes check_pattern to verify the existence of a required string, then updates the 
 *            global failure count and prints the results in colorized output.
 * Expected Behavior: Prints a [PASS] message if the check succeeds. If it fails, increments fail_count 
 *                    and prints a [FAIL] message. If the file is missing, prints an [ERROR].
 * Edge Cases: Relies on check_pattern to accurately distinguish between active and commented code.
 */
void run_static_test(const char *filename, const char *pattern, const char *desc, int *fail_count) {
    // Perform the static analysis by verifying the presence of the required pattern in the target file.
    int res = check_pattern(filename, pattern);
    
    // Evaluate the analysis result and display appropriate feedback.
    if (res == 1) {
        // The pattern was found in active code. The program complies with this specific static requirement.
        printf("  %s[PASS]%s %s\n", GREEN, RESET, desc);
    } else if (res == 0) {
        // The pattern was not found. This indicates the ELF program lacks necessary syscalls or behavior.
        printf("  %s[FAIL]%s %s (Missing: '%s')\n", RED, RESET, desc, pattern);
        // Increment the failure counter to track overall test suite success.
        (*fail_count)++;
    } else {
        // The target file could not be read, preventing any analysis.
        printf("  %s[ERROR]%s File %s not found!\n", RED, RESET, filename);
        // Record file read errors as test failures to ensure missing dependencies are caught.
        (*fail_count)++;
    }
}

/*
 * Function: main
 * Purpose: Coordinates the static analysis suite for standard userland ELF binaries (echo, clear).
 * Mechanism: Executes a series of pre-defined static tests targeting source files to verify that 
 *            they utilize the correct system call IDs for standard OS functionality.
 * Expected Behavior: Analyzes multiple applications. If all required system calls are present, exits with 0. 
 *                    If any checks fail, reports the total failures and exits with 1.
 * Edge Cases: Tests applications requiring varying numbers of system calls.
 */
int main() {
    // Initialize the global counter to track any compliance violations across the tested files.
    int fail_count = 0;
    
    printf("\n======================================================\n");
    printf("       esdumanOS - ELF Static Analyzer (SAST)         \n");
    printf("======================================================\n");

    // ---------------------------------------------------------
    // 1. ECHO.C STATIC BEHAVIOR TESTS
    // ---------------------------------------------------------
    // Verify that echo.c leverages SYSCALL_GET_ARGS (id 42) to process command-line arguments.
    run_static_test("apps/bin/echo.c", "syscall(42", 
                    "echo.c -> Uses SYSCALL_GET_ARGS (42) to read arguments", &fail_count);
                    
    // Verify that echo.c utilizes SYSCALL_WRITE (id 4) to output its parsed arguments to stdout.
    run_static_test("apps/bin/echo.c", "syscall(4", 
                    "echo.c -> Uses SYSCALL_WRITE (4) to print text to screen", &fail_count);
                    
    // Verify that echo.c issues SYSCALL_EXIT (id 1) to correctly terminate and clean up its process.
    run_static_test("apps/bin/echo.c", "syscall(1", 
                    "echo.c -> Uses SYSCALL_EXIT (1) for safe termination", &fail_count);

    // ---------------------------------------------------------
    // 2. CLEAR.C STATIC BEHAVIOR TESTS
    // ---------------------------------------------------------
    // Verify that clear.c invokes SYSCALL_CLEAR_SCREEN (id 10) to clear the terminal display.
    run_static_test("apps/bin/clear.c", "syscall(10", 
                    "clear.c -> Uses SYSCALL_CLEAR_SCREEN (10) to clear screen", &fail_count);
                    
    // Verify that clear.c issues SYSCALL_EXIT (id 1) to guarantee proper userland process termination.
    run_static_test("apps/bin/clear.c", "syscall(1", 
                    "clear.c -> Uses SYSCALL_EXIT (1) for safe termination", &fail_count);

    printf("======================================================\n");
    // Assess the final results of the static analysis to determine suite success.
    if (fail_count == 0) {
        // All checks passed; userland binaries statically conform to expected behavior.
        printf("RESULT: %sALL STATIC TESTS PASSED.%s New ELF programs comply with standards.\n\n", GREEN, RESET);
        return 0;
    } else {
        // One or more checks failed; userland binaries exhibit non-compliant structures.
        printf("RESULT: %s%d STATIC TEST(S) FAILED!%s ELF programs violate rules.\n\n", RED, fail_count, RESET);
        return 1;
    }
}