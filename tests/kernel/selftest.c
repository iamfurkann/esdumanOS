/*
 * File: selftest.c
 * Purpose: Main runner for the comprehensive kernel test suite.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "tty.h"
#include "stdio.h"
#include "arch.h"
#include "process.h"
#include "libft.h"
#include "fs.h"
#include "elf.h"
#include "errno.h"
#include "registers.h"
#include "uaccess.h"
#include "syscall.h"
#include "rtc.h"
#include "pmm.h"

int tests_passed = 0;
int tests_failed = 0;

/*
 * Ring 3 test payload, built from tests/user/ktest_user.c, encrypted with the
 * build-time ELF key and embedded by the Makefile. Only linked into $(TEST_BIN).
 */
extern unsigned char ktest_user_elf[];
extern unsigned int ktest_user_elf_len;

/*
 * Companion payload that takes a user-mode page fault on purpose. Installed
 * beside ktest_user so the Ring 3 half can exec it and check that a crashing
 * child is reaped and its status delivered. Test builds only.
 */
extern unsigned char ktest_crash_elf[];
extern unsigned int ktest_crash_elf_len;

/*
 * Companion payload that sends itself SIG_KILL. Installed for the same reason as
 * the crash payload: apply_default_signal_action() runs only on the way out of
 * syscall_handler(), so only a real Ring 3 process can reach it. Test builds only.
 */
extern unsigned char ktest_signal_elf[];
extern unsigned int ktest_signal_elf_len;



/**
 * @brief Writes a byte to a specified hardware I/O port.
 *
 * A primitive inline wrapper around the x86 `outb` instruction, utilized here 
 * to interface directly with the QEMU debug exit port.
 *
 * @param port The target I/O port address.
 * @param val The 8-bit value to write to the port.
 * @expected The exact byte value is dispatched to the specified hardware port.
 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

/**
 * @brief Triggers an automated QEMU emulator shutdown with an exit code.
 *
 * Communicates with QEMU's debug exit device (commonly located at port 0xf4) 
 * to forcefully terminate the virtual machine. It maps the test suite's success 
 * state into standard system exit codes for CI/CD pipeline integration.
 *
 * @param is_success Integer flag indicating overall test suite success (1) or failure (0).
 * @expected QEMU gracefully terminates, yielding Exit Code 33 for success or Exit Code 35 for failure.
 */
void qemu_shutdown(int is_success) {
    outb(0xf4, is_success ? 0x10 : 0x11);
}

/*
 * Collected failures, reprinted as one block at the end of the run.
 *
 * A full run emits several hundred lines across both sinks, so a handful of
 * [FAIL] markers scattered through it are easy to miss and tedious to find. The
 * text is copied rather than referenced: results coming from Ring 3 arrive in a
 * stack buffer inside the syscall handler, which is gone by the time the summary
 * prints.
 *
 * Bounded on purpose. Past the cap the count still climbs, and the summary says
 * how many it could not list - better than growing a buffer inside a kernel that
 * is already failing.
 */
#define KTEST_MAX_LOGGED_FAILURES 40
#define KTEST_FAILURE_TEXT_MAX    112

static char ktest_failures[KTEST_MAX_LOGGED_FAILURES][KTEST_FAILURE_TEXT_MAX];
static int ktest_failure_count = 0;

void ktest_record_failure(const char *message, const char *file, int line) {
    if (ktest_failure_count >= KTEST_MAX_LOGGED_FAILURES) {
        ktest_failure_count++;   /* still counted, just not stored */
        return;
    }

    char *slot = ktest_failures[ktest_failure_count++];
    uint32_t pos = 0;

    for (uint32_t i = 0; message[i] != '\0' && pos < KTEST_FAILURE_TEXT_MAX - 1; i++) {
        slot[pos++] = message[i];
    }

    if (file) {
        const char *sep = " (";
        for (uint32_t i = 0; sep[i] != '\0' && pos < KTEST_FAILURE_TEXT_MAX - 1; i++) slot[pos++] = sep[i];
        for (uint32_t i = 0; file[i] != '\0' && pos < KTEST_FAILURE_TEXT_MAX - 1; i++) slot[pos++] = file[i];
        if (pos < KTEST_FAILURE_TEXT_MAX - 1) slot[pos++] = ':';

        char line_str[16];
        ktest_itoa(line, line_str);
        for (uint32_t i = 0; line_str[i] != '\0' && pos < KTEST_FAILURE_TEXT_MAX - 1; i++) slot[pos++] = line_str[i];
        if (pos < KTEST_FAILURE_TEXT_MAX - 1) slot[pos++] = ')';
    } else {
        const char *tag = " (ring3)";
        for (uint32_t i = 0; tag[i] != '\0' && pos < KTEST_FAILURE_TEXT_MAX - 1; i++) slot[pos++] = tag[i];
    }

    slot[pos] = '\0';
}

/**
 * @brief Reprints every recorded failure as one block.
 */
static void ktest_print_failures(void) {
    if (ktest_failure_count == 0) return;

    int listed = ktest_failure_count;
    if (listed > KTEST_MAX_LOGGED_FAILURES) listed = KTEST_MAX_LOGGED_FAILURES;

    printk("\n---------------- FAILURES ----------------\n");

    for (int i = 0; i < listed; i++) {
        char num[16]; ktest_itoa(i + 1, num);
        printk("  %s. %s\n", num, ktest_failures[i]);
    }

    if (ktest_failure_count > KTEST_MAX_LOGGED_FAILURES) {
        char extra[16]; ktest_itoa(ktest_failure_count - KTEST_MAX_LOGGED_FAILURES, extra);
        printk("  ... and %s more not listed\n", extra);
    }
}

/**
 * @brief Prints the aggregated pass/fail tally to both the VGA console and COM1.
 *
 * @expected A single summary block is emitted on both sinks.
 */
static void ktest_print_summary(void) {
    char pass_str[16]; ktest_itoa(tests_passed, pass_str);
    char fail_str[16]; ktest_itoa(tests_failed, fail_str);

    ktest_print_failures();

    printk("\n======================================================\n");
    printk("RESULT: %s PASSED | %s FAILED\n", pass_str, fail_str);
    printk("======================================================\n");

}

/**
 * @brief Ends the suite: prints the tally and hands the verdict to QEMU.
 *
 * Split out of run_all_selftests() because the Ring 3 half of the suite finishes
 * inside a syscall on a different stack, and has to be able to terminate the run
 * from there.
 *
 * @expected QEMU exits with code 33 when nothing failed, 35 otherwise.
 */
void ktest_finish(void) {
    ktest_print_summary();
    qemu_shutdown(tests_failed == 0);
    /* isa-debug-exit has already terminated us; park the CPU if it did not. */
    while (1) { asm volatile("cli; hlt"); }
}

/**
 * @brief SYSCALL_KTEST_REPORT handler: records one result sent up from Ring 3.
 *
 * Deliberately reachable only from test builds. syscall.c resolves this through
 * a weak symbol, so production kernels answer SYSCALL_KTEST_REPORT with -ENOSYS.
 *
 * Register protocol:
 *   ebx = KT_REPORT_FAIL(0) | KT_REPORT_PASS(1) | KT_REPORT_DONE(2)
 *   ecx = pointer to a NUL-terminated description (ignored for DONE)
 *
 * The message itself arrives through copy_string_from_user(), so every reported
 * assertion also exercises the uaccess string path against a real Ring 3 pointer.
 *
 * @param regs Saved register state of the calling user process.
 * @expected Counters advance and the result is echoed to VGA and COM1; DONE ends the run.
 */
void sys_ktest_report(arch_regs_t *regs) {
    int kind = (int)regs->ebx;

    if (kind == KT_REPORT_DONE) {
        /*
         * Last chance to check the syscall restart bookkeeping, and the only
         * place it means anything: we are inside a real Ring 3 syscall, on the
         * live interrupt frame, with the entry address recorded by the
         * dispatcher a few instructions ago.
         */
        KTEST_ASSERT(current_task != 0 && current_task->in_syscall,
                     "[SYSCALL] in_syscall is set while a syscall is running");
        KTEST_ASSERT(trap_frame_is_live(regs),
                     "[SYSCALL] the dispatcher was handed the live interrupt frame");
        KTEST_ASSERT(current_task != 0 &&
                     current_task->syscall_entry_eip == regs->eip - SYSCALL_INSN_LEN,
                     "[STRICT] [SYSCALL] recorded entry EIP points at the trap instruction");

        regs->eax = 0;
        ktest_finish();
        return;
    }

    if (kind == KT_REPORT_TICKS) {
        /* Not an assertion: hands the payload the tick counter so it can time
         * sleep() for itself rather than assume the call blocked. */
        regs->eax = (int)timer_get_ticks();
        return;
    }

    if (kind == KT_REPORT_FREEMEM) {
        /* Reported in KB so the value stays well inside a positive int. */
        regs->eax = (int)(pmm_get_free_memory() / 1024);
        return;
    }

    char msg[128];
    int res = copy_string_from_user(msg, (const char *)regs->ecx, sizeof(msg));

    if (res != E_OK && res != E_NAMETOOLONG) {
        /* A payload that cannot hand us a readable string is itself a failure. */
        ft_strlcpy(msg, "(unreadable message pointer from Ring 3)", sizeof(msg));
        kind = KT_REPORT_FAIL;
    }

    if (kind == KT_REPORT_FAIL) {
        printk("  [FAIL] %s\n", msg);
        ktest_record_failure(msg, 0, 0);
        tests_failed++;
    } else {
        printk("  [PASS] %s\n", msg);
        tests_passed++;
    }

    regs->eax = 0;
}

/**
 * @brief Installs the Ring 3 payload in /bin and transfers control to it.
 *
 * Everything before this point runs at CPL=0 against a synthetic task, so the
 * privilege boundary itself is never crossed. This hands the rest of the suite
 * to a real user process; start_first_task() iret's into Ring 3 and does not
 * return, so the payload's SYSCALL_KTEST_REPORT/DONE is what ends the run.
 *
 * @expected Control reaches Ring 3; anything else is reported as a failure and
 *           the suite terminates rather than hanging.
 */
static void run_user_mode_tests(void) {
    printk("\n--- Ring 3 (User Mode) Boundary Tests ---\n");

    int bin_idx = fs_get_entry_idx("bin", 0);
    KTEST_ASSERT(bin_idx != -1, "[RING3] /bin is present to install the payload into");
    if (bin_idx == -1) return;

    uint8_t bin_id = dir_table[bin_idx].entry_id;

    /* Drop any copy left by an earlier boot on a persistent disk image. */
    fs_delete("ktest_user", bin_id);

    int wres = fs_create_file_raw("ktest_user", ktest_user_elf, ktest_user_elf_len, bin_id);
    KTEST_ASSERT(wres == E_OK, "[RING3] user-mode payload written to /bin/ktest_user");
    if (wres != E_OK) return;

    /* The deliberate-crash companion, for the teardown assertions. */
    fs_delete("ktest_crash", bin_id);
    KTEST_ASSERT(fs_create_file_raw("ktest_crash", ktest_crash_elf, ktest_crash_elf_len, bin_id) == E_OK,
                 "[RING3] crash payload written to /bin/ktest_crash");

    /* The deliberate-self-kill companion, for the signal default action. */
    fs_delete("ktest_signal", bin_id);
    KTEST_ASSERT(fs_create_file_raw("ktest_signal", ktest_signal_elf, ktest_signal_elf_len, bin_id) == E_OK,
                 "[RING3] self-kill payload written to /bin/ktest_signal");

    asm volatile("sti");

    int pid = load_and_exec_elf("ktest_user", bin_id);
    KTEST_ASSERT(pid > 0, "[RING3] user-mode payload loaded into its own address space");
    if (pid <= 0) return;

    foreground_task = pid;
    start_first_task();

    /* start_first_task() iret's into Ring 3 and never comes back. */
    KTEST_ASSERT(0, "[RING3] control transferred to the user-mode payload");
}

/**
 * @brief The master execution routine for the kernel test suite.
 *
 * Orchestrates the sequential initialization and execution of all modular kernel tests, 
 * ranging from low-level memory handling up to complex integration testing. After 
 * traversing all modules, it aggregates the results and halts the emulator.
 *
 * @expected The terminal is initialized, all registered sub-tests are dispatched 
 *           consecutively, and a final statistical summary of passes and failures 
 *           is reported before cleanly halting execution.
 */
void run_all_selftests(void) {
    terminal_initialize();
    
    // Map 4 consecutive simulated user-space pages for integration tests (0x500000 - 0x503000)
    extern uint32_t pmm_alloc_frame(void);
    extern int map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
    /*
     * init_multitasking() has already built the real task list (the idle task).
     * The kernel-mode modules below run against a synthetic task instead, so
     * stash the real one and put it back before the Ring 3 half starts.
     */
    process_t *saved_current = current_task;
    process_t *saved_head    = task_list_head;

    /* CR4.SMAP decides whether the kernel-mode half can run at all; see below. */
    uint32_t cr4_now;
    asm volatile("mov %%cr4, %0" : "=r"(cr4_now));
    int smap_active = (cr4_now >> 21) & 1;

    // Create a mock task structure so syscalls have valid fd_table and uid
    static process_t dummy_task;
    static file_descriptor_t dummy_fds[16];
    ft_memset(&dummy_task, 0, sizeof(process_t));
    ft_memset(dummy_fds, 0, sizeof(dummy_fds));
    dummy_task.fd_table_size = 16;
    dummy_task.fd_table = dummy_fds;
    dummy_task.uid = 0;
    dummy_task.pid = 999;
    dummy_task.state = 1; // TASK_RUNNING
    dummy_task.next = 0;
    dummy_task.prev = 0;
    
    current_task = &dummy_task;
    task_list_head = &dummy_task;
    task_list_tail = &dummy_task;

    /*
     * Zero the scratch pages while they are still kernel-only, then hand them
     * to the modules as user memory. Mapping them user-accessible first and
     * memsetting them afterwards is a supervisor write to a user page, which
     * faults the moment SMAP is active.
     */
    for (int i = 0; i < 4; i++) {
        uint32_t vaddr = 0x500000 + (i * 4096);
        uint32_t sim_phys = pmm_alloc_frame();
        map_page(vaddr, sim_phys, 3); // Present | Read/Write, kernel only
        ft_memset((void *)vaddr, 0, 4096);
        map_page(vaddr, sim_phys, 7); // ...then Present | Read/Write | User
    }

    printk("\n======================================================\n");
    printk("       KFS COMPREHENSIVE KERNEL TEST SUITE            \n");
    printk("======================================================\n");

    if (smap_active) {
        /*
         * The kernel-mode modules stand in for user space: they keep their test
         * buffers in the user-accessible scratch window above and then read and
         * write them directly from Ring 0. That is exactly the access pattern
         * SMAP exists to stop, so with SMAP on they cannot run as written -
         * roughly twenty sites across test_vfs, test_passwd, test_security,
         * test_pipe, test_stress and test_paging.
         *
         * They are skipped rather than worked around. Bracketing them with
         * EFLAGS.AC would disable SMAP for the whole kernel-mode half, which
         * would defeat the point of running under it, and would not survive the
         * syscalls they make anyway: uaccess_end() clears AC unconditionally.
         *
         * The Ring 3 payload below covers the same boundary from the correct
         * side, and is the part worth running under SMAP.
         */
        printk("\n[KTEST] SMAP active: kernel-mode modules skipped.\n");
        printk("        They simulate user space from Ring 0, which SMAP forbids.\n");
        printk("        The Ring 3 payload exercises the boundary properly.\n");
    } else {
        run_string_tests();
        run_memory_tests();
        run_pipe_tests();
        run_vfs_tests();
        run_devfs_tests();
        run_passwd_tests();
        run_security_tests();
        run_stress_tests();
        run_adversarial_tests();
        run_integration_tests();
        run_regression_tests();
        run_concurrency_tests();
        run_paging_tests();
        run_pmm_tests();
        run_lifecycle_tests();
        run_fault_tests();
        run_syscall_tests();
        run_process_tests();
        run_signal_tests();
        run_reap_tests();
        run_elf_tests();
        run_crypto_tests();
        run_entropy_tests();
        run_bcache_tests();

        printk("\n*** KERNEL-MODE SELF-TESTS FINISHED ***\n");
    }

    /*
     * Put the real task list back. init_multitasking() left exactly one task in
     * it, so rebuild it as that singleton: the stress module appends processes
     * to whatever list head is current, and those entries must not survive into
     * the Ring 3 run.
     */
    current_task   = saved_current;
    task_list_head = saved_head;
    task_list_tail = saved_head;
    if (saved_head) {
        saved_head->prev = 0;
        saved_head->next = 0;
    }

    run_user_mode_tests();

    /* Only reached when the Ring 3 handoff failed; report what we have. */
    ktest_finish();
}
