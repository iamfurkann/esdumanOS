/* Detach the data volume at /mnt. */
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
    int rc = syscall(72, 0, 0, 0);   /* SYSCALL_UMOUNT */

    if (rc == 0) {
        print("Unmounted /mnt."); nl();
        syscall(1, 0, 0, 0);
        while (1) { }
    }

    if (rc == -19)      print("umount: no filesystem at /mnt");
    else if (rc == -16) print("umount: a process has an open file or working directory on /mnt");
    else if (rc == -1)  print("umount: only root may unmount");
    else if (rc == -5)  print("umount: the disk would not take its unwritten data; still mounted");
    else                print("umount: failed");
    nl();

    syscall(1, 1, 0, 0);
    while (1) { }
}
