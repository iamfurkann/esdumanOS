/**
 * @file wc.c
 * @brief Counts lines, words and bytes in a file or on standard input
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
 * @brief Prints an unsigned number right-aligned in an eight-column field.
 *
 * Written out here rather than pulled from libft: the /bin tools are each
 * compiled as a single freestanding translation unit with no library linked, so
 * every one of them carries the helpers it needs.
 *
 * @param n Value to print.
 */
static void print_count(unsigned int n) {
    char digits[12];
    int len = 0;

    if (n == 0) {
        digits[len++] = '0';
    } else {
        while (n > 0) {
            digits[len++] = (char)('0' + (n % 10));
            n /= 10;
        }
    }

    for (int pad = len; pad < 8; pad++) syscall(4, 1, (int)" ", 1);
    for (int i = len - 1; i >= 0; i--) syscall(4, 1, (int)&digits[i], 1);
}

static unsigned int count_lines = 0;
static unsigned int count_words = 0;
static unsigned int count_bytes = 0;

/**
 * @brief Reads a descriptor to the end, accumulating the three counts.
 *
 * A word is a maximal run of non-whitespace, so the in_word flag has to survive
 * across reads: a chunk boundary can fall in the middle of one, and resetting
 * per chunk would count that word twice.
 *
 * @param fd Descriptor to read; 0 is standard input.
 */
static void wc_fd(int fd) {
    char buf[128];
    int in_word = 0;
    int n;

    while ((n = syscall(3, fd, (int)buf, 128)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            count_bytes++;
            if (c == '\n') count_lines++;

            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                count_words++;
            }
        }
    }
}

/**
 * @brief Main entry point for the application
 */
void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    int status = 0;

    if (args_buf[0] == '\0') {
        /*
         * No file named means read standard input, which is the case this tool
         * exists for. Its output is three numbers however much it consumed, so
         * "something | wc" is the cheapest way to see that a pipeline stage
         * actually reached the end of its input rather than stopping early.
         *
         * Descriptor 0 is not closed afterwards: it belongs to whoever started
         * this process.
         */
        wc_fd(0);
        print_count(count_lines);
        print_count(count_words);
        print_count(count_bytes);
        print_newline();
    } else {
        int fd = syscall(40, (int)args_buf, 0, 0); // SYSCALL_OPEN
        if (fd < 0) { print("wc: File not found"); print_newline(); status = 1; }
        else {
            wc_fd(fd);
            syscall(38, fd, 0, 0); // SYSCALL_CLOSE

            print_count(count_lines);
            print_count(count_words);
            print_count(count_bytes);
            print(" ");
            print(args_buf);
            print_newline();
        }
    }

    syscall(1, status, 0, 0); // EXIT
    while(1);
}
