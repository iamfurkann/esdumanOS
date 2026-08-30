/**
 * @file umount.c
 * @brief Unmounts the file system.
 *
 * Takes no argument, because there is one file system to unmount. That is the
 * same limit `mount` states and it is stated in one more place here rather than
 * left to be discovered: a program that accepted a path and ignored it would be
 * the shape of lie `su` carried for eleven releases.
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
    int rc = syscall(72, 0, 0, 0);   /* SYSCALL_UMOUNT */

    if (rc == 0) {
        /*
         * Said out loud, unlike most commands here. Unmounting leaves the system
         * with no file system at all - no /bin, no /etc - and a command that did
         * that silently would look like it had done nothing until the next thing
         * failed.
         */
        print("Unmounted. Nothing is mounted now; use mount <device>."); nl();
        syscall(1, 0, 0, 0);
        while (1) { }
    }

    if (rc == -19)      print("umount: nothing is mounted");
    else if (rc == -16) print("umount: something has a file open");
    else if (rc == -1)  print("umount: only root may unmount");
    else if (rc == -5)  print("umount: the disk would not take its unwritten data; still mounted");
    else                print("umount: failed");
    nl();

    syscall(1, 1, 0, 0);
    while (1) { }
}
