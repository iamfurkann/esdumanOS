#ifndef SYSCALL_H
#define SYSCALL_H

/*
 * ==========================================================================
 * THE SYSCALL NUMBERS BELOW ARE FROZEN AS OF v1.0.0.
 * ==========================================================================
 *
 * Frozen means one thing and it is worth being exact about, because the wrong
 * reading of it held this release up for two minor versions: no number already
 * assigned here changes its value, changes its meaning, or is given to a
 * different call. It does not mean the table is closed. New calls may be added,
 * and mount()/umount() are expected to be the next ones.
 *
 * Three rules follow from that, and they are the whole of the contract:
 *
 *   1. New calls continue from the highest assigned number, not from the lowest
 *      free one. v1.0.0 said "from 68"; SYSCALL_SETKEY took 68 in v1.1.0 and
 *      SYSCALL_PCIINFO took 69 in v1.4.0, which is the rule working rather than
 *      an exception to it, and the next call takes 70.
 *
 *   2. The holes are never filled. 11 (CAT_FILE) and 28 (LS_DIR) held calls that
 *      were removed; 30, 31 and 32 were reserved for a crypto API that was never
 *      designed and never will be under that reservation; 99 held YIELD until
 *      v1.0.0 moved it to 67. Every one of them stays empty for good. A number
 *      that once meant something and now means something else is the one failure
 *      a frozen ABI exists to prevent, and a reservation nobody redeemed is not
 *      a promise worth keeping to the point of burning three numbers on it.
 *
 *   3. Numbers >= 200 are reserved for calls that only exist in test builds.
 *      SYSCALL_KTEST_REPORT is the only one today. Production kernels answer
 *      E_NOSYS across the whole band, so nothing there can become load-bearing
 *      by accident.
 *
 * What enforces this is tests/kernel/test_abi.c, not this comment. Every number
 * in this file is asserted there by literal value, and so are the errno codes,
 * the flag constants and the security levels - because a syscall number is
 * written down in four places that no compiler compares: here, as a literal in
 * the sources under apps/bin, as a literal string in
 * tests/host/c/test_elf_sast.c, and in
 * README.md's table. Prose saying "these do not change" would have done nothing
 * about any of them. A test fails.
 *
 * Not frozen, and deliberately: the struct layouts that cross the boundary have
 * their own size assertions in test_regression.c, and the limits documented in
 * README are limits rather than promises. esd_stat_t's timestamps are seconds
 * since the Unix epoch in a uint32_t and run out in 2106; that is a documented
 * limit of this ABI, not a defect waiting to be fixed in it.
 */

/**
 * @brief Encoded length of the syscall trap instruction (int 0x80 is CD 80).
 *
 * The processor pushes the address *after* the trap, so a syscall that has to
 * block must resume this many bytes earlier to run again. This is the only
 * place that assumption lives: syscall_handler() converts it into a recorded
 * entry address once, and every blocking site uses that recorded value instead
 * of subtracting from EIP itself.
 *
 * Named SYSCALL_TRAP_INSN_LEN rather than SYSCALL_INSN_LEN because it is not a
 * syscall number and used to look exactly like one - it sat in this file, in the
 * SYSCALL_* namespace, holding the value 2, which is also SYSCALL_IPC_SEND. Any
 * reader or script counting the numbers in this header by their prefix got 2
 * twice and one entry too many; that is how the count came out as 65 when the
 * table has 62 production calls in it.
 */
#define SYSCALL_TRAP_INSN_LEN 2

/** @brief Terminate current process */
#define SYSCALL_EXIT            1
/**
 * @brief Start a program; ebx is the path, ecx the mode, edx the argument string.
 *
 * A zero ecx is the form this call has always had: the caller blocks until the
 * program finishes and receives its exit status. init uses it, because init has
 * nothing else to do while the shell runs.
 *
 * EXEC_NOWAIT returns the child's pid immediately instead. A shell needs that to
 * own the program it just started the way it owns a pipeline - put it in a group
 * of its own, hand it the terminal, and wait for it with wait(), which can
 * report a child that stopped as well as one that exited. With the blocking form
 * there is no pid to name, so a stopped foreground command could not be brought
 * back.
 *
 * Who may run what is decided by the file's execute bit as of v0.9.4, plus read
 * and search permission on the directory holding it. Before that it was decided
 * by location: anybody could run a program in /bin, and root alone could run one
 * anywhere else - so executability was a property of where a file sat rather
 * than of the file, and the `x` bit stored on every entry since v0.9.0 meant
 * nothing. A user can now run a program they wrote and chmod'ed themselves,
 * which is the Unix arrangement and is deliberate.
 */
#define SYSCALL_EXEC            5

/** @brief exec() mode: block until the program finishes and return its status. */
#define EXEC_WAIT               0
/** @brief exec() mode: return the new process's pid and leave it running. */
#define EXEC_NOWAIT             1
/** @brief Set process scheduling priority */
#define SYSCALL_SET_PRIORITY    7
/**
 * @brief Yield the CPU to another process.
 *
 * 67 as of v1.0.0. It was 99 from the beginning - far outside the run every
 * other call sits in, for no reason anybody recorded, which is the shape of a
 * placeholder that was never revisited. Moving it is an ABI break and v1.0.0 was
 * the last moment one was permitted; leaving it would have frozen the outlier in
 * place for the life of the project, and 99 is now a hole like any other.
 *
 * It had exactly two callers and both are in this tree, which is what made the
 * move affordable: init's idle loop, which names the constant, and the idle task
 * itself - whose Ring 3 loop is assembled byte by byte in init_multitasking().
 * That one used to carry the number as a literal 0x63 in the instruction stream,
 * where no compiler could see it disagree with this line. It is built from this
 * constant now, and that change had to land before this one: a stale byte there
 * does not fail a test, it stops the machine booting.
 */
#define SYSCALL_YIELD           67

/** @brief Read from a file descriptor */
#define SYSCALL_READ            3
/** @brief Write to a file descriptor */
#define SYSCALL_WRITE           4
/** @brief Clear the console screen */
#define SYSCALL_CLEAR_SCREEN    10
/** @brief Set keyboard layout */
#define SYSCALL_SET_LAYOUT      12

// ==========================================================
// VFS (Virtual File System) & Directory Management Syscalls
// ==========================================================
/** @brief Create a new file */
#define SYSCALL_CREATE_FILE     8
/** @brief List files in current directory */
#define SYSCALL_LIST_FILES      9
/*
 * 11 was SYSCALL_CAT_FILE, removed in v0.9.2.
 *
 * It printed a file's contents from the kernel, so anything reading it could not
 * pipe or redirect them - and it had no caller at all: the shell's `cat` opens
 * the file and writes it itself, which is why `cat` worked and this did not.
 * Dead and wrong at once.
 *
 * The number is retired, not free: v1.0.0 froze the rule that a hole is never
 * filled. See the contract at the top of this file.
 */
/** @brief Remove a file */
#define SYSCALL_RM_FILE         22
/** @brief Move or rename a file */
#define SYSCALL_MV_FILE         23

/** @brief Create a new directory */
#define SYSCALL_MKDIR           26  
/*
 * 28 was SYSCALL_LS_DIR, removed in v0.10.0.
 *
 * It printed a directory listing from inside the kernel with terminal_putchar(),
 * so its output never reached the calling process's descriptor 1 - "ls | grep"
 * saw an empty pipe. v0.9.2 moved the shell's `ls` onto READDIR because of that
 * and left the syscall in place "for any caller that genuinely wants a screen
 * dump". There was never such a caller, in /bin or the shell or the tests, and
 * v0.9.3 found that out by looking rather than by reading the comment.
 *
 * The same position CAT_FILE was in when v0.9.2 removed it, and the number gets
 * the same treatment: retired rather than reused. v1.0.0 made that a rule rather
 * than a habit - see the contract at the top of this file.
 */
/** @brief Get directory ID */
#define SYSCALL_GET_DIR_ID      29

/** @brief Dump current task stack */
#define SYSCALL_STACK_DUMP      14
/** @brief Display memory information */
#define SYSCALL_MEMINFO         15
/** @brief Test dynamic memory allocation */
#define SYSCALL_TEST_MALLOC     16
/** @brief Display hex dump of memory */
#define SYSCALL_HEXDUMP         17

/** @brief Send IPC message */
#define SYSCALL_IPC_SEND        2
/** @brief Receive IPC message */
#define SYSCALL_IPC_RECEIVE     6
/**
 * @brief Arm, re-arm or cancel the caller's alarm; ebx is a count of seconds.
 *
 * Returns the seconds left on the previous alarm, or 0 when there was none, so a
 * caller that had to displace one can put it back. Zero cancels and still
 * reports. An interval past what a signed tick difference can name - a little
 * under 249 days at TIMER_HZ - is refused with E_INVAL rather than clamped.
 *
 * SIG_ALRM arrives when the deadline passes and terminates the process by
 * default, which is POSIX's choice: a bound whose default is to be ignored is
 * not a bound. A caller that means to survive its own alarm registers a handler
 * with SIGNAL_REG.
 *
 * This was not an alarm before v1.0.0, and the entry here said it was. It read
 * none of its arguments, sent no signal, and had no relation to the caller - it
 * armed kernel timer slot 1 for a fixed three seconds and let the kernel print a
 * green line, which made it the last call in this table that produced output
 * from Ring 0 on a caller's behalf. The number was one of the four things v1.0.0
 * had to settle before freezing, and it was settled by making the call mean what
 * its name had been claiming.
 */
#define SYSCALL_ALARM           18
/** @brief Register a signal handler */
#define SYSCALL_SIGNAL_REG      24
/**
 * @brief Send a signal; ebx is the pid, ecx the signal.
 *
 * A negative pid names the process group -pid and signals every member, which is
 * how the shell continues a stopped job: a job is a group, and the process the
 * shell forked is rarely the only member of it - that process may itself have
 * started the program the user is actually looking at, and both were stopped.
 */
#define SYSCALL_KILL            25
/** @brief Return from a signal handler */
#define SYSCALL_SIGRETURN       27

/** @brief Enable system lockdown mode */
#define SYSCALL_LOCKDOWN        13
/** @brief Trigger kernel panic (testing) */
#define SYSCALL_PANIC           19
/** @brief Reboot the system */
#define SYSCALL_REBOOT          20
/** @brief Halt the system */
#define SYSCALL_HALT            21

/*
 * 30, 31 and 32 were "reserved for future crypto API" and are retired.
 *
 * The reservation was made before v0.5 and nothing was ever designed to fill it:
 * no interface, no caller, no note saying what the three calls would have been.
 * v0.10.1 removed an AES-256 interface from crypto.h that had been declared and
 * never implemented, which is the same reservation in another form and is the
 * argument against keeping this one.
 *
 * Retired rather than released back for general use, because the freeze has one
 * rule about holes and three numbers are not worth making it two rules. A crypto
 * API, if it is ever built, continues from the highest assigned number like
 * anything else.
 */
/** @brief Set system security level */
#define SYSCALL_SET_SEC_LEVEL   33
/**
 * @brief Read an open file's stored bytes, without decrypting them.
 *
 * read() against the stored form: same descriptor, same offset, same shape -
 * ebx a descriptor, ecx a buffer, edx a size, bytes back in eax.
 *
 * This was SYSCALL_CAT_RAW until v0.9.2, which took a path and printed the whole
 * file as hex from the kernel, where nothing could pipe or redirect it. What the
 * call uniquely offered was bypassing decryption; opening, looping and
 * formatting are a program's work and are done in one now.
 */
#define SYSCALL_READ_RAW        34
/** @brief Set user ID of current process */
#define SYSCALL_SETUID          35
/** @brief Create an IPC pipe */
#define SYSCALL_PIPE            36
/** @brief Duplicate a file descriptor */
#define SYSCALL_DUP2            37
/** @brief Close a file descriptor */
#define SYSCALL_CLOSE           38
/** @brief Display kernel diagnostic messages */
#define SYSCALL_DMESG           39
/** @brief Open a file */
#define SYSCALL_OPEN            40
/** @brief Authenticate user */
#define SYSCALL_AUTH            41
/** @brief Get process arguments */
#define SYSCALL_GET_ARGS        42
/** @brief Get user ID of current process */
#define SYSCALL_GETUID          43
/** @brief Read directory entries into user buffer */
#define SYSCALL_READDIR         44
/** @brief Write every dirty block-cache sector out to disk */
#define SYSCALL_SYNC            45
/** @brief Change the calling process's working directory */
#define SYSCALL_CHDIR           46
/** @brief Write the calling process's working directory into a user buffer */
#define SYSCALL_GETCWD          47
/** @brief Report a path's metadata into a user esd_stat_t */
#define SYSCALL_STAT            48
/** @brief Report an open descriptor's metadata into a user esd_stat_t */
#define SYSCALL_FSTAT           49
/** @brief Reposition the read/write offset of an open file */
#define SYSCALL_LSEEK           50
/** @brief Return the calling process's pid */
#define SYSCALL_GETPID          51
/** @brief Block the calling process for a number of milliseconds */
#define SYSCALL_SLEEP           52
/** @brief Duplicate the calling process; returns 0 in the child, its pid in the parent */
#define SYSCALL_FORK            53
/**
 * @brief Collect a child that has something to report; ebx is an int*, ecx flags.
 *
 * eax comes back as the pid that reported, 0 when children exist but none has
 * anything to say and WNOHANG was asked for, or E_CHILD when there is nothing
 * left to wait for at all. The status is written through ebx, which may be NULL
 * when the caller only wants the pid.
 */
#define SYSCALL_WAIT            54

/** @brief wait() flag: report that nothing is ready rather than blocking. */
#define WNOHANG                 1
/**
 * @brief wait() flag: report a child that stopped, not only one that exited.
 *
 * Without it a stopped child is invisible here, which is the right default: a
 * caller that does not know about job control would otherwise be handed a pid
 * whose process is still very much alive and would treat it as finished.
 */
#define WUNTRACED               2

/**
 * @brief Status bit meaning "this child stopped", OR'd with the signal number.
 *
 * Above the eight bits an exit status occupies - sys_exit() masks its argument
 * to 0-255 and a signalled death is reported as 128 plus the signal - so there
 * is no value a caller could confuse with either.
 */
#define WSTATUS_STOPPED         0x100
/** @brief Fill an esd_time_t with the current time; non-zero ecx asks for UTC */
#define SYSCALL_TIME            55
/**
 * @brief Move the program break; returns the resulting break.
 *
 * Raw brk semantics, as the kernel call rather than the libc wrapper: the
 * requested break goes in ebx and the *resulting* break comes back in eax. A
 * request that cannot be satisfied returns the break unchanged rather than an
 * errno, so the caller finds out by comparing the answer with what it asked
 * for - and `brk(0)`, which can never be granted, is therefore how you read the
 * current break without moving it.
 */
#define SYSCALL_BRK             56
/**
 * @brief Map anonymous, private, zero-filled pages; returns the address.
 *
 * ebx is the length in bytes, rounded up to a page. ecx is reserved and must be
 * zero. Returns 0xFFFFFFFF when the mapping could not be made. The kernel picks
 * the address; there is no MAP_FIXED and no file backing.
 */
#define SYSCALL_MMAP            57
/**
 * @brief Release pages obtained from mmap; returns 0 or a negative errno.
 *
 * ebx is the address and ecx the length. Both are rounded to page boundaries,
 * and the range must lie inside the region mmap hands out.
 */
#define SYSCALL_MUNMAP          58
/**
 * @brief Inspect and control the kernel log; ebx selects the operation.
 *
 * The read side of the log has been reachable since v0.4.x through DMESG. What
 * has never been reachable is everything around it: the severity threshold has
 * sat at INFO since boot with no way to move it, so every DEBUG record the
 * kernel composes has been discarded unseen, and there has been no way to clear
 * the ring or to ask how much of it was lost when it wrapped.
 */
#define SYSCALL_KLOG_CTL        59

/** @brief Discard every held record. Root only. */
#define KLOG_CTL_CLEAR          0
/** @brief Set the severity threshold from ecx. Root only. */
#define KLOG_CTL_SET_LEVEL      1
/** @brief Return the current severity threshold. */
#define KLOG_CTL_GET_LEVEL      2
/** @brief Return how many records are currently held. */
#define KLOG_CTL_HELD           3
/** @brief Return how many records the ring has overwritten since boot. */
#define KLOG_CTL_DROPPED        4

/**
 * @brief Place a process in a process group; ebx is the pid, ecx the group.
 *
 * A pid of 0 means the caller, and a group of 0 means "found a new group named
 * by that pid". A caller may place itself or one of its children and nothing
 * else, which is what stops an unrelated program moving a shell's job out from
 * under it.
 *
 * Both the shell and the child it just forked call this with the same arguments,
 * which is the ordinary Unix answer to a race neither of them can win: whichever
 * runs first, the group is set before the child can be signalled.
 */
#define SYSCALL_SETPGID         60
/**
 * @brief Hand the terminal to a process group; ebx is the group.
 *
 * The foreground group is what Ctrl-C interrupts. A caller may only name its own
 * group or a group containing one of its children - a background job cannot take
 * the terminal away from the shell that started it.
 */
#define SYSCALL_TCSETPGRP       61
/**
 * @brief Read a process's group; ebx is the pid, or 0 for the caller.
 */
#define SYSCALL_GETPGID         62

/**
 * @brief Ask whether a read would block; ebx is the descriptor.
 *
 * Returns 1 when a read would return straight away - because there is data, or
 * because there is an end of file to report - 0 when it would block, and a
 * negative errno for a descriptor that is not open.
 *
 * It exists for one problem with no other honest answer. The keyboard sends the
 * escape sequences a terminal sends, so `ESC` now arrives both as the Escape key
 * and as the first byte of a sequence, and nothing in this system has a timer
 * fine enough to tell them apart by how long the next byte takes. Asking whether
 * a second byte is already waiting does tell them apart, and asking is the only
 * thing a program can do that does not involve consuming a byte it may have to
 * give back.
 */
#define SYSCALL_POLL            63

/**
 * @brief Change a file's permission bits.
 *
 * ebx is a path, ecx is the new mode. Only the owner and root may change one,
 * which is the same rule every Unix applies and for the same reason: a mode a
 * stranger can rewrite is not a permission, it is a suggestion.
 *
 * The sticky bit, 01000, means what it means everywhere as of v0.9.4: set on a
 * directory it restricts removal to the owner of each entry, the owner of the
 * directory, and root. `chmod 1777 /tmp` is the arrangement it exists for. It
 * was always stored - the mask has been 07777 since v0.9.0 - and what changed is
 * that something reads it.
 *
 * The set-user-id and set-group-id bits are still kept and still consulted by
 * nothing. A caller that sets one is asking for something this system has no
 * meaning for, and silently keeping only what exists is kinder than an error
 * nobody can act on.
 */
#define SYSCALL_CHMOD           64

/**
 * @brief Change a file's owner and group.
 *
 * ebx is a path, ecx is the new uid, edx the new gid. **Root only**, and that is
 * stricter than it strictly has to be: handing a file to somebody else is how a
 * user escapes their own quota on systems that have one, and this system does
 * not have one to escape yet. The restriction is easier to keep than to add back.
 */
#define SYSCALL_CHOWN           65

/**
 * @brief Set the wall clock.
 *
 * ebx is an esd_time_t in user memory, holding the time as the caller reads it -
 * that is, with tz_offset_hours saying what the fields are adjusted by. The
 * kernel takes the offset back out and writes UTC, because that is what the chip
 * holds and what every reader of it assumes.
 *
 * Root only. A clock a user can move is a clock that says nothing about when a
 * file was written.
 */
#define SYSCALL_SETTIME         66

/**
 * @brief Change the disk passphrase; ebx is the old one, ecx the new one.
 *
 * The first number handed out after the freeze, and it is 68 rather than any of
 * the free-looking gaps below it - which is the whole of what rule 1 means. 67
 * went to YIELD when v1.0.0 moved it, so this continues from there.
 *
 * Both arguments are user strings. Root only: a passphrase a second user can
 * change is a disk that user owns.
 *
 * It re-wraps the data key rather than re-encrypting the disk. The old
 * passphrase opens the key slot, a fresh salt derives a new key-encryption key,
 * and the same data key goes back under it - so the files on the disk are not
 * touched and cannot be lost by getting this wrong. A wrong old passphrase
 * returns E_ACCES and changes nothing.
 */
#define SYSCALL_SETKEY          68

/**
 * @brief Render the PCI inventory into the caller's buffer.
 *
 * ebx is the buffer, ecx its capacity. Returns the number of bytes written, or a
 * negative errno. Root only, like every other diagnostic that hands back text
 * the kernel rendered - it goes through the same check MEMINFO, HEXDUMP and
 * STACK_DUMP do.
 *
 * The second number handed out after the freeze, continuing from SETKEY at 68
 * rather than filling a hole. mount() and umount(), which the ABI comment above
 * has been promising since v1.0.0, take 70 and 71 when they are written; the
 * README said they would take 68 and 69 and was two releases out of date when it
 * said it.
 *
 * It reports what the bus was asked at boot rather than re-reading configuration
 * space. A user-space call that poked at 0xCF8 on every invocation would be a
 * different and much larger thing to have to defend.
 */
#define SYSCALL_PCIINFO         69

// ==========================================================
// Test-build-only syscalls (>= 200)
//
// Present in every build so user-space test payloads compile against a single
// header, but only serviced when the kernel was linked with the test modules
// AND booted with kernel_pass=selftest. Production kernels answer E_NOSYS.
//
// The whole band from 200 up is reserved for this, frozen alongside the rest -
// so a production call can never be given a number a test payload might already
// be calling, and a test build can add one without consulting the table above.
// ==========================================================
/** @brief Report one Ring 3 self-test result to the kernel test framework */
#define SYSCALL_KTEST_REPORT    200
#endif // SYSCALL_H