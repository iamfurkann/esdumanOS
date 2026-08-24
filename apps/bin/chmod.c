/**
 * @file chmod.c
 * @brief Changes a file's permission bits.
 *
 * The mode is read as octal and only as octal. Symbolic modes - `u+x`, `go-w` -
 * are a small parser and a large amount of behaviour to get subtly wrong, and
 * they mean nothing extra on a system whose bits are the nine everybody knows.
 * A number that is not octal is refused rather than guessed at: `chmod 800 f`
 * silently meaning something is worse than `chmod 800 f` failing.
 *
 * Syscall numbers are written out as literals here, as in every other program in
 * /bin - tests/host/c/test_elf_sast.c asserts that each program contains the
 * literal call text for the syscalls it is supposed to make.
 */
#include "syscall.h"

/**
 * @brief Invokes a system call.
 * @param num System call number.
 * @param arg1 First argument.
 * @param arg2 Second argument.
 * @param arg3 Third argument.
 * @return Return value from the system call.
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Prints a string to standard output.
 * @param str Null-terminated string.
 */
static void print(const char *str) {
    int len = 0;
    while (str[len]) len++;
    syscall(4, 1, (int)str, len);   /* SYSCALL_WRITE */
}

/** @brief Prints a newline. */
static void nl(void) {
    syscall(4, 1, (int)"\n", 1);
}

/**
 * @brief Reads an octal mode.
 *
 * @param s Digits.
 * @param out Receives the value.
 * @return 1 when every character was an octal digit and there was at least one.
 */
static int parse_octal(const char *s, unsigned int *out) {
    unsigned int v = 0;
    int digits = 0;

    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '7') return 0;
        v = (v << 3) | (unsigned int)(s[i] - '0');
        digits++;
        if (digits > 4) return 0;
    }
    if (digits == 0) return 0;

    *out = v;
    return 1;
}

/**
 * @brief Main entry point.
 */
void main(void) {
    char args[160];
    unsigned int mode = 0;
    int i = 0;

    for (int k = 0; k < 160; k++) args[k] = '\0';
    syscall(42, (int)args, 0, 0);   /* SYSCALL_GET_ARGS */

    /* "<mode> <path>", split at the first space. The path keeps any spaces
     * after that, which is the same shape every other tool here uses. */
    while (args[i] && args[i] != ' ') i++;

    if (args[i] != ' ') {
        print("Usage: chmod <octal mode> <file>"); nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }
    args[i] = '\0';

    if (!parse_octal(args, &mode)) {
        print("chmod: not an octal mode: "); print(args); nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }

    int rc = syscall(64, (int)&args[i + 1], (int)mode, 0);   /* SYSCALL_CHMOD */

    if (rc < 0) {
        print("chmod: cannot change '"); print(&args[i + 1]); print("'");
        /* The two a user actually meets, told apart. Anything else is reported
         * without a guess at what it was. */
        if (rc == -1) print(": not the owner");
        else if (rc == -2) print(": no such file");
        nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }

    syscall(1, 0, 0, 0);   /* SYSCALL_EXIT */
    while (1) { }
}
