/*
 * File: test_abi.c
 * Purpose: The frozen v1.0.0 ABI, asserted rather than promised.
 *
 * This file is part of the esdumanOS test suite.
 *
 * Every constant a Ring 3 program is compiled against is checked here against
 * the literal value it froze at. That is the whole design, and the reason for it
 * is that a syscall number is written down in four places no compiler compares:
 *
 *   - include/syscall.h, which is the authority;
 *   - the sources under apps/bin, as literals, since they are freestanding;
 *   - tests/host/c/test_elf_sast.c, as literal strings that grep for those;
 *   - README.md's System Call Reference, as a markdown table.
 *
 * Only the kernel's own dispatcher refers to them by name, so only the kernel
 * would notice a change. Nineteen user programs and a static analyser would go
 * on holding numbers that had quietly come to mean something else - and a
 * comment at the top of syscall.h saying "these are frozen" would have caught
 * exactly none of it. This module is what makes the freeze a thing that fails.
 *
 * A test that reads the value it is checking would be worthless, so nothing here
 * derives anything: the literals are typed out. When one of these fails, the
 * question is not whether the test is out of date. The number moved, and every
 * program built before it moved is now calling something else.
 */
#include "ktest.h"
#include "syscall.h"
#include "registers.h"
#include "isr.h"
#include "process.h"
#include "signal.h"
#include "security.h"
#include "stat.h"
#include "errno.h"
#include "libft.h"

/**
 * @brief Asserts that a retired syscall number is answered with E_NOSYS.
 *
 * The dispatcher is driven directly with a zeroed frame. cs = 0 makes
 * syscall_handler() read the trap as coming from Ring 0, which keeps it from
 * recording a syscall entry address against the synthetic task this module runs
 * as - the frame is not a real one and nothing should ever resume on it.
 *
 * @param number The retired number.
 * @return The dispatcher's answer.
 */
static int dispatch_retired(uint32_t number) {
    arch_regs_t regs;

    ft_bzero(&regs, sizeof(regs));
    regs.eax = number;

    syscall_handler(&regs);

    return (int)regs.eax;
}

/**
 * @brief The frozen system call numbers.
 *
 * Sixty-four production calls, one assertion each, so a failure names the
 * number that moved instead of reporting that something in the table did. It was
 * sixty-two at the freeze; SETKEY and PCIINFO are the two added since, and
 * adding numbers is what the freeze permits.
 */
static void run_syscall_number_assertions(void) {
    /* Process and scheduling. */
    KTEST_ASSERT(SYSCALL_EXIT == 1,          "[STRICT] [ABI] EXIT is 1");
    KTEST_ASSERT(SYSCALL_EXEC == 5,          "[STRICT] [ABI] EXEC is 5");
    KTEST_ASSERT(SYSCALL_SET_PRIORITY == 7,  "[STRICT] [ABI] SET_PRIORITY is 7");
    KTEST_ASSERT(SYSCALL_GETPID == 51,       "[STRICT] [ABI] GETPID is 51");
    KTEST_ASSERT(SYSCALL_SLEEP == 52,        "[STRICT] [ABI] SLEEP is 52");
    KTEST_ASSERT(SYSCALL_FORK == 53,         "[STRICT] [ABI] FORK is 53");
    KTEST_ASSERT(SYSCALL_WAIT == 54,         "[STRICT] [ABI] WAIT is 54");
    KTEST_ASSERT(SYSCALL_SETPGID == 60,      "[STRICT] [ABI] SETPGID is 60");
    KTEST_ASSERT(SYSCALL_TCSETPGRP == 61,    "[STRICT] [ABI] TCSETPGRP is 61");
    KTEST_ASSERT(SYSCALL_GETPGID == 62,      "[STRICT] [ABI] GETPGID is 62");

    /*
     * YIELD is the one number v1.0.0 moved, and the only one it was ever going
     * to be allowed to move. It sat at 99 - outside the run every other call
     * lives in - from the first release until the freeze.
     */
    KTEST_ASSERT(SYSCALL_YIELD == 67,        "[STRICT] [ABI] YIELD is 67, moved from 99 by the freeze");

    /* Time. */
    KTEST_ASSERT(SYSCALL_TIME == 55,         "[STRICT] [ABI] TIME is 55");
    KTEST_ASSERT(SYSCALL_SETTIME == 66,      "[STRICT] [ABI] SETTIME is 66");

    /*
     * The first number handed out after the freeze, and the assertion that the
     * rule was followed: 68 continues from YIELD at 67 rather than filling the
     * gap at 11, 28, 30, 31 or 32.
     */
    KTEST_ASSERT(SYSCALL_SETKEY == 68,       "[STRICT] [ABI] SETKEY is 68, the first number assigned after the freeze");

    /*
     * And the second, three releases later, which is what makes 68 a rule rather
     * than a one-off: 69 continues from 68 instead of filling a hole.
     */
    KTEST_ASSERT(SYSCALL_PCIINFO == 69,      "[STRICT] [ABI] PCIINFO is 69, continuing from SETKEY");

    /*
     * And the third. Three numbers assigned since the freeze, each one higher
     * than the last and none of them in a hole; at this point the rule has more
     * evidence behind it than the paragraph in syscall.h that states it.
     */
    KTEST_ASSERT(SYSCALL_USBINFO == 70,      "[STRICT] [ABI] USBINFO is 70, continuing from PCIINFO");

    /*
     * And the two this table has been naming as "the next ones" since v1.0.0.
     * Eleven releases is a long time for a promise about numbers nobody had
     * spent, and the sentence that made it moved three times while they waited -
     * which is the whole reason the frontier below is asserted rather than
     * trusted.
     */
    KTEST_ASSERT(SYSCALL_MOUNT == 71,        "[STRICT] [ABI] MOUNT is 71, where it was promised in v1.0.0");
    KTEST_ASSERT(SYSCALL_UMOUNT == 72,       "[STRICT] [ABI] and UMOUNT is 72, beside it");

    /* Descriptors and I/O. */
    KTEST_ASSERT(SYSCALL_READ == 3,          "[STRICT] [ABI] READ is 3");
    KTEST_ASSERT(SYSCALL_WRITE == 4,         "[STRICT] [ABI] WRITE is 4");
    KTEST_ASSERT(SYSCALL_CLEAR_SCREEN == 10, "[STRICT] [ABI] CLEAR_SCREEN is 10");
    KTEST_ASSERT(SYSCALL_SET_LAYOUT == 12,   "[STRICT] [ABI] SET_LAYOUT is 12");
    KTEST_ASSERT(SYSCALL_PIPE == 36,         "[STRICT] [ABI] PIPE is 36");
    KTEST_ASSERT(SYSCALL_DUP2 == 37,         "[STRICT] [ABI] DUP2 is 37");
    KTEST_ASSERT(SYSCALL_CLOSE == 38,        "[STRICT] [ABI] CLOSE is 38");
    KTEST_ASSERT(SYSCALL_OPEN == 40,         "[STRICT] [ABI] OPEN is 40");
    KTEST_ASSERT(SYSCALL_LSEEK == 50,        "[STRICT] [ABI] LSEEK is 50");
    KTEST_ASSERT(SYSCALL_POLL == 63,         "[STRICT] [ABI] POLL is 63");

    /* File system. */
    KTEST_ASSERT(SYSCALL_CREATE_FILE == 8,   "[STRICT] [ABI] CREATE_FILE is 8");
    KTEST_ASSERT(SYSCALL_LIST_FILES == 9,    "[STRICT] [ABI] LIST_FILES is 9");
    KTEST_ASSERT(SYSCALL_RM_FILE == 22,      "[STRICT] [ABI] RM_FILE is 22");
    KTEST_ASSERT(SYSCALL_MV_FILE == 23,      "[STRICT] [ABI] MV_FILE is 23");
    KTEST_ASSERT(SYSCALL_MKDIR == 26,        "[STRICT] [ABI] MKDIR is 26");
    KTEST_ASSERT(SYSCALL_GET_DIR_ID == 29,   "[STRICT] [ABI] GET_DIR_ID is 29");
    KTEST_ASSERT(SYSCALL_READ_RAW == 34,     "[STRICT] [ABI] READ_RAW is 34");
    KTEST_ASSERT(SYSCALL_READDIR == 44,      "[STRICT] [ABI] READDIR is 44");
    KTEST_ASSERT(SYSCALL_SYNC == 45,         "[STRICT] [ABI] SYNC is 45");
    KTEST_ASSERT(SYSCALL_CHDIR == 46,        "[STRICT] [ABI] CHDIR is 46");
    KTEST_ASSERT(SYSCALL_GETCWD == 47,       "[STRICT] [ABI] GETCWD is 47");
    KTEST_ASSERT(SYSCALL_STAT == 48,         "[STRICT] [ABI] STAT is 48");
    KTEST_ASSERT(SYSCALL_FSTAT == 49,        "[STRICT] [ABI] FSTAT is 49");
    KTEST_ASSERT(SYSCALL_CHMOD == 64,        "[STRICT] [ABI] CHMOD is 64");
    KTEST_ASSERT(SYSCALL_CHOWN == 65,        "[STRICT] [ABI] CHOWN is 65");

    /* IPC and signals. */
    KTEST_ASSERT(SYSCALL_IPC_SEND == 2,      "[STRICT] [ABI] IPC_SEND is 2");
    KTEST_ASSERT(SYSCALL_IPC_RECEIVE == 6,   "[STRICT] [ABI] IPC_RECEIVE is 6");
    KTEST_ASSERT(SYSCALL_ALARM == 18,        "[STRICT] [ABI] ALARM is 18");
    KTEST_ASSERT(SYSCALL_SIGNAL_REG == 24,   "[STRICT] [ABI] SIGNAL_REG is 24");
    KTEST_ASSERT(SYSCALL_KILL == 25,         "[STRICT] [ABI] KILL is 25");
    KTEST_ASSERT(SYSCALL_SIGRETURN == 27,    "[STRICT] [ABI] SIGRETURN is 27");

    /* Memory. */
    KTEST_ASSERT(SYSCALL_MEMINFO == 15,      "[STRICT] [ABI] MEMINFO is 15");
    KTEST_ASSERT(SYSCALL_TEST_MALLOC == 16,  "[STRICT] [ABI] TEST_MALLOC is 16");
    KTEST_ASSERT(SYSCALL_HEXDUMP == 17,      "[STRICT] [ABI] HEXDUMP is 17");
    KTEST_ASSERT(SYSCALL_BRK == 56,          "[STRICT] [ABI] BRK is 56");
    KTEST_ASSERT(SYSCALL_MMAP == 57,         "[STRICT] [ABI] MMAP is 57");
    KTEST_ASSERT(SYSCALL_MUNMAP == 58,       "[STRICT] [ABI] MUNMAP is 58");

    /* Security, identity and the machine. */
    KTEST_ASSERT(SYSCALL_LOCKDOWN == 13,     "[STRICT] [ABI] LOCKDOWN is 13");
    KTEST_ASSERT(SYSCALL_STACK_DUMP == 14,   "[STRICT] [ABI] STACK_DUMP is 14");
    KTEST_ASSERT(SYSCALL_PANIC == 19,        "[STRICT] [ABI] PANIC is 19");
    KTEST_ASSERT(SYSCALL_REBOOT == 20,       "[STRICT] [ABI] REBOOT is 20");
    KTEST_ASSERT(SYSCALL_HALT == 21,         "[STRICT] [ABI] HALT is 21");
    KTEST_ASSERT(SYSCALL_SET_SEC_LEVEL == 33, "[STRICT] [ABI] SET_SEC_LEVEL is 33");
    KTEST_ASSERT(SYSCALL_SETUID == 35,       "[STRICT] [ABI] SETUID is 35");
    KTEST_ASSERT(SYSCALL_DMESG == 39,        "[STRICT] [ABI] DMESG is 39");
    KTEST_ASSERT(SYSCALL_AUTH == 41,         "[STRICT] [ABI] AUTH is 41");
    KTEST_ASSERT(SYSCALL_GET_ARGS == 42,     "[STRICT] [ABI] GET_ARGS is 42");
    KTEST_ASSERT(SYSCALL_GETUID == 43,       "[STRICT] [ABI] GETUID is 43");
    KTEST_ASSERT(SYSCALL_KLOG_CTL == 59,     "[STRICT] [ABI] KLOG_CTL is 59");

    /*
     * The test-only band. Frozen too, so that a production call can never be
     * given a number a test payload is already compiled against.
     */
    KTEST_ASSERT(SYSCALL_KTEST_REPORT == 200,
                 "[STRICT] [ABI] KTEST_REPORT is 200, inside the reserved test band");

    /*
     * Not a syscall number, and this is the assertion that says so. It held the
     * value 2 under the name SYSCALL_INSN_LEN until v1.0.0, in the SYSCALL_*
     * namespace, indistinguishable by prefix from SYSCALL_IPC_SEND - which is
     * also 2. Anything counting the numbers in that header by their prefix
     * counted this one and got one call too many.
     */
    KTEST_ASSERT(SYSCALL_TRAP_INSN_LEN == 2,
                 "[STRICT] [ABI] the int 0x80 trap is still two bytes long");
}

/**
 * @brief The retired numbers, which the dispatcher must refuse.
 *
 * A hole is only retired if something enforces that it is empty. These are the
 * numbers a future release would be tempted to reuse, and this is what makes
 * reusing one fail before it ships rather than after a user program built
 * against the old meaning calls the new one.
 */
static void run_retired_number_assertions(void) {
    KTEST_ASSERT(dispatch_retired(11) == E_NOSYS,
                 "[STRICT] [ABI] 11 stays retired - it was CAT_FILE, removed in v0.9.2");
    KTEST_ASSERT(dispatch_retired(28) == E_NOSYS,
                 "[STRICT] [ABI] 28 stays retired - it was LS_DIR, removed in v0.10.0");
    KTEST_ASSERT(dispatch_retired(30) == E_NOSYS,
                 "[STRICT] [ABI] 30 stays retired - reserved for a crypto API never designed");
    KTEST_ASSERT(dispatch_retired(31) == E_NOSYS,
                 "[STRICT] [ABI] 31 stays retired - same reservation");
    KTEST_ASSERT(dispatch_retired(32) == E_NOSYS,
                 "[STRICT] [ABI] 32 stays retired - same reservation");
    KTEST_ASSERT(dispatch_retired(99) == E_NOSYS,
                 "[STRICT] [ABI] 99 stays retired - YIELD left it for 67 at the freeze");

    /*
     * 73 is where the next call goes, and it must be free until one is written.
     * This is the assertion that catches a number being taken quietly. It said
     * 68 until v1.1.0 assigned that one, 69 until v1.4.0, 70 until v1.8.0, and
     * 71 until v1.11.0 spent both 71 and 72 at once - which is what this line is
     * for: the frontier moves by somebody editing it deliberately, not by
     * drifting. Every time, it moved by failing first.
     */
    KTEST_ASSERT(dispatch_retired(73) == E_NOSYS,
                 "[STRICT] [ABI] 73 is unassigned, and is where the next call continues from");
}

/**
 * @brief The error codes, which cross the boundary as return values.
 *
 * A syscall reports failure by returning a negative code in eax and there is no
 * global errno, so these values are as much a part of the interface as the call
 * numbers - a program comparing against -2 for "no such file" is compiled
 * against this table whether it includes the header or not.
 */
static void run_errno_assertions(void) {
    KTEST_ASSERT(E_OK == 0,             "[STRICT] [ABI] E_OK is 0");
    KTEST_ASSERT(E_PERM == -1,          "[STRICT] [ABI] E_PERM is -1");
    KTEST_ASSERT(E_NOENT == -2,         "[STRICT] [ABI] E_NOENT is -2");
    KTEST_ASSERT(E_SRCH == -3,          "[STRICT] [ABI] E_SRCH is -3");
    KTEST_ASSERT(E_INTR == -4,          "[STRICT] [ABI] E_INTR is -4");
    KTEST_ASSERT(E_IO == -5,            "[STRICT] [ABI] E_IO is -5");
    KTEST_ASSERT(E_NXIO == -6,          "[STRICT] [ABI] E_NXIO is -6");
    KTEST_ASSERT(E_2BIG == -7,          "[STRICT] [ABI] E_2BIG is -7");
    KTEST_ASSERT(E_NOEXEC == -8,        "[STRICT] [ABI] E_NOEXEC is -8");
    KTEST_ASSERT(E_BADF == -9,          "[STRICT] [ABI] E_BADF is -9");
    KTEST_ASSERT(E_CHILD == -10,        "[STRICT] [ABI] E_CHILD is -10");
    KTEST_ASSERT(E_AGAIN == -11,        "[STRICT] [ABI] E_AGAIN is -11");
    KTEST_ASSERT(E_NOMEM == -12,        "[STRICT] [ABI] E_NOMEM is -12");
    KTEST_ASSERT(E_ACCES == -13,        "[STRICT] [ABI] E_ACCES is -13");
    KTEST_ASSERT(E_FAULT == -14,        "[STRICT] [ABI] E_FAULT is -14");
    KTEST_ASSERT(E_BUSY == -16,         "[STRICT] [ABI] E_BUSY is -16");
    KTEST_ASSERT(E_EXIST == -17,        "[STRICT] [ABI] E_EXIST is -17");
    KTEST_ASSERT(E_XDEV == -18,         "[STRICT] [ABI] E_XDEV is -18");
    KTEST_ASSERT(E_NODEV == -19,        "[STRICT] [ABI] E_NODEV is -19");
    KTEST_ASSERT(E_NOTDIR == -20,       "[STRICT] [ABI] E_NOTDIR is -20");
    KTEST_ASSERT(E_ISDIR == -21,        "[STRICT] [ABI] E_ISDIR is -21");
    KTEST_ASSERT(E_INVAL == -22,        "[STRICT] [ABI] E_INVAL is -22");
    KTEST_ASSERT(E_NFILE == -23,        "[STRICT] [ABI] E_NFILE is -23");
    KTEST_ASSERT(E_MFILE == -24,        "[STRICT] [ABI] E_MFILE is -24");
    KTEST_ASSERT(E_NOTTY == -25,        "[STRICT] [ABI] E_NOTTY is -25");
    KTEST_ASSERT(E_FBIG == -27,         "[STRICT] [ABI] E_FBIG is -27");
    KTEST_ASSERT(E_NOSPC == -28,        "[STRICT] [ABI] E_NOSPC is -28");
    KTEST_ASSERT(E_SPIPE == -29,        "[STRICT] [ABI] E_SPIPE is -29");
    KTEST_ASSERT(E_ROFS == -30,         "[STRICT] [ABI] E_ROFS is -30");
    KTEST_ASSERT(E_MLINK == -31,        "[STRICT] [ABI] E_MLINK is -31");
    KTEST_ASSERT(E_PIPE == -32,         "[STRICT] [ABI] E_PIPE is -32");
    KTEST_ASSERT(E_NAMETOOLONG == -36,  "[STRICT] [ABI] E_NAMETOOLONG is -36");
    KTEST_ASSERT(E_NOSYS == -38,        "[STRICT] [ABI] E_NOSYS is -38");
    KTEST_ASSERT(E_NOTEMPTY == -39,     "[STRICT] [ABI] E_NOTEMPTY is -39");
}

/**
 * @brief The flags and enumerations a caller passes in or reads back.
 *
 * These travel in the argument registers and in memory the kernel writes, so a
 * value that shifted would be as breaking as a call number that did - and
 * quieter, because the call would still succeed and mean something else.
 */
static void run_constant_assertions(void) {
    /* exec() modes. */
    KTEST_ASSERT(EXEC_WAIT == 0,        "[STRICT] [ABI] EXEC_WAIT is 0");
    KTEST_ASSERT(EXEC_NOWAIT == 1,      "[STRICT] [ABI] EXEC_NOWAIT is 1");

    /*
     * wait() flags and the stopped-status bit. WSTATUS_STOPPED sits above the
     * eight bits an exit status occupies, which is what keeps it from colliding
     * with a status of its own - including the 128 + signal a signalled death is
     * reported with.
     */
    KTEST_ASSERT(WNOHANG == 1,          "[STRICT] [ABI] WNOHANG is 1");
    KTEST_ASSERT(WUNTRACED == 2,        "[STRICT] [ABI] WUNTRACED is 2");
    KTEST_ASSERT(WSTATUS_STOPPED == 0x100,
                 "[STRICT] [ABI] WSTATUS_STOPPED is 0x100, clear of any exit status");

    /* lseek() origins. */
    KTEST_ASSERT(SEEK_SET == 0,         "[STRICT] [ABI] SEEK_SET is 0");
    KTEST_ASSERT(SEEK_CUR == 1,         "[STRICT] [ABI] SEEK_CUR is 1");
    KTEST_ASSERT(SEEK_END == 2,         "[STRICT] [ABI] SEEK_END is 2");

    /* klog_ctl() operations. */
    KTEST_ASSERT(KLOG_CTL_CLEAR == 0,     "[STRICT] [ABI] KLOG_CTL_CLEAR is 0");
    KTEST_ASSERT(KLOG_CTL_SET_LEVEL == 1, "[STRICT] [ABI] KLOG_CTL_SET_LEVEL is 1");
    KTEST_ASSERT(KLOG_CTL_GET_LEVEL == 2, "[STRICT] [ABI] KLOG_CTL_GET_LEVEL is 2");
    KTEST_ASSERT(KLOG_CTL_HELD == 3,      "[STRICT] [ABI] KLOG_CTL_HELD is 3");
    KTEST_ASSERT(KLOG_CTL_DROPPED == 4,   "[STRICT] [ABI] KLOG_CTL_DROPPED is 4");

    /*
     * Signal numbers. The shell writes these out as literals - it is
     * freestanding and cannot include signal.h - so they are an interface
     * between two programs rather than a kernel-private enumeration.
     */
    KTEST_ASSERT(SIG_INT == 2,          "[STRICT] [ABI] SIG_INT is 2");
    KTEST_ASSERT(SIG_KILL == 9,         "[STRICT] [ABI] SIG_KILL is 9");
    KTEST_ASSERT(SIG_PIPE == 13,        "[STRICT] [ABI] SIG_PIPE is 13");
    KTEST_ASSERT(SIG_ALRM == 14,        "[STRICT] [ABI] SIG_ALRM is 14, as Linux numbers it on i386");
    KTEST_ASSERT(SIG_TERM == 15,        "[STRICT] [ABI] SIG_TERM is 15");
    KTEST_ASSERT(SIG_CONT == 18,        "[STRICT] [ABI] SIG_CONT is 18");
    KTEST_ASSERT(SIG_TSTP == 20,        "[STRICT] [ABI] SIG_TSTP is 20");
    KTEST_ASSERT(SIG_TTIN == 21,        "[STRICT] [ABI] SIG_TTIN is 21");
    KTEST_ASSERT(SIG_IGN == 1u,         "[STRICT] [ABI] SIG_IGN is 1, which is not a reachable handler address");

    /*
     * Security levels, which SET_SEC_LEVEL takes by value.
     *
     * Ordered, and the order is the interface: eight IMMUTABLE guards ask
     * ">= SEC_LEVEL_IMMUTABLE", so a level appended to this enum below the last
     * one would change what every one of them permits. v0.10.1 converted seven
     * of those guards from == to >= for exactly this reason.
     */
    KTEST_ASSERT(SEC_LEVEL_NORMAL == 0,          "[STRICT] [ABI] SEC_LEVEL_NORMAL is 0");
    KTEST_ASSERT(SEC_LEVEL_CRYPTO_ENFORCED == 1, "[STRICT] [ABI] SEC_LEVEL_CRYPTO_ENFORCED is 1");
    KTEST_ASSERT(SEC_LEVEL_LOCKDOWN == 2,        "[STRICT] [ABI] SEC_LEVEL_LOCKDOWN is 2");
    KTEST_ASSERT(SEC_LEVEL_IMMUTABLE == 3,       "[STRICT] [ABI] SEC_LEVEL_IMMUTABLE is 3, and is still the strictest");
}

/**
 * @brief Tests the frozen v1.0.0 ABI.
 *
 * Expected Behavior:
 * - Every production syscall number holds the value it froze at.
 * - Every retired number is answered with E_NOSYS, and 68 is still free.
 * - Every errno, flag, signal number and security level holds its frozen value.
 *
 * Edge Cases Covered:
 * - SYSCALL_TRAP_INSN_LEN, which is in the namespace but is not a call number.
 * - The reserved test-only band at 200.
 */
void run_abi_tests(void) {
    printk("\n--- Frozen ABI Tests (v1.0.0) ---\n");

    run_syscall_number_assertions();
    run_retired_number_assertions();
    run_errno_assertions();
    run_constant_assertions();
}
