/**
 * @file mount.c
 * @brief Chooses which block device the file system is on, or lists them.
 *
 * With no argument it prints the registered devices and marks the one in use.
 * With a device name it moves the file system there.
 *
 * It is not POSIX mount and does not pretend to be: there is no mount point
 * argument because there is one file system at a time, and this changes which
 * disk carries it rather than attaching a second one somewhere. include/fs.h
 * says why, and v1.12.0 is where that changes.
 *
 * Syscall numbers are written out as literals here, as in every other program in
 * /bin - tests/host/c/test_elf_sast.c asserts that each program contains the
 * literal call text for the syscalls it is supposed to make.
 */

/**
 * @brief Invokes a system call.
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/** @brief Prints a string to standard output. */
static void print(const char *str) {
    int len = 0;
    while (str[len]) len++;
    syscall(4, 1, (int)str, len);   /* SYSCALL_WRITE */
}

/** @brief Prints a newline. */
static void nl(void) {
    syscall(4, 1, (int)"\n", 1);
}

/**
 * @brief Main entry point.
 */
void main(void) {
    char args[160];
    char report[320];
    int i = 0;

    for (int k = 0; k < 160; k++) args[k] = '\0';
    syscall(42, (int)args, 0, 0);   /* SYSCALL_GET_ARGS */

    /* Trailing spaces are not a device name. The shell hands arguments through
     * verbatim, so "mount " with nothing after it has to read as no argument
     * rather than as a device called the empty string. */
    while (args[i] == ' ') i++;

    if (args[i] == '\0') {
        /*
         * No argument: ask rather than set. The kernel renders and this writes,
         * which is the division of labour lspci and lsusb use and the reason
         * `mount > devices.txt` produces a file with something in it.
         */
        int n = syscall(71, 0, (int)report, sizeof(report));   /* SYSCALL_MOUNT */

        if (n < 0) {
            print("mount: cannot read the device list"); nl();
            syscall(1, 1, 0, 0);
            while (1) { }
        }

        if (n > 0) syscall(4, 1, (int)report, n);
        syscall(1, 0, 0, 0);
        while (1) { }
    }

    int rc = syscall(71, (int)&args[i], 0, 0);   /* SYSCALL_MOUNT */

    if (rc == 0) {
        syscall(1, 0, 0, 0);
        while (1) { }
    }

    /*
     * The three refusals a person will actually meet, told apart. "It did not
     * work" would send somebody looking at the disk when the answer is that they
     * have a file open, or that they typed a name nothing answers to.
     */
    if (rc == -19)      print("mount: no device by that name");
    else if (rc == -16) print("mount: something has a file open");
    else if (rc == -1)  print("mount: only root may change disks");
    else if (rc == -22) print("mount: that device holds no file system this kernel can read");
    else                print("mount: failed");
    nl();

    syscall(1, 1, 0, 0);
    while (1) { }
}
