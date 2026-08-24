/**
 * @file free.c
 * @brief Displays memory usage information
 */

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
    syscall(4, 1, (int)str, len);
}

/**
 * @brief Prints a newline character to the standard output
 */
void print_newline() {
    syscall(4, 1, (int)"\n", 1);
}

/**
 * @brief Main entry point for the application
 */
void main(void) {
    char args_buf[128];
    char report[256];
    int n;

    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    /*
     * The kernel renders the figures and this writes them.
     *
     * It used to be one call and nothing else: SYSCALL_MEMINFO printed straight
     * to the screen, so `free > mem.txt` produced an empty file and said it had
     * worked. The bytes come back here now and go out through descriptor 1,
     * which is what puts them in a pipe or a file.
     */
    n = syscall(15, (int)report, sizeof(report), 0); // SYSCALL_MEMINFO

    if (n < 0) {
        const char *msg = "free: cannot read memory information\n";
        int len = 0;
        while (msg[len]) len++;
        syscall(4, 1, (int)msg, len); // SYSCALL_WRITE
        syscall(1, 1, 0, 0);          // EXIT
        while (1);
    }

    if (n > 0) syscall(4, 1, (int)report, n); // SYSCALL_WRITE

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
