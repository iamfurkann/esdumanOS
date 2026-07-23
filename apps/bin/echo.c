/**
 * @brief Performs a system call.
 * 
 * This function triggers a software interrupt (0x80) to switch to kernel mode
 * and execute a system call.
 * 
 * @param num The system call number.
 * @param arg1 The first argument for the system call.
 * @param arg2 The second argument for the system call.
 * @param arg3 The third argument for the system call.
 * @return The result of the system call.
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Main entry point for the echo process.
 * 
 * Retrieves command-line arguments passed by the shell and prints them to
 * the standard output, followed by a newline.
 */
void main(void) {
    char args_buf[256];
    for (int k = 0; k < 256; k++) {
        args_buf[k] = '\0';
    }

    syscall(42, (int)args_buf, 0, 0); 
    
    int i = 0;
    while (args_buf[i] != ' ' && args_buf[i] != '\0') i++; 
    if (args_buf[i] == ' ') i++;
    
    if (args_buf[i] == '\0') {
        syscall(4, 1, (int)"\n", 1);
    } else {
        int len = 0;
        
        while (args_buf[i + len] != '\0' && (i + len) < 255) {
            len++;
        }
        syscall(4, 1, (int)&args_buf[i], len);
        syscall(4, 1, (int)"\n", 1);
    }
    
    syscall(1, 0, 0, 0);
    while(1);
}