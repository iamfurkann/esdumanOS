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
 * @brief Main entry point for the clear process.
 * 
 * Invokes the system call to clear the terminal screen and exits safely.
 */
void main(void) {
    syscall(10, 0, 0, 0); 
    syscall(1, 0, 0, 0); 
    while(1);
}