/**
 * @file date.c
 * @brief Prints the current date and time
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
    
    
    // We don't have a RTC syscall yet. We will just print a dummy date or uptime.
    // For now, let's just use meminfo to show system is alive, 
    // or add a dummy date output.
    print("Thu Jul 23 23:08:00 UTC 2026"); print_newline();
    

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
