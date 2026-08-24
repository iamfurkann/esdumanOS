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

    // ---------------------------------------------------------
    // 3. NEW COMMANDS STATIC BEHAVIOR TESTS
    // ---------------------------------------------------------
    
    // touch.c tests
    run_static_test("apps/bin/touch.c", "syscall(8,", "touch.c -> Uses SYSCALL_CREATE_FILE (8)", &fail_count);
    run_static_test("apps/bin/touch.c", "syscall(1,", "touch.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // rm.c tests
    run_static_test("apps/bin/rm.c", "syscall(22,", "rm.c -> Uses SYSCALL_RM_FILE (22)", &fail_count);
    run_static_test("apps/bin/rm.c", "syscall(1,", "rm.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // mv.c tests
    run_static_test("apps/bin/mv.c", "syscall(23,", "mv.c -> Uses SYSCALL_MV_FILE (23)", &fail_count);
    run_static_test("apps/bin/mv.c", "syscall(1,", "mv.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // cp.c tests
    run_static_test("apps/bin/cp.c", "syscall(40,", "cp.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/cp.c", "syscall(8,", "cp.c -> Uses SYSCALL_CREATE_FILE (8)", &fail_count);
    run_static_test("apps/bin/cp.c", "syscall(3,", "cp.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/cp.c", "syscall(4,", "cp.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/cp.c", "syscall(38,", "cp.c -> Uses SYSCALL_CLOSE (38)", &fail_count);
    run_static_test("apps/bin/cp.c", "syscall(1,", "cp.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // free.c tests
    run_static_test("apps/bin/free.c", "syscall(15,", "free.c -> Uses SYSCALL_MEMINFO (15)", &fail_count);
    run_static_test("apps/bin/free.c", "syscall(1,", "free.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // whoami.c tests
    run_static_test("apps/bin/whoami.c", "syscall(43,", "whoami.c -> Uses SYSCALL_GETUID (43)", &fail_count);
    run_static_test("apps/bin/whoami.c", "syscall(1,", "whoami.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // kill.c tests
    run_static_test("apps/bin/kill.c", "syscall(25,", "kill.c -> Uses SYSCALL_KILL (25)", &fail_count);
    run_static_test("apps/bin/kill.c", "syscall(1,", "kill.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // grep.c tests
    run_static_test("apps/bin/grep.c", "syscall(40,", "grep.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/grep.c", "syscall(3,", "grep.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/grep.c", "syscall(38,", "grep.c -> Uses SYSCALL_CLOSE (38)", &fail_count);
    run_static_test("apps/bin/grep.c", "syscall(1,", "grep.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // head.c tests
    run_static_test("apps/bin/head.c", "syscall(40,", "head.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/head.c", "syscall(3,", "head.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/head.c", "syscall(38,", "head.c -> Uses SYSCALL_CLOSE (38)", &fail_count);
    run_static_test("apps/bin/head.c", "syscall(1,", "head.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // wc.c tests
    run_static_test("apps/bin/wc.c", "syscall(42,", "wc.c -> Uses SYSCALL_GET_ARGS (42)", &fail_count);
    run_static_test("apps/bin/wc.c", "syscall(40,", "wc.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/wc.c", "syscall(3,", "wc.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/wc.c", "syscall(4,", "wc.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/wc.c", "syscall(38,", "wc.c -> Uses SYSCALL_CLOSE (38)", &fail_count);
    run_static_test("apps/bin/wc.c", "syscall(1,", "wc.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    /*
     * Standard input, statically.
     *
     * Every tool here opened a file by name and nothing else, so a pipeline could
     * be parsed, forked and connected with no program willing to sit at the far
     * end of it. These three check that the descriptor-0 path still exists at all
     * - a reader of this source can see the file path being handled and miss that
     * the bare-argument case is gone.
     */
    run_static_test("apps/bin/grep.c", "grep_fd(0,", "grep.c -> Reads standard input when no file is named", &fail_count);
    run_static_test("apps/bin/head.c", "head_fd(0)", "head.c -> Reads standard input when no file is named", &fail_count);
    run_static_test("apps/bin/wc.c", "wc_fd(0)", "wc.c -> Reads standard input when no file is named", &fail_count);

    // date.c tests
    run_static_test("apps/bin/date.c", "syscall(4,", "date.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/date.c", "syscall(1,", "date.c -> Uses SYSCALL_EXIT (1)", &fail_count);
    /*
     * The clock, not a string it carries. date(1) printed the same fixed line on
     * every boot until the TIME syscall existed, and a regression to that would
     * still print something plausible - this is the check that would notice.
     */
    run_static_test("apps/bin/date.c", "syscall(55,", "date.c -> Uses SYSCALL_TIME (55) rather than a fixed string", &fail_count);
    run_static_test("apps/bin/date.c", "syscall(42,", "date.c -> Uses SYSCALL_GET_ARGS (42) to read -u", &fail_count);

    /*
     * edit.c tests.
     *
     * The full-screen program, and the only one that reads the keyboard a byte
     * at a time and asks whether another is waiting. POLL (63) is the check
     * worth pinning: without it the editor cannot tell the Escape key from the
     * first byte of an arrow, and losing the call would leave a modal editor
     * whose most-pressed key blocks until the next one.
     */
    run_static_test("apps/bin/edit.c", "syscall(63,", "edit.c -> Uses SYSCALL_POLL (63) to tell ESC from a sequence", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(3,", "edit.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(4,", "edit.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(40,", "edit.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(38,", "edit.c -> Uses SYSCALL_CLOSE (38)", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(8,", "edit.c -> Uses SYSCALL_CREATE_FILE (8) for a file that does not exist yet", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(24,", "edit.c -> Uses SYSCALL_SIGNAL_REG (24) to decline the interrupt and catch the continue", &fail_count);
    run_static_test("apps/bin/edit.c", "syscall(1,", "edit.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    /*
     * chmod.c and chown.c tests.
     *
     * The call each one exists to make is the thing worth pinning: a refactor
     * that stopped chmod calling SYSCALL_CHMOD would still build, still run, and
     * still exit zero.
     */
    run_static_test("apps/bin/chmod.c", "syscall(64,", "chmod.c -> Uses SYSCALL_CHMOD (64), which is the whole point of it", &fail_count);
    run_static_test("apps/bin/chmod.c", "syscall(42,", "chmod.c -> Uses SYSCALL_GET_ARGS (42)", &fail_count);
    run_static_test("apps/bin/chmod.c", "syscall(4,", "chmod.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/chmod.c", "syscall(1,", "chmod.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    run_static_test("apps/bin/chown.c", "syscall(65,", "chown.c -> Uses SYSCALL_CHOWN (65), which is the whole point of it", &fail_count);
    run_static_test("apps/bin/chown.c", "syscall(42,", "chown.c -> Uses SYSCALL_GET_ARGS (42)", &fail_count);
    run_static_test("apps/bin/chown.c", "syscall(4,", "chown.c -> Uses SYSCALL_WRITE (4)", &fail_count);
    run_static_test("apps/bin/chown.c", "syscall(1,", "chown.c -> Uses SYSCALL_EXIT (1)", &fail_count);

    // sh.c tests
    run_static_test("apps/bin/sh.c", "syscall(5,", "sh.c -> Uses SYSCALL_EXEC (5) for external apps", &fail_count);
    run_static_test("apps/bin/sh.c", "SYSCALL_OPEN", "sh.c -> Uses SYSCALL_OPEN (40)", &fail_count);
    run_static_test("apps/bin/sh.c", "SYSCALL_READ", "sh.c -> Uses SYSCALL_READ (3)", &fail_count);
    run_static_test("apps/bin/sh.c", "SYSCALL_WRITE", "sh.c -> Uses SYSCALL_WRITE (4)", &fail_count);

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