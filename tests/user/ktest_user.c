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
#include "stat.h"

/* Report kinds understood by sys_ktest_report() in tests/kernel/selftest.c. */
#define KT_FAIL    0
#define KT_PASS    1
#define KT_DONE    2
#define KT_TICKS   3
#define KT_FREEMEM 4

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
 * @brief Reads the kernel tick counter.
 *
 * Ring 3 has no clock, so without this the sleep() assertions below could only
 * check that the call returned - which a sleep that never waited would also do.
 * Serviced only in test builds; see KT_REPORT_TICKS in tests/kernel/ktest.h.
 */
static int kt_ticks(void) {
    return syscall(SYSCALL_KTEST_REPORT, KT_TICKS, 0, 0);
}

/**
 * @brief Reads the physical allocator's free-memory figure, in KB.
 *
 * Ring 3 has no view of the frame allocator, and "the parent survived" does not
 * prove a dead task was actually reclaimed. Test builds only, like kt_ticks().
 */
static int kt_free_kb(void) {
    return syscall(SYSCALL_KTEST_REPORT, KT_FREEMEM, 0, 0);
}

/*
 * File type values from include/fs.h and the PIT rate from include/rtc.h.
 * Both headers declare kernel functions, so they are not included here; the
 * values are repeated instead, the same way the GDT selectors above are.
 */
#define FT_REGULAR_U 0
#define FT_DIR_U     1
#define TICKS_PER_SEC 100

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
     * K-10 was that parent ids arrived as a raw byte from user space and were
     * stored without being checked, so an entry could be created under a parent
     * that did not exist - and the parent walk in check_vfs_access() then spun
     * forever. That register is no longer consulted: the parent comes from the
     * process's working directory. The bug class is gone rather than guarded, so
     * what these assert now is that a garbage value in the old argument slot has
     * no effect at all, rather than being rejected.
     *
     * 250 is far above anything a normal boot allocates, so if it were still
     * being honoured the entry would land somewhere unreachable.
     */
    KT_ASSERT(syscall(SYSCALL_MKDIR, (int)"ktestdir2", 250, 0) == E_OK,
              "[VFS] mkdir() ignores the obsolete parent-id argument");
    KT_ASSERT(syscall(SYSCALL_GET_DIR_ID, (int)"/ktestdir2", 0, 0) > 0,
              "[STRICT] [VFS] the entry landed in the working directory, not under the bogus id");
    KT_ASSERT(syscall(SYSCALL_CREATE_FILE, (int)"ktestfile", (int)"x", 250) == E_OK,
              "[VFS] create_file() ignores the obsolete parent-id argument");

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
     * Working directory, from Ring 3.
     *
     * This is the only place the round trip means anything: the value lives in
     * the PCB, so a kernel-mode test would be inspecting the same struct it just
     * wrote. Here the payload can only reach it through the syscall boundary,
     * which is what has to work.
     */
    char cwd[128];
    for (int i = 0; i < 128; i++) cwd[i] = 0;

    KT_ASSERT(syscall(SYSCALL_GETCWD, (int)cwd, 128, 0) > 0,
              "[CWD] getcwd() reports a path from Ring 3");
    KT_ASSERT(cwd[0] == '/', "[CWD] the reported path is absolute");

    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/etc", 0, 0) == E_OK,
              "[CWD] chdir() into an existing directory succeeds");

    for (int i = 0; i < 128; i++) cwd[i] = 0;
    int cwd_len = syscall(SYSCALL_GETCWD, (int)cwd, 128, 0);
    KT_ASSERT(cwd_len == 4 && cwd[0] == '/' && cwd[1] == 'e' && cwd[2] == 't' && cwd[3] == 'c',
              "[STRICT] [CWD] getcwd() reflects the directory chdir() moved to");

    /*
     * The assertion the whole change exists for.
     *
     * "passwd" carries no directory information at all, and the third syscall
     * argument is 0. If the kernel were still taking its base directory from the
     * caller this would look in the root directory and fail; it succeeds only
     * because the lookup started from the cwd chdir() set.
     */
    int rel_fd = syscall(SYSCALL_OPEN, (int)"passwd", 0, 0);
    KT_ASSERT(rel_fd >= 0,
              "[STRICT] [CWD] a bare relative name resolves against the working directory");
    if (rel_fd >= 0) syscall(SYSCALL_CLOSE, rel_fd, 0, 0);

    /* And the same name must not resolve from root, where no such file exists. */
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/", 0, 0) == E_OK,
              "[CWD] chdir(\"/\") returns to root");
    KT_ASSERT(syscall(SYSCALL_OPEN, (int)"passwd", 0, 0) < 0,
              "[STRICT] [CWD] the same relative name fails from a directory that lacks it");

    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/etc", 0, 0) == E_OK,
              "[CWD] chdir() back into /etc for the parent-walk check");
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"..", 0, 0) == E_OK,
              "[CWD] chdir(\"..\") walks back toward root");

    for (int i = 0; i < 128; i++) cwd[i] = 0;
    KT_ASSERT(syscall(SYSCALL_GETCWD, (int)cwd, 128, 0) == 1 && cwd[0] == '/',
              "[STRICT] [CWD] \"..\" from /etc lands at root");

    /*
     * A failed chdir must not move the process. Checked explicitly because the
     * resolution and the commit are separate steps, and a version that wrote
     * cwd_id before validating would pass every assertion above.
     */
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/no_such_directory", 0, 0) < 0,
              "[CWD] chdir() to a missing directory is refused");
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/etc/passwd", 0, 0) < 0,
              "[STRICT] [CWD] chdir() to a regular file is refused, not accepted as a directory");

    for (int i = 0; i < 128; i++) cwd[i] = 0;
    KT_ASSERT(syscall(SYSCALL_GETCWD, (int)cwd, 128, 0) == 1 && cwd[0] == '/',
              "[STRICT] [CWD] a refused chdir left the process where it was");

    KT_ASSERT(syscall(SYSCALL_GETCWD, (int)cwd, 0, 0) < 0,
              "[CWD] getcwd() rejects a zero-sized buffer");
    KT_ASSERT(syscall(SYSCALL_GETCWD, ADDR_KERNEL_HEAP, 128, 0) < 0,
              "[UACCESS] getcwd() rejects a kernel destination buffer");

    /*
     * End to end: exec a real tool with a bare filename and check where the file
     * lands. This is the path that was broken - the kernel resolved against the
     * working directory correctly, but the shell was still pasting the current
     * directory onto the front of the argument string, joined with a space. A
     * tool that treats its whole argument string as one filename then created
     * "/home notes.txt" instead of notes.txt inside /home.
     *
     * Nothing above catches that: every assertion so far calls the syscalls
     * directly, and the defect lived in how arguments were assembled before the
     * syscall was ever reached.
     *
     * /tmp because check_vfs_access() allows writes there regardless of uid.
     */
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/tmp", 0, 0) == E_OK,
              "[CWD] chdir(/tmp) for the exec argument check");

    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/touch", 0, (int)"anchor.txt") == E_OK,
              "[PROC] exec() of a tool with a bare filename argument");

    int made = syscall(SYSCALL_OPEN, (int)"anchor.txt", 0, 0);
    KT_ASSERT(made >= 0,
              "[STRICT] [CWD] the tool created its file in the working directory");
    if (made >= 0) syscall(SYSCALL_CLOSE, made, 0, 0);

    /* And not in root, which is where a mangled argument string would put it. */
    KT_ASSERT(syscall(SYSCALL_OPEN, (int)"/anchor.txt", 0, 0) < 0,
              "[STRICT] [CWD] and not in root under a mangled name");

    syscall(SYSCALL_CHDIR, (int)"/", 0, 0);

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
     * 8. File and process introspection.
     *
     * stat/fstat/lseek/getpid/sleep, from the only side where they mean
     * anything: a kernel-mode test would be reading back the same structs it
     * had just written, whereas here every field has to survive copy_to_user()
     * and every argument has to survive validation.
     *
     * Before the lockdown section, because stat() on an encrypted file reads
     * that file's header and the key is destroyed down there.
     * ------------------------------------------------------------------ */
    esd_stat_t st;
    esd_stat_t st2;

    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/tmp", 0, 0) == E_OK,
              "[STAT] chdir(/tmp), which is writable regardless of uid");

    /* 16 bytes, chosen so the encrypted form pads to a different length. */
    const char *probe = "0123456789abcdef";
    int probe_len = kt_strlen(probe);

    KT_ASSERT(syscall(SYSCALL_CREATE_FILE, (int)"statprobe.txt", (int)probe, 0) == E_OK,
              "[STAT] probe file created");

    KT_ASSERT(syscall(SYSCALL_STAT, (int)"statprobe.txt", (int)&st, 0) == E_OK,
              "[STAT] stat() of a relative name succeeds");

    /*
     * The assertion this whole helper exists for. dir_table records the
     * encrypted form's length - an IV, a header and padding - so a stat() built
     * on it would be wrong for every regular file on a default boot, where
     * SEC_LEVEL_CRYPTO_ENFORCED is the starting level.
     */
    KT_ASSERT(st.st_size == (unsigned int)probe_len,
              "[STRICT] [STAT] st_size is the byte count written, not the size on disk");
    KT_ASSERT(st.st_type == FT_REGULAR_U, "[STAT] a regular file reports FT_REGULAR");

    if (st.st_encrypted) {
        KT_ASSERT(st.st_disk_size > st.st_size,
                  "[STRICT] [STAT] the encrypted form on disk is larger than the plaintext");
    } else {
        KT_ASSERT(st.st_disk_size == st.st_size,
                  "[STAT] unencrypted, the two sizes agree");
    }

    /* And what stat() promised is what a read actually yields. */
    int pfd = syscall(SYSCALL_OPEN, (int)"statprobe.txt", 0, 0);
    KT_ASSERT(pfd >= 0, "[STAT] probe file opened");

    if (pfd >= 0) {
        char rd[32];
        for (int i = 0; i < 32; i++) rd[i] = 0;

        KT_ASSERT(syscall(SYSCALL_READ, pfd, (int)rd, 32) == probe_len,
                  "[STRICT] [STAT] read() returns exactly the count stat() predicted");

        /* fstat() must describe the same entry, field for field. */
        KT_ASSERT(syscall(SYSCALL_FSTAT, pfd, (int)&st2, 0) == E_OK,
                  "[STAT] fstat() on an open descriptor succeeds");
        KT_ASSERT(st2.st_size == st.st_size && st2.st_ino == st.st_ino &&
                  st2.st_type == st.st_type && st2.st_disk_size == st.st_disk_size &&
                  st2.st_uid == st.st_uid && st2.st_parent == st.st_parent,
                  "[STRICT] [STAT] fstat() agrees with stat() on every field");

        /* ---- lseek ---- */
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 0, SEEK_SET) == 0,
                  "[LSEEK] SEEK_SET to 0 reports the new offset");

        for (int i = 0; i < 32; i++) rd[i] = 0;
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 10, SEEK_SET) == 10,
                  "[LSEEK] SEEK_SET past the start reports the requested offset");
        KT_ASSERT(syscall(SYSCALL_READ, pfd, (int)rd, 32) == probe_len - 10,
                  "[STRICT] [LSEEK] a read after seeking returns only the remaining bytes");
        KT_ASSERT(rd[0] == 'a',
                  "[STRICT] [LSEEK] and returns the bytes from the offset, not from the start");

        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 4, SEEK_SET) == 4,
                  "[LSEEK] SEEK_SET repositions again");
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 3, SEEK_CUR) == 7,
                  "[STRICT] [LSEEK] SEEK_CUR composes with the current position");
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, -2, SEEK_CUR) == 5,
                  "[LSEEK] SEEK_CUR accepts a negative offset");

        /*
         * SEEK_END is the case that breaks if the end is taken from the
         * directory table: the encrypted length is larger, so this would land
         * past the plaintext and the read below would still return 0 - passing
         * for the wrong reason. The offset itself is therefore checked too.
         */
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 0, SEEK_END) == probe_len,
                  "[STRICT] [LSEEK] SEEK_END lands on the plaintext end, not the on-disk end");
        KT_ASSERT(syscall(SYSCALL_READ, pfd, (int)rd, 32) == 0,
                  "[LSEEK] a read at the end returns 0");

        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, -4, SEEK_END) == probe_len - 4,
                  "[LSEEK] SEEK_END with a negative offset walks back from the end");

        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, -1, SEEK_SET) < 0,
                  "[LSEEK] an offset before the start of the file is refused");
        KT_ASSERT(syscall(SYSCALL_LSEEK, pfd, 0, 99) < 0,
                  "[LSEEK] an unknown whence is refused");

        syscall(SYSCALL_CLOSE, pfd, 0, 0);
    }

    /* Directories, and the root, which owns no directory table entry. */
    KT_ASSERT(syscall(SYSCALL_STAT, (int)"/etc", (int)&st, 0) == E_OK && st.st_type == FT_DIR_U,
              "[STAT] stat() of a directory reports FT_DIR");
    KT_ASSERT(syscall(SYSCALL_STAT, (int)"/", (int)&st, 0) == E_OK &&
              st.st_type == FT_DIR_U && st.st_ino == 0,
              "[STRICT] [STAT] stat(\"/\") describes the root, which has no table entry");

    /* Failure paths. */
    KT_ASSERT(syscall(SYSCALL_STAT, (int)"/no_such_file_at_all", (int)&st, 0) < 0,
              "[STAT] stat() of a missing path is refused");
    KT_ASSERT(syscall(SYSCALL_STAT, (int)"statprobe.txt", ADDR_KERNEL_HEAP, 0) < 0,
              "[UACCESS] stat() rejects a kernel destination buffer");
    KT_ASSERT(syscall(SYSCALL_STAT, ADDR_KERNEL_TEXT, (int)&st, 0) < 0,
              "[UACCESS] stat() rejects a kernel path pointer");
    KT_ASSERT(syscall(SYSCALL_FSTAT, 999, (int)&st, 0) < 0,
              "[STAT] fstat() rejects an out-of-range descriptor");

    /*
     * A pipe has no directory entry, and a /dev descriptor stores a device table
     * index in the same field a file descriptor stores a pointer. Both must be
     * refused by type rather than dereferenced - that field overload is what
     * made a stale comparison in open() an indirect call through dev_table[-2].
     */
    unsigned int sfds[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
    if (syscall(SYSCALL_PIPE, (int)sfds, 0, 0) == 0) {
        KT_ASSERT(syscall(SYSCALL_FSTAT, (int)sfds[0], (int)&st, 0) < 0,
                  "[STRICT] [STAT] fstat() on a pipe is refused, not answered with garbage");
        KT_ASSERT(syscall(SYSCALL_LSEEK, (int)sfds[0], 0, SEEK_SET) < 0,
                  "[STRICT] [LSEEK] lseek() on a pipe is refused");
        syscall(SYSCALL_CLOSE, (int)sfds[0], 0, 0);
        syscall(SYSCALL_CLOSE, (int)sfds[1], 0, 0);
    }

    int devfd = syscall(SYSCALL_OPEN, (int)"/dev/null", 0, 0);
    if (devfd >= 0) {
        KT_ASSERT(syscall(SYSCALL_FSTAT, devfd, (int)&st, 0) < 0,
                  "[STRICT] [STAT] fstat() on a /dev node is refused");
        KT_ASSERT(syscall(SYSCALL_LSEEK, devfd, 0, SEEK_END) < 0,
                  "[STRICT] [LSEEK] lseek() on a /dev node is refused");
        syscall(SYSCALL_CLOSE, devfd, 0, 0);
    }

    /* ---- getpid ---- */
    int pid1 = syscall(SYSCALL_GETPID, 0, 0, 0);
    int pid2 = syscall(SYSCALL_GETPID, 0, 0, 0);
    KT_ASSERT(pid1 > 0, "[PROC] getpid() returns a positive pid");
    KT_ASSERT(pid1 == pid2, "[PROC] getpid() is stable across calls");

    /* ---- sleep ---- */
    KT_ASSERT(syscall(SYSCALL_SLEEP, 0, 0, 0) == E_OK,
              "[SLEEP] a zero delay returns immediately");
    KT_ASSERT(syscall(SYSCALL_SLEEP, -1, 0, 0) < 0,
              "[SLEEP] a negative delay is refused");

    /*
     * Measured, not assumed. A sleep() that returned without ever blocking
     * would satisfy every assertion above; only the tick counter tells the two
     * apart. 500 ms is 50 ticks at TIMER_HZ.
     *
     * The upper bound is deliberately loose - one whole extra second. The task
     * is woken by a sweep in schedule(), so it resumes at the next scheduling
     * point after its deadline rather than exactly on it, and under an emulator
     * that slack is worth allowing for. The lower bound is the one carrying the
     * claim.
     */
    int t0 = kt_ticks();
    int slept = syscall(SYSCALL_SLEEP, 500, 0, 0);
    int elapsed = kt_ticks() - t0;

    KT_ASSERT(slept == E_OK, "[SLEEP] sleep(500) reports success");
    KT_ASSERT(elapsed >= 50,
              "[STRICT] [SLEEP] sleep(500) really waited at least 50 ticks");
    KT_ASSERT(elapsed < 50 + TICKS_PER_SEC,
              "[SLEEP] and woke within a second of its deadline");

    /* The pid survived blocking and being rescheduled. */
    KT_ASSERT(syscall(SYSCALL_GETPID, 0, 0, 0) == pid1,
              "[PROC] the pid is unchanged after blocking in sleep()");

    syscall(SYSCALL_RM_FILE, (int)"statprobe.txt", 0, 0);
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/", 0, 0) == E_OK,
              "[STAT] back to root for the remaining sections");

    /* ------------------------------------------------------------------
     * 9. Writing to a file through a descriptor.
     *
     * sys_write had no branch for a regular file at all, so every write to one
     * returned E_BADF. That is why /bin/cp produced an empty destination and
     * why the shell's ">" could never be wired up.
     *
     * The semantics under test are deliberately narrower than POSIX, because
     * the stored form allows nothing wider: a file is one AES-CBC blob
     * authenticated over its whole plaintext, so it can be replaced but never
     * appended to. Writes are therefore buffered and committed once, when the
     * last descriptor closes.
     * ------------------------------------------------------------------ */
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/tmp", 0, 0) == E_OK,
              "[WRITE] chdir(/tmp) for the write tests");

    KT_ASSERT(syscall(SYSCALL_CREATE_FILE, (int)"wtest.txt", (int)"", 0) == E_OK,
              "[WRITE] target file created");

    const char *wpayload = "written through a descriptor";
    int wlen = kt_strlen(wpayload);

    int wfd = syscall(SYSCALL_OPEN, (int)"wtest.txt", 1, 0);
    KT_ASSERT(wfd >= 0, "[WRITE] open() for writing succeeds");

    if (wfd >= 0) {
        KT_ASSERT(syscall(SYSCALL_WRITE, wfd, (int)wpayload, wlen) == wlen,
                  "[STRICT] [WRITE] write() to a file reports the full byte count");

        /*
         * The commit is on close, and this is what proves it: the file on disk
         * is still the empty one until the descriptor goes away.
         */
        KT_ASSERT(syscall(SYSCALL_STAT, (int)"wtest.txt", (int)&st, 0) == E_OK && st.st_size == 0,
                  "[STRICT] [WRITE] the file is unchanged while the descriptor is open");

        KT_ASSERT(syscall(SYSCALL_CLOSE, wfd, 0, 0) == E_OK,
                  "[WRITE] close() commits the buffered write");

        KT_ASSERT(syscall(SYSCALL_STAT, (int)"wtest.txt", (int)&st, 0) == E_OK &&
                  st.st_size == (unsigned int)wlen,
                  "[STRICT] [WRITE] and the new size is visible once it has closed");

        /* And the bytes survived the encrypt/decrypt round trip. */
        int rfd = syscall(SYSCALL_OPEN, (int)"wtest.txt", 0, 0);
        KT_ASSERT(rfd >= 0, "[WRITE] the written file reopens for reading");
        if (rfd >= 0) {
            char rb[64];
            for (int i = 0; i < 64; i++) rb[i] = 0;

            KT_ASSERT(syscall(SYSCALL_READ, rfd, (int)rb, 64) == wlen,
                      "[STRICT] [WRITE] read() returns exactly the bytes written");

            int same = 1;
            for (int i = 0; i < wlen; i++) { if (rb[i] != wpayload[i]) same = 0; }
            KT_ASSERT(same, "[STRICT] [WRITE] and they come back byte for byte");

            /* A descriptor opened for reading must refuse writes. */
            KT_ASSERT(syscall(SYSCALL_WRITE, rfd, (int)"x", 1) < 0,
                      "[STRICT] [WRITE] writing to a read-only descriptor is refused");

            syscall(SYSCALL_CLOSE, rfd, 0, 0);
        }
    }

    /*
     * Opening for writing truncates. Open and close without writing anything,
     * and the file must come back empty - that is what "> file" has to mean, and
     * it is why the dirty flag is set at open rather than at the first write.
     */
    int tfd = syscall(SYSCALL_OPEN, (int)"wtest.txt", 1, 0);
    KT_ASSERT(tfd >= 0, "[WRITE] reopening for writing succeeds");
    if (tfd >= 0) {
        KT_ASSERT(syscall(SYSCALL_CLOSE, tfd, 0, 0) == E_OK,
                  "[WRITE] closing it again commits");
        KT_ASSERT(syscall(SYSCALL_STAT, (int)"wtest.txt", (int)&st, 0) == E_OK && st.st_size == 0,
                  "[STRICT] [WRITE] opening for writing truncated the file");
    }

    /*
     * The commit belongs to the LAST reference, not to every close.
     *
     * dup2() can point several descriptors at one open file. Committing on each
     * close would rewrite the file repeatedly from a partial buffer, and the
     * first close would publish a half-written file.
     */
    int d1 = syscall(SYSCALL_OPEN, (int)"wtest.txt", 1, 0);
    KT_ASSERT(d1 >= 0, "[WRITE] open for the duplicate-descriptor check");
    if (d1 >= 0) {
        KT_ASSERT(syscall(SYSCALL_WRITE, d1, (int)"abc", 3) == 3,
                  "[WRITE] three bytes buffered");
        KT_ASSERT(syscall(SYSCALL_DUP2, d1, 9, 0) == 9,
                  "[WRITE] dup2() gives a second descriptor onto the same file");

        syscall(SYSCALL_CLOSE, d1, 0, 0);
        KT_ASSERT(syscall(SYSCALL_STAT, (int)"wtest.txt", (int)&st, 0) == E_OK && st.st_size == 0,
                  "[STRICT] [WRITE] closing one of two descriptors does not commit");

        KT_ASSERT(syscall(SYSCALL_CLOSE, 9, 0, 0) == E_OK,
                  "[WRITE] closing the last one does");
        KT_ASSERT(syscall(SYSCALL_STAT, (int)"wtest.txt", (int)&st, 0) == E_OK && st.st_size == 3,
                  "[STRICT] [WRITE] and the file now holds what was buffered");
    }

    /*
     * End to end through a real tool. /bin/cp has always done the right thing -
     * open, read, write, close - and produced an empty file because the kernel
     * dropped every write. This is the assertion that says it works now, and it
     * goes through exec and the argument string rather than calling the
     * syscalls directly, which is where v0.3.1's defect lived.
     */
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/cp", 0, (int)"wtest.txt wcopy.txt") == 0,
              "[WRITE] cp reports success");

    esd_stat_t cst;
    KT_ASSERT(syscall(SYSCALL_STAT, (int)"wcopy.txt", (int)&cst, 0) == E_OK,
              "[WRITE] cp created the destination");
    KT_ASSERT(cst.st_size == 3,
              "[STRICT] [WRITE] and copied the contents rather than leaving it empty");

    syscall(SYSCALL_RM_FILE, (int)"wtest.txt", 0, 0);
    syscall(SYSCALL_RM_FILE, (int)"wcopy.txt", 0, 0);
    KT_ASSERT(syscall(SYSCALL_CHDIR, (int)"/", 0, 0) == E_OK,
              "[WRITE] back to root");

    /* ------------------------------------------------------------------
     * 10. Child process round trip.
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
        KT_ASSERT(ex == 0, "[PROC] exec() of a child that exits 0 returns 0");
    }

    /*
     * The child's status has to survive the trip up.
     *
     * exit() discarded its argument and nothing anywhere held a status, so a
     * parent learned only that its child had finished. exec() therefore always
     * reported success, and the shell's && and || could not tell one outcome
     * from the other: "stat /no_such_file && echo CHAINED" printed CHAINED.
     *
     * /bin/stat exits 1 when it cannot stat its argument, which makes it the
     * one tool already in the image that can show a non-zero status arriving
     * intact instead of being flattened to E_OK. The success case is asserted
     * beside it, because a version that simply returned 1 unconditionally would
     * satisfy the first assertion on its own.
     */
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/stat", 0, (int)"/no_such_file_at_all") == 1,
              "[STRICT] [PROC] exec() returns the child's non-zero exit status");
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/stat", 0, (int)"/etc") == 0,
              "[STRICT] [PROC] and returns 0 when the same child succeeds");

    /*
     * A usage error is a failure too. /bin/stat exited 0 when given no argument
     * at all, which reported "no file given" as success - harmless while
     * statuses went nowhere, and wrong the moment they started arriving.
     */
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/stat", 0, 0) == 1,
              "[STRICT] [PROC] a tool invoked with no argument exits non-zero");

    /* ------------------------------------------------------------------
     * A child that crashes.
     *
     * Until v0.4.2 the page-fault handler's entire teardown was marking the
     * task dead and rescheduling. exit_current_process() never ran, so the
     * parent waiting on WAIT_CHILD was never woken - a user program with an
     * ordinary null-pointer bug left the shell that started it hung with no
     * console - and the task never reached the zombie list, so the reaper never
     * released its address space, page tables, user stacks or process_t.
     *
     * Nothing else in the image can produce a user-mode page fault, which is
     * why /bin/ktest_crash exists. It is embedded in the test kernel only.
     *
     * Note the failure mode if this regresses: the exec below never returns and
     * the whole run hits the QEMU timeout. That is loud, which is what we want -
     * a hang is the actual symptom, and it cannot be mistaken for a pass.
     * ------------------------------------------------------------------ */
    int free_before = kt_free_kb();

    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/ktest_crash", 0, 0) == 139,
              "[STRICT] [PROC] a child that faults is reaped and reports 139");

    const char *alive = "[PROC] parent survived a child's segfault\n";
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, (int)alive, kt_strlen(alive)) == kt_strlen(alive),
              "[PROC] the parent is still able to cross the boundary afterwards");

    /*
     * And the address space really came back. Ten crashes, then compare: each
     * leaked a page directory, its page tables, 32 user stack pages and the
     * process_t, so a regression shows up as a large, monotonic drop rather
     * than as noise. The tolerance is generous because the block cache and the
     * heap move underneath this too.
     */
    for (int c = 0; c < 10; c++) {
        syscall(SYSCALL_EXEC, (int)"/bin/ktest_crash", 0, 0);
    }

    int free_after = kt_free_kb();
    KT_ASSERT(free_before > 0 && free_after > 0,
              "[MEM] the free-memory figure is readable before and after");
    KT_ASSERT(free_after + 256 >= free_before,
              "[STRICT] [MEM] ten crashed children did not leak their address spaces");

    /* Still alive and still able to cross the boundary after three teardowns. */
    const char *after = "[PROC] parent survived the child teardowns\n";
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, (int)after, kt_strlen(after)) == kt_strlen(after),
              "[PROC] parent still functional after the reaper ran");

    /* ------------------------------------------------------------------
     * A child that signals itself fatally.
     *
     * The other half of the SIG_KILL default action, and the half no
     * kernel-mode module can reach. send_user_signal() reaps a target that is
     * not the running task on the spot, but it cannot reap the task whose
     * syscall it is running inside - that one keeps the pending bit and is
     * terminated by apply_default_signal_action() at the end of
     * syscall_handler(). The kernel-mode suite runs against a synthetic task
     * and never returns through syscall_handler(), so only a genuine Ring 3
     * process walks this path. Hence /bin/ktest_signal, the same arrangement
     * /bin/ktest_crash has for the fault path.
     *
     * 137 is 128 + SIG_KILL, matching the 139 the crash test expects for
     * SIGSEGV. A status of 0 here would mean the payload ran off the end of
     * main() because the default action never fired.
     *
     * If this regresses the exec never returns and the run hits the QEMU
     * timeout - loud, and impossible to mistake for a pass.
     * ------------------------------------------------------------------ */
    KT_ASSERT(syscall(SYSCALL_EXEC, (int)"/bin/ktest_signal", 0, 0) == 137,
              "[STRICT] [SIGNAL] a child that signals itself is reaped and reports 137");

    const char *sig_alive = "[SIGNAL] parent survived a child's self-kill\n";
    KT_ASSERT(syscall(SYSCALL_WRITE, 1, (int)sig_alive, kt_strlen(sig_alive)) == kt_strlen(sig_alive),
              "[SIGNAL] the parent is still able to cross the boundary afterwards");

    /* ------------------------------------------------------------------
     * 11. LOCKDOWN.
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
