/**
 * @file lsusb.c
 * @brief Lists the USB controller and ports the kernel found when it booted.
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
    char report[2240];
    int n;

    /*
     * No SYSCALL_GET_ARGS call, for the reason lspci gives: this command takes
     * no arguments, and a syscall whose result is discarded reads as though
     * something depends on it. The shell hands arguments to a program that
     * ignores them without complaint, so `lsusb -v` prints the same list rather
     * than failing.
     */

    /*
     * The kernel renders and this writes, so `lsusb > usb.txt` produces a file
     * with something in it. free printed from inside the kernel until v0.9.2 and
     * that is exactly what it got wrong.
     *
     * The buffer matches USBINFO_BUF on the kernel side, which grew in v1.9.0
     * when the report started carrying two lines per device as well as one per
     * port. A short one here would not be a fault - the kernel copies at most
     * what it is given - but it would silently lose the end of the list, and on
     * a machine where something is plugged into a high-numbered port that is the
     * half the person running this is looking for.
     *
     * The number is written out rather than derived, because this file is
     * compiled freestanding and cannot include the kernel's headers. That makes
     * it a number in two places, which is the arrangement this project distrusts
     * on principle - so xhci.h names this file where USBINFO_BUF is defined, and
     * whoever moves one goes looking for the other.
     */
    n = syscall(70, (int)report, sizeof(report), 0); // SYSCALL_USBINFO

    if (n < 0) {
        /*
         * The kernel refuses this to anyone but root, so the usual reason to be
         * here is a shell that is not root rather than a machine with no
         * controller - a machine with no controller gets a line saying so and a
         * successful return.
         */
        print("lsusb: cannot read the USB inventory\n");
        syscall(1, 1, 0, 0); // EXIT
        while (1);
    }

    if (n > 0) syscall(4, 1, (int)report, n); // SYSCALL_WRITE

    syscall(1, 0, 0, 0); // EXIT
    while(1);
}
