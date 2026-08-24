/**
 * @file chown.c
 * @brief Changes a file's owner and group.
 *
 * Root only, and the kernel is what enforces that - this program does not check
 * first. A tool that decides for itself who may call it is a tool that can be
 * replaced by one that decides differently.
 *
 * Numeric ids only. There is a /etc/passwd but no name lookup exposed to user
 * space, and inventing one here would put a second parser for that file in a
 * place that has no business owning one.
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
 * @brief Reads a decimal id.
 *
 * @param s Digits.
 * @param out Receives the value.
 * @return 1 when every character was a digit and there was at least one.
 */
static int parse_uint(const char *s, unsigned int *out) {
    unsigned int v = 0;
    int digits = 0;

    for (int i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10u + (unsigned int)(s[i] - '0');
        digits++;
        if (digits > 10) return 0;
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
    unsigned int uid = 0, gid = 0;
    int i = 0, colon = -1;

    for (int k = 0; k < 160; k++) args[k] = '\0';
    syscall(42, (int)args, 0, 0);   /* SYSCALL_GET_ARGS */

    while (args[i] && args[i] != ' ') i++;

    if (args[i] != ' ') {
        print("Usage: chown <uid>[:<gid>] <file>"); nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }
    args[i] = '\0';

    for (int k = 0; args[k]; k++) {
        if (args[k] == ':') { colon = k; break; }
    }

    if (colon >= 0) {
        args[colon] = '\0';
        if (!parse_uint(&args[colon + 1], &gid)) {
            print("chown: not a group id"); nl();
            syscall(1, 1, 0, 0);
            while (1) { }
        }
    }

    if (!parse_uint(args, &uid)) {
        print("chown: not a user id"); nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }

    /* No group given means the group follows the user, which is what this
     * system does everywhere else - there is no group database for it to mean
     * anything else. */
    if (colon < 0) gid = uid;

    int rc = syscall(65, (int)&args[i + 1], (int)uid, (int)gid);   /* SYSCALL_CHOWN */

    if (rc < 0) {
        print("chown: cannot change '"); print(&args[i + 1]); print("'");
        if (rc == -1) print(": root only");
        else if (rc == -2) print(": no such file");
        nl();
        syscall(1, 1, 0, 0);
        while (1) { }
    }

    syscall(1, 0, 0, 0);   /* SYSCALL_EXIT */
    while (1) { }
}
