/**
 * @file stat.c
 * @brief Prints a file's metadata: size, type, owner and storage form.
 */
#include "syscall.h"
#include "stat.h"

/**
 * @brief Invokes a system call
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return Return value from the system call
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Prints a string to the standard output
 * @param str The null-terminated string to print
 */
void print(const char *str) {
    int len = 0;
    while(str[len]) len++;
    syscall(SYSCALL_WRITE, 1, (int)str, len);
}

/**
 * @brief Prints a newline character to the standard output
 */
void print_newline(void) {
    syscall(SYSCALL_WRITE, 1, (int)"\n", 1);
}

/**
 * @brief Prints an unsigned value in decimal
 * @param value The number to print
 */
void print_uint(unsigned int value) {
    char buf[12];
    int i = 0;

    if (value == 0) { print("0"); return; }

    while (value > 0) { buf[i++] = (char)('0' + (value % 10)); value /= 10; }

    char out[12];
    int j = 0;
    while (i > 0) { out[j++] = buf[--i]; }
    out[j] = '\0';

    print(out);
}

/**
 * @brief Main entry point for the application
 *
 * The whole argument string is the path, as the other tools here treat it. Since
 * v0.3.1 the shell passes it through unchanged, so a bare name resolves against
 * the working directory the kernel is holding for this process.
 */
void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(SYSCALL_GET_ARGS, (int)args_buf, 0, 0);

    if (args_buf[0] == '\0') {
        /*
         * Exit 1, like the failure below. This used to exit 0, which reported a
         * usage error as success - invisible until exit statuses started
         * reaching the shell, and then immediately visible as
         * "stat && echo CHAINED" printing CHAINED with no file named.
         */
        print("stat: no file given"); print_newline();
        syscall(SYSCALL_EXIT, 1, 0, 0);
        while (1);
    }

    esd_stat_t st;
    int res = syscall(SYSCALL_STAT, (int)args_buf, (int)&st, 0);

    if (res < 0) {
        print("stat: cannot stat '"); print(args_buf); print("'"); print_newline();
        syscall(SYSCALL_EXIT, 1, 0, 0);
        while (1);
    }

    print("  File: "); print(args_buf); print_newline();

    print("  Type: ");
    print(st.st_type == 1 ? "directory" : "regular file");
    print_newline();

    print("  Size: "); print_uint(st.st_size); print(" bytes"); print_newline();

    /*
     * Reported separately because they differ whenever the file is stored
     * encrypted: the on-disk form carries an IV, a header and padding on top of
     * the bytes a read() returns. Printing only one of them would make the
     * other one look wrong.
     */
    print("  Disk: "); print_uint(st.st_disk_size);
    print(st.st_encrypted ? " bytes (encrypted)" : " bytes");
    print_newline();

    print(" Inode: "); print_uint(st.st_ino); print_newline();
    print(" Owner: "); print_uint(st.st_uid); print_newline();

    syscall(SYSCALL_EXIT, 0, 0, 0);
    while (1);
}
