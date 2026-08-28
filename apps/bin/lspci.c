/**
 * @file lspci.c
 * @brief Lists the PCI devices the kernel found when it booted.
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
 * @brief Main entry point for the application
 */
void main(void) {
    char report[1600];
    int n;

    /*
     * No SYSCALL_GET_ARGS call, unlike free and whoami, which both make one and
     * then never look at the buffer. This command takes no arguments, and a
     * syscall whose result is discarded is not a placeholder for a future
     * argument - it is a line that reads as though something depends on it.
     *
     * The other half of the reason: the shell passes arguments to a program that
     * ignores them without complaint, so `lspci -v` prints the same list rather
     * than failing. That is the same thing free does and it is worth being
     * deliberate about rather than inheriting.
     */

    /*
     * Same division of labour as free: the kernel renders the figures and this
     * writes them out through descriptor 1, so the output goes into a pipe or a
     * file when it is asked to. free printed from inside the kernel until
     * v0.9.2, which is why `free > mem.txt` produced an empty file and reported
     * success, and there is no reason to build a second command that way.
     *
     * The buffer matches PCIINFO_BUF on the kernel side. A short one would not
     * be a fault - the kernel copies at most what it is given - but it would
     * silently lose the last devices on the bus, which is the half of the list
     * somebody running this is least likely to already know about.
     */
    n = syscall(69, (int)report, sizeof(report), 0); // SYSCALL_PCIINFO

    if (n < 0) {
        /*
         * The kernel refuses this to anyone but root, so the usual reason to be
         * here is a shell that is not root rather than a machine with no bus.
         */
        print("lspci: cannot read the PCI inventory\n");
        syscall(1, 1, 0, 0); // EXIT
        while (1);
    }

    if (n > 0) syscall(4, 1, (int)report, n); // SYSCALL_WRITE

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
