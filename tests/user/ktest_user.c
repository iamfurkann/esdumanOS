/*
 * File: ktest_user.c
 * Purpose: Ring 3 (user mode) half of the kernel self-test suite.
 *
 * The in-kernel modules under tests/kernel/ all run at CPL=0 against a synthetic
 * task, so they never exercise the privilege boundary itself. This program is
 * loaded as a real ELF binary into a real address space and executed via iret,
 * so every syscall it makes crosses Ring 3 -> Ring 0 for real: the trap gate,
 * the uaccess copy helpers, the pointer validator and the fd table are all on
 * the live path.
 *
 * Results are reported back to the kernel through SYSCALL_KTEST_REPORT, which
 * only exists in test builds. The final KT_DONE call makes the kernel print the
 * summary and shut QEMU down with the suite's exit code.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "syscall.h"
#include "errno.h"

/* Report kinds understood by sys_ktest_report() in tests/kernel/selftest.c. */
#define KT_FAIL 0
#define KT_PASS 1
#define KT_DONE 2

/**
 * @brief Performs a system call from user mode.
 *
 * @param num The system call number.
 * @param arg1 The first argument for the system call.
 * @param arg2 The second argument for the system call.
 * @param arg3 The third argument for the system call.
 * @return The result of the system call.
 */
static inline int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Reports a single assertion result to the kernel test framework.
 *
 * @param ok Non-zero if the assertion held.
 * @param msg Description of what was asserted.
 */
static void kt_assert(int ok, const char *msg) {
    syscall(SYSCALL_KTEST_REPORT, ok ? KT_PASS : KT_FAIL, (int)msg, 0);
}

#define KT_ASSERT(cond, msg) kt_assert((cond) ? 1 : 0, (msg))

/**
 * @brief Calculates the length of a null-terminated string.
 *
 * @param s The string to measure.
 * @return The length of the string.
 */
static int kt_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

/*
 * Addresses used as hostile syscall arguments. All of these must be rejected
 * when they arrive from Ring 3.
 */
#define ADDR_NULL        0x00000000u  /* null page                              */
#define ADDR_BELOW_USER  0x00000100u  /* below the 4 MB user-space floor        */
#define ADDR_JUST_BELOW  0x003FFFFFu  /* last byte before the user-space floor  */
#define ADDR_KERNEL_TEXT 0xC0100000u  /* kernel image                           */
#define ADDR_KERNEL_HEAP 0xD0000000u  /* kernel heap                            */
#define ADDR_RECURSIVE   0xFFFFF000u  /* recursive page directory               */

/**
 * @brief Ring 3 entry point of the self-test suite.
 *
 * Ordering note: assertions that could take the kernel down are deliberately
 * placed last, so that everything before them has already been reported.
 */
void main(void) {
    /* ------------------------------------------------------------------
     * 1. Prove we really are in Ring 3.
     *
     * Reading CS is unprivileged, so this is a non-destructive check. If the
     * suite ever regresses to running the payload at CPL=0 these two lines
     * fail loudly instead of the whole boundary silently going untested.
     * ------------------------------------------------------------------ */
    unsigned short cs = 0, ss = 0;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    asm volatile("mov %%ss, %0" : "=r"(ss));

    /*
     * Selector values come from init_gdt() in arch/x86/cpu/gdt.c. The table has
     * a Kernel Stack descriptor at index 3, so the Ring 3 segments sit one slot
     * higher than the conventional layout: user code is index 4 (0x20 | RPL 3 =
     * 0x23) and user data index 5 (0x28 | RPL 3 = 0x2B). See GDT_USER_CS and
     * GDT_USER_DS in include/gdt.h; user space cannot include that header, so
     * the values are repeated here.
     */
    KT_ASSERT((cs & 3) == 3, "[RING3] CPL is 3 (payload runs unprivileged)");
    KT_ASSERT(cs == 0x23, "[RING3] CS is the user code selector (GDT_USER_CS, 0x23)");
    KT_ASSERT(ss == 0x2B, "[RING3] SS is the user data selector (GDT_USER_DS, 0x2B)");

    /* ------------------------------------------------------------------
     * 2. Basic syscalls succeed over the real trap gate.
     * ------------------------------------------------------------------ */
    const char *banner = "[RING3] hello from CPL=3\n";
    int wr = syscall(SYSCALL_WRITE, 1, (int)banner, kt_strlen(banner));
    KT_ASSERT(wr == kt_strlen(banner), "[RING3] write() with a valid user buffer returns the byte count");

    int uid = syscall(SYSCALL_GETUID, 0, 0, 0);
    KT_ASSERT(uid >= 0, "[RING3] getuid() returns a valid uid");

    int nosys = syscall(9999, 0, 0, 0);
    KT_ASSERT(nosys < 0, "[RING3] unknown syscall number is rejected");

    /* ------------------------------------------------------------------
     * 3. Pointer validation, exercised from user mode.
     *
     * The in-kernel adversarial module makes the same calls, but from CPL=0
     * with pages the test itself mapped as user-accessible. These run against
     * a genuine user address space.
     * ------------------------------------------------------------------ */
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_NULL, 4) < 0,
              "[UACCESS] write() rejects a NULL user buffer");
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_BELOW_USER, 4) < 0,
              "[UACCESS] write() rejects a buffer below the user-space floor");
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_JUST_BELOW, 4) < 0,
              "[UACCESS] write() rejects a buffer straddling the user-space floor");
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_KERNEL_TEXT, 16) < 0,
              "[UACCESS] write() rejects a pointer into kernel text");
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_KERNEL_HEAP, 16) < 0,
              "[UACCESS] write() rejects a pointer into the kernel heap");
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, ADDR_RECURSIVE, 16) < 0,
              "[UACCESS] write() rejects a pointer at the recursive page directory");

    char scratch[64];
    KT_ASSERT(syscall(SYSCALL_READ, 0, ADDR_KERNEL_HEAP, 8) < 0,
              "[UACCESS] read() rejects a kernel destination buffer");
    KT_ASSERT(syscall(SYSCALL_GET_ARGS, ADDR_KERNEL_TEXT, 0, 0) < 0,
              "[UACCESS] get_args() rejects a kernel destination buffer");
    KT_ASSERT(syscall(SYSCALL_OPEN, ADDR_KERNEL_TEXT, 0, 0) < 0,
              "[UACCESS] open() rejects a kernel path pointer");

    /* A size that would overflow start+len must not wrap past the check. */
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, (int)scratch, (int)0x7FFFFFFF) < 0,
              "[UACCESS] write() rejects a length that runs off the end of user space");

    /* ------------------------------------------------------------------
     * 4. A full copy_from_user -> kernel -> copy_to_user round trip.
     * ------------------------------------------------------------------ */
    KT_ASSERT(syscall(SYSCALL_PIPE, ADDR_KERNEL_HEAP, 0, 0) < 0,
              "[UACCESS] pipe() rejects a kernel fd-pair buffer");

    unsigned int fds[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    int pres = syscall(SYSCALL_PIPE, (int)fds, 0, 0);
    KT_ASSERT(pres == 0, "[IPC] pipe() succeeds from Ring 3");

    if (pres == 0) {
        const char *payload = "esduman";
        int plen = kt_strlen(payload);

        int pw = syscall(SYSCALL_WRITE, (int)fds[1], (int)payload, plen);
        KT_ASSERT(pw == plen, "[IPC] pipe write() copies the full payload in from user space");

        char rbuf[16];
        for (int i = 0; i < 16; i++) rbuf[i] = 0;

        int pr = syscall(SYSCALL_READ, (int)fds[0], (int)rbuf, plen);
        KT_ASSERT(pr == plen, "[IPC] pipe read() copies the full payload back out to user space");

        int same = 1;
        for (int i = 0; i < plen; i++) {
            if (rbuf[i] != payload[i]) same = 0;
        }
        KT_ASSERT(same, "[IPC] pipe round trip preserves the payload byte for byte");

        syscall(SYSCALL_CLOSE, (int)fds[0], 0, 0);
        syscall(SYSCALL_CLOSE, (int)fds[1], 0, 0);
    }

    /* ------------------------------------------------------------------
     * 5. VFS reachable from Ring 3.
     * ------------------------------------------------------------------ */
    int mk = syscall(SYSCALL_MKDIR, (int)"ktestdir", 0, 0);
    KT_ASSERT(mk == E_OK, "[VFS] mkdir() succeeds from Ring 3");

    int dirid = syscall(SYSCALL_GET_DIR_ID, (int)"/ktestdir", 0, 0);
    KT_ASSERT(dirid > 0, "[VFS] get_dir_id() resolves the directory just created");

    /*
     * Parent ids are a raw byte from user space and used to be stored without
     * being checked. An entry could therefore be created whose parent was
     * itself, and the parent walk in check_vfs_access() then spun forever.
     * Entry 250 is far above anything a normal boot allocates.
     */
    KT_ASSERT(syscall(SYSCALL_MKDIR, (int)"orphan", 250, 0) < 0,
              "[VFS] mkdir() rejects a parent id that names no directory");
    KT_ASSERT(syscall(SYSCALL_CREATE_FILE, (int)"orphanfile", (int)"x", 250) < 0,
              "[VFS] create_file() rejects a parent id that names no directory");

    /* ------------------------------------------------------------------
     * 6. DevFS.
     * ------------------------------------------------------------------ */
    int nullfd = syscall(SYSCALL_OPEN, (int)"/dev/null", 0, 0);
    KT_ASSERT(nullfd >= 0, "[DEVFS] open(/dev/null) succeeds");
    if (nullfd >= 0) {
        KT_ASSERT(syscall(SYSCALL_READ, nullfd, (int)scratch, 8) == 0,
                  "[DEVFS] read(/dev/null) returns 0 (EOF)");
        syscall(SYSCALL_CLOSE, nullfd, 0, 0);
    }

    /*
     * /dev/random is the one part of the K-9 finding that user space could read
     * for itself: it used to key its ChaCha20 stream from RDTSC and two RTC
     * registers, once per boot. It now draws every key from the kernel entropy
     * pool, and these assertions pin the contract that had to survive that
     * change — a full buffer, and different bytes on the next read.
     */
    char second[16];
    int randfd = syscall(SYSCALL_OPEN, (int)"/dev/random", 0, 0);
    KT_ASSERT(randfd >= 0, "[DEVFS] open(/dev/random) succeeds");
    if (randfd >= 0) {
        for (int i = 0; i < 16; i++) { scratch[i] = 0; second[i] = 0; }

        int n1 = syscall(SYSCALL_READ, randfd, (int)scratch, 16);
        int n2 = syscall(SYSCALL_READ, randfd, (int)second, 16);
        KT_ASSERT(n1 == 16 && n2 == 16,
                  "[DEVFS] read(/dev/random) fills the whole user buffer");

        int differs = 0;
        for (int i = 0; i < 16; i++) {
            if (scratch[i] != second[i]) differs = 1;
        }
        KT_ASSERT(differs, "[DEVFS] two reads of /dev/random return different bytes");
        syscall(SYSCALL_CLOSE, randfd, 0, 0);
    }

    int urandfd = syscall(SYSCALL_OPEN, (int)"/dev/urandom", 0, 0);
    KT_ASSERT(urandfd >= 0, "[DEVFS] open(/dev/urandom) succeeds");
    if (urandfd >= 0) {
        KT_ASSERT(syscall(SYSCALL_READ, urandfd, (int)scratch, 8) == 8,
                  "[DEVFS] read(/dev/urandom) returns the requested byte count");
        syscall(SYSCALL_CLOSE, urandfd, 0, 0);
    }

    /*
     * Regression guard for the devfs index contract.
     *
     * get_device_idx() reports failure as a negative errno (E_NOENT, -2), but
     * sys_open() used to compare it against -1. The descriptor was therefore
     * handed out carrying ptr == (uint32_t)-2, and the first read()/write() on
     * it evaluated dev_table[-2].read and called through whatever lay in front
     * of the table — an indirect call to attacker-influenced memory, reachable
     * by any unprivileged process.
     *
     * The open() must fail outright. If it ever succeeds again the descriptor is
     * closed without being read, so the test reports the regression instead of
     * triggering it.
     */
    int badfd = syscall(SYSCALL_OPEN, (int)"/dev/no_such_device", 0, 0);
    KT_ASSERT(badfd < 0, "[DEVFS] open() on a missing /dev node returns an error");
    if (badfd >= 0) {
        syscall(SYSCALL_CLOSE, badfd, 0, 0);
    }

    /* ------------------------------------------------------------------
     * 7. Last, because it is the one that can take the kernel down.
     *
     * sys_readdir() stores into the user buffer with plain pointer writes
     * instead of copy_to_user(). With SMAP active (make test_smap, -cpu max)
     * that faults inside the kernel with no fixup handler installed, which
     * lands in kernel_panic(). Everything above has already been reported by
     * the time we get here.
     * ------------------------------------------------------------------ */
    char dbuf[256];
    for (int i = 0; i < 256; i++) dbuf[i] = 0;

    KT_ASSERT(syscall(SYSCALL_READDIR, 0, ADDR_KERNEL_HEAP, 64) < 0,
              "[UACCESS] readdir() rejects a kernel destination buffer");

    int rd = syscall(SYSCALL_READDIR, 0, (int)dbuf, 256);
    KT_ASSERT(rd > 0, "[VFS] readdir() fills the user buffer from Ring 3");

    /*
     * A short buffer must truncate, not overflow. The listing is staged in the
     * kernel and handed over with one copy_to_user(), so the caller's size is
     * the only thing bounding the write.
     */
    struct { char buf[16]; char canary[16]; } rd_probe;
    for (int i = 0; i < 16; i++) { rd_probe.buf[i] = 0; rd_probe.canary[i] = 0x7E; }

    int rd_short = syscall(SYSCALL_READDIR, 0, (int)rd_probe.buf, 16);
    KT_ASSERT(rd_short >= 0 && rd_short <= 16, "[VFS] readdir() honours a short buffer");

    int canary_intact = 1;
    for (int i = 0; i < 16; i++) {
        if (rd_probe.canary[i] != 0x7E) canary_intact = 0;
    }
    KT_ASSERT(canary_intact, "[VFS] readdir() did not write past the caller's buffer");

    /*
     * sync() from Ring 3. The block cache is write-back, and the automatic policy
     * only bounds the loss window - it does not let a program decide that what it
     * just wrote must be on the disk now. So the syscall has to be reachable
     * unprivileged: making durability root-only would leave ordinary programs no
     * way to commit their own work.
     *
     * Called twice on purpose. The second call has nothing left to write, and
     * must still report success rather than treating an already-clean cache as a
     * failure.
     */
    KT_ASSERT(syscall(SYSCALL_SYNC, 0, 0, 0) == E_OK,
              "[BCACHE] sync() succeeds from an unprivileged Ring 3 process");
    KT_ASSERT(syscall(SYSCALL_SYNC, 0, 0, 0) == E_OK,
              "[BCACHE] sync() on an already-clean cache still reports success");

    /* ------------------------------------------------------------------
     * 8. Child process round trip.
     *
     * The only path that drives process teardown end to end: exec() a child,
     * the child exits, and the zombie reaper in schedule() releases both its
     * process_t and its address space. Nothing else in the suite reaches that
     * code, so without this the teardown wiring is written but never run.
     *
     * The return value is now checked exactly: sys_exec() publishes it before
     * sleep_current_task() swaps in another task's context, so the parent sees
     * a real status instead of the syscall number it went in with.
     * ------------------------------------------------------------------ */
    for (int child = 0; child < 3; child++) {
        int ex = syscall(SYSCALL_EXEC, (int)"/bin/hello", 0, 0);
        KT_ASSERT(ex == E_OK, "[PROC] exec() of a child that exits returns E_OK");
    }

    /* Still alive and still able to cross the boundary after three teardowns. */
    const char *after = "[PROC] parent survived the child teardowns\n";
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, (int)after, kt_strlen(after)) == kt_strlen(after),
              "[PROC] parent still functional after the reaper ran");

    /* ------------------------------------------------------------------
     * 9. LOCKDOWN.
     *
     * Deliberately the very last thing: it destroys the master key, and
     * security levels only ever go up, so every encrypted filesystem operation
     * from here on is refused and nothing can undo it. Anything that still
     * needs the disk has to have run already.
     * ------------------------------------------------------------------ */
    KT_ASSERT(syscall(SYSCALL_READDIR, 0, (int)dbuf, 256) > 0,
              "[SEC] the filesystem is still readable before lockdown");

    KT_ASSERT(syscall(SYSCALL_LOCKDOWN, 0, 0, 0) == 0,
              "[SEC] lockdown is accepted from root");

    /*
     * The key is gone now. LOCKDOWN outranks CRYPTO_ENFORCED, so the VFS still
     * selects the encrypted path - it has to refuse rather than fall back to an
     * all-zero key, which would hand back garbage on reads and destroy files on
     * writes.
     */
    int pw_fd = syscall(SYSCALL_OPEN, (int)"/etc/passwd", 0, 0);
    KT_ASSERT(pw_fd >= 0, "[SEC] /etc/passwd can still be opened after lockdown");
    if (pw_fd >= 0) {
        KT_ASSERT(syscall(SYSCALL_READ, pw_fd, (int)scratch, 8) < 0,
                  "[STRICT] [SEC] encrypted read is refused once the key is destroyed");
        syscall(SYSCALL_CLOSE, pw_fd, 0, 0);
    }

    /* LOCKDOWN is documented as blocking new tasks; nothing enforced it. */
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/hello", 0, 0) < 0,
              "[STRICT] [SEC] lockdown blocks starting new programs");

    /* Hand the verdict back to the kernel; it prints the summary and exits QEMU. */
    syscall(SYSCALL_KTEST_REPORT, KT_DONE, 0, 0);

    syscall(SYSCALL_EXIT, 0, 0, 0);
    while (1) { }
}
