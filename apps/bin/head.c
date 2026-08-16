/**
 * @file head.c
 * @brief Prints the first 10 lines of a file or of standard input
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
 * @brief Copies the first ten lines of a descriptor to standard output.
 *
 * Byte at a time, which is what lets it stop on the tenth newline without having
 * read past it. That matters more on a pipe than on a file: bytes consumed here
 * are gone, and a later stage would never see them.
 *
 * @param fd Descriptor to read; 0 is standard input.
 */
static void head_fd(int fd) {
    char c;
    int lines = 0;

    while (lines < 10 && syscall(3, fd, (int)&c, 1) > 0) {
        syscall(4, 1, (int)&c, 1);
        if (c == '\n') lines++;
    }
}

/**
 * @brief Main entry point for the application
 */
void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    // The shell will pass the canonical absolute path as the argument string.
    // E.g. for "touch a.txt", the shell passes "/current/path/a.txt"


    int status = 0;

    if (args_buf[0] == '\0') {
        /*
         * No file named means read standard input. This used to be a usage
         * error, which left the tool that most obviously belongs at the end of a
         * pipeline unable to sit there.
         *
         * Descriptor 0 is not closed afterwards: it belongs to whoever started
         * this process, and closing it would take their standard input away too.
         */
        head_fd(0);
    } else {
        int fd = syscall(40, (int)args_buf, 0, 0);
        if (fd < 0) { print("head: File not found"); print_newline(); status = 1; }
        else {
            head_fd(fd);
            syscall(38, fd, 0, 0);
        }
    }


    syscall(1, status, 0, 0); // EXIT
    while(1);
}
