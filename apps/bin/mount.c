/* List disks or attach a data filesystem at /mnt. */
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

    if (rc == -13) {
        char pass[64] = {0};
        unsigned used = 0;
        print("Disk passphrase: ");
        int too_long = 0;
        while (1) {
            char c;
            if (syscall(3, 0, (int)&c, 1) != 1) break;
            if (c == '\n' || c == '\r') {
                if (!too_long) rc = syscall(71, (int)&args[i], (int)pass, 0x4d4e5401u);
                break;
            }
            if (c == '\b' || c == 127) { if (used) pass[--used] = 0; }
            else if (c >= 32 && c <= 126) {
                if (used < sizeof(pass) - 1) pass[used++] = c;
                else too_long = 1;
            }
        }
        volatile char *wipe = pass;
        for (unsigned j = 0; j < sizeof(pass); j++) wipe[j] = 0;
        nl();
    }
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
    else if (rc == -16) print("mount: /mnt is busy");
    else if (rc == -13) print("mount: disk passphrase rejected");
    else if (rc == -39) print("mount: /mnt must be empty");
    else if (rc == -1)  print("mount: only root may change disks");
    else if (rc == -22) print("mount: that device holds no file system this kernel can read");
    else                print("mount: failed");
    nl();

    syscall(1, 1, 0, 0);
    while (1) { }
}
