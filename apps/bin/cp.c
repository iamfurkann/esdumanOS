/**
 * @file cp.c
 * @brief Copies a file
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
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    // The shell will pass the canonical absolute path as the argument string.
    // E.g. for "touch a.txt", the shell passes "/current/path/a.txt"
    
    
    int status = 0;

    int i = 0;
    while(args_buf[i] && args_buf[i] != ' ') i++;
    if (args_buf[i] == ' ') {
        args_buf[i] = '\0';
        char *src = args_buf;
        char *dst = &args_buf[i+1];
        
        int fd_in = syscall(40, (int)src, 0, 0); // SYSCALL_OPEN (O_RDONLY)
        if (fd_in < 0) { print("cp: Source not found"); print_newline(); status = 1; }
        else {
            int res = syscall(8, (int)dst, (int)"", 0); // SYSCALL_CREATE_FILE
            if (res < 0) { print("cp: Error creating destination"); print_newline(); status = 1; }
            else {
                int fd_out = syscall(40, (int)dst, 1, 0); // SYSCALL_OPEN (O_WRONLY)
                if (fd_out < 0) {
                    /* Silently skipped before, leaving an empty destination and
                     * a success exit. */
                    print("cp: Error opening destination"); print_newline();
                    status = 1;
                } else {
                    char buf[64];
                    int bytes_read;
                    while ((bytes_read = syscall(3, fd_in, (int)buf, 64)) > 0) {
                        /*
                         * The result of the write was discarded, which is why a
                         * copy silently produced a zero-byte file: the kernel
                         * has no write path for a regular file descriptor yet
                         * (sys_write handles console, pipes and devices only),
                         * so every one of these returns E_BADF.
                         *
                         * Reporting it does not make cp work - that needs the
                         * kernel-side write support - but it turns silent data
                         * loss into a visible failure.
                         */
                        if (syscall(4, fd_out, (int)buf, bytes_read) < 0) { // SYSCALL_WRITE
                            print("cp: Error writing to destination"); print_newline();
                            status = 1;
                            break;
                        }
                    }
                    syscall(38, fd_out, 0, 0); // SYSCALL_CLOSE
                }
            }
            syscall(38, fd_in, 0, 0); // SYSCALL_CLOSE
        }
    } else {
        print("Usage: cp <src> <dst>"); print_newline();
        status = 1;
    }


    syscall(1, status, 0, 0); // EXIT
    while(1);
}
