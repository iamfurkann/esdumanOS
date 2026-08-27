/*
 * File: test_signal.c
 * Purpose: Signal handling unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"
#include "signal.h"
#include "libft.h"
#include "rtc.h"
#include "errno.h"

/*
 * syscalls_internal.h is not on the tests' include path - they build with
 * -Iinclude and it lives in kernel/syscall/ - so the handler is declared here,
 * the same way test_devfs.c and test_security.c reach theirs.
 */
extern void sys_alarm(arch_regs_t *regs);

/*
 * Kernel timer slot used by the test below.
 *
 * Slot 1 used to belong to the alarm demo registered in kernel_main(). Nothing
 * registers a slot at boot any more - v1.0.0 made SYSCALL_ALARM a real
 * alarm(seconds) served by a deadline in the PCB - so the slots are all free and
 * this one is picked only to keep the choice explicit.
 */
#define KTIMER_TEST_SLOT 5

static volatile int ktimer_fire_count = 0;

static void ktimer_test_callback(void) {
    ktimer_fire_count++;
}

/**
 * @brief Tests the kernel timer bottom half: countdown, dispatch, and disarm.
 *
 * The two halves of the mechanism had no coverage at all. kernel_timer_tick_handler()
 * counts armed slots down from the timer interrupt and process_pending_kernel_timers()
 * runs the ones that have reached zero, off the interrupt path - and until this
 * release the latter was called from a point in schedule() that the "same task
 * reselected" early return skipped, so a timer could be counted down and then
 * never run for as long as nothing else became runnable.
 *
 * What this asserts is the drain itself: that a due callback runs, that it runs
 * exactly once, and that one which is not due yet waits for the ticks. The move
 * of the call site is what the Ring 3 sleep() timing assertion covers, since
 * that is the only place schedule() is genuinely entered from user mode.
 *
 * The countdown half runs with interrupts masked. IRQ0 calls
 * kernel_timer_tick_handler() a hundred times a second against the same slots,
 * so arming a two-tick delay and then ticking it by hand is a race the PIT wins:
 * the interrupt spends the delay before the test can, and the timer arrives
 * already due. The drain half needs no masking - process_pending_kernel_timers()
 * is reached only through schedule(), which IRQ0 calls solely for Ring 3 frames,
 * and these modules run at CPL 0.
 *
 * Expected Behavior:
 * - A slot armed with zero delay runs on the next drain.
 * - The slot is disarmed by the drain, so a second drain does not re-run it.
 * - A slot armed with a delay waits until the tick handler has counted it out.
 *
 * Edge Cases Covered:
 * - Re-arming a slot that has already fired.
 * - Arming a slot that has no handler registered, which must be ignored rather
 *   than dispatched through a null pointer.
 */
static void run_kernel_timer_tests(void) {
    ktimer_fire_count = 0;

    register_kernel_timer(KTIMER_TEST_SLOT, ktimer_test_callback);
    schedule_kernel_timer(KTIMER_TEST_SLOT, 0);

    process_pending_kernel_timers();
    KTEST_ASSERT(ktimer_fire_count == 1,
                 "[TIMER] a kernel timer armed with no delay runs on the next drain");

    process_pending_kernel_timers();
    KTEST_ASSERT(ktimer_fire_count == 1,
                 "[STRICT] [TIMER] the drain disarms the slot, so it does not fire twice");

    /*
     * Masked, and the results are only read out afterwards: KTEST_ASSERT prints
     * through printk(), which takes a mutex and writes to three sinks, and none
     * of that belongs inside a window with interrupts off.
     */
    asm volatile("cli");

    schedule_kernel_timer(KTIMER_TEST_SLOT, 2);
    process_pending_kernel_timers();
    int fired_when_armed = ktimer_fire_count;

    kernel_timer_tick_handler();
    process_pending_kernel_timers();
    int fired_after_one_tick = ktimer_fire_count;

    kernel_timer_tick_handler();
    process_pending_kernel_timers();
    int fired_after_two_ticks = ktimer_fire_count;

    asm volatile("sti");

    KTEST_ASSERT(fired_when_armed == 1,
                 "[TIMER] a slot with ticks left to run is not dispatched early");
    KTEST_ASSERT(fired_after_one_tick == 1,
                 "[TIMER] one tick of a two-tick delay is still not enough");
    KTEST_ASSERT(fired_after_two_ticks == 2,
                 "[STRICT] [TIMER] the slot fires once the tick handler has counted it out");

    /*
     * schedule_kernel_timer() only arms slots that already carry a handler. An
     * unregistered slot must stay disarmed: dispatching it would call through a
     * null handler_addr from inside the scheduler.
     */
    schedule_kernel_timer(KTIMER_TEST_SLOT + 1, 0);
    process_pending_kernel_timers();
    KTEST_ASSERT(ktimer_fire_count == 2,
                 "[TIMER] arming a slot with no handler registered is ignored");

    /* Leave the slot clean so later modules are not dispatched into. */
    register_kernel_timer(KTIMER_TEST_SLOT, 0);
}

/**
 * @brief Tests alarm(): arming, re-arming, cancelling, refusal, and delivery.
 *
 * Two halves, and they are separated on purpose. The first drives sys_alarm()
 * and reads the PCB, which is arithmetic and answers immediately. The second
 * asks whether expire_alarms() delivers, and it does that by writing a deadline
 * into the PCB by hand rather than by arming a real one and waiting for it.
 *
 * Waiting is what makes a timing test unreliable, and this suite already has one
 * test that fails two runs in five for want of that discipline. Nothing here
 * depends on how long the test itself takes: a deadline one tick in the past is
 * due no matter when it is read, and one ten thousand ticks ahead is not.
 *
 * The seconds assertions do have a tolerance, and it is a whole second wide: the
 * PIT is running at TIMER_HZ underneath these lines, so the remaining count only
 * has to survive fewer than a hundred ticks passing between arming and reading.
 *
 * A handler is registered for SIG_ALRM throughout. Without one the signal
 * terminates by default, and the process it would terminate is the test - the
 * bit would sit pending until the next return to Ring 3 and kill the payload
 * that had nothing to do with it. The handler, the alarm fields and the pending
 * mask are all put back at the end for the same reason.
 *
 * Expected Behavior:
 * - Arming reports 0 when there was no previous alarm, and the seconds left when
 *   there was.
 * - A zero interval cancels and still reports what it displaced.
 * - A negative or unrepresentable interval is refused without disturbing state.
 * - expire_alarms() signals a due alarm, clears it, and leaves an undue one.
 *
 * Edge Cases Covered:
 * - Re-arming over a live alarm.
 * - Cancelling when there is nothing to cancel.
 * - An interval one second past what a signed tick difference can name.
 */
static void run_alarm_tests(void) {
    arch_regs_t regs;

    uint32_t saved_deadline = current_task->alarm_deadline;
    uint8_t  saved_active   = current_task->alarm_active;
    uint32_t saved_handler  = current_task->signal_handlers[SIG_ALRM];
    uint32_t saved_pending  = current_task->pending_signals;

    current_task->signal_handlers[SIG_ALRM] = 0x80000000;
    current_task->pending_signals &= ~(1u << SIG_ALRM);
    current_task->alarm_active = 0;
    current_task->alarm_deadline = 0;

    /* Arming a task that holds no alarm reports nothing displaced. */
    ft_bzero(&regs, sizeof(regs));
    regs.ebx = 5;
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == 0,
                 "[ALARM] arming with no alarm outstanding reports 0 seconds left");
    KTEST_ASSERT(current_task->alarm_active == 1,
                 "[STRICT] [ALARM] and the task now holds one");

    uint32_t ahead = current_task->alarm_deadline - timer_get_ticks();
    KTEST_ASSERT(ahead > (uint32_t)(4 * TIMER_HZ) && ahead <= (uint32_t)(5 * TIMER_HZ),
                 "[ALARM] the deadline lands five seconds ahead, in ticks");

    /* Re-arming reports what it displaced, rounded up so that an alarm with any
     * time left never reports the 0 that means there was none. */
    ft_bzero(&regs, sizeof(regs));
    regs.ebx = 10;
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == 5,
                 "[STRICT] [ALARM] re-arming reports the seconds left on the previous alarm");

    /* Zero cancels, and still reports. */
    ft_bzero(&regs, sizeof(regs));
    regs.ebx = 0;
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == 10,
                 "[ALARM] cancelling reports what it cancelled");
    KTEST_ASSERT(current_task->alarm_active == 0,
                 "[STRICT] [ALARM] and the task holds no alarm afterwards");

    ft_bzero(&regs, sizeof(regs));
    regs.ebx = 0;
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == 0,
                 "[ALARM] cancelling nothing reports 0");

    /*
     * Refusals. ebx is read as a signed count, so a caller that passes -1 is
     * told so rather than being given an alarm 136 years out.
     */
    ft_bzero(&regs, sizeof(regs));
    regs.ebx = (uint32_t)(-1);
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == E_INVAL,
                 "[ALARM] a negative interval is refused");
    KTEST_ASSERT(current_task->alarm_active == 0,
                 "[STRICT] [ALARM] and a refused interval arms nothing");

    ft_bzero(&regs, sizeof(regs));
    regs.ebx = (0x7FFFFFFFu / TIMER_HZ) + 1;
    sys_alarm(&regs);
    KTEST_ASSERT((int)regs.eax == E_INVAL,
                 "[ALARM] an interval past what a signed tick difference can name is refused");

    /*
     * Delivery. The deadline is written directly so the answer does not depend
     * on time passing while the test runs.
     */
    current_task->alarm_deadline = timer_get_ticks() + (uint32_t)(100 * TIMER_HZ);
    current_task->alarm_active = 1;
    current_task->pending_signals &= ~(1u << SIG_ALRM);

    expire_alarms();
    KTEST_ASSERT((current_task->pending_signals & (1u << SIG_ALRM)) == 0,
                 "[STRICT] [ALARM] an alarm that is not due yet is not delivered");
    KTEST_ASSERT(current_task->alarm_active == 1,
                 "[ALARM] and it is still held");

    current_task->alarm_deadline = timer_get_ticks() - 1;

    expire_alarms();
    KTEST_ASSERT((current_task->pending_signals & (1u << SIG_ALRM)) != 0,
                 "[STRICT] [ALARM] a due alarm delivers SIG_ALRM to the process that set it");
    KTEST_ASSERT(current_task->alarm_active == 0,
                 "[STRICT] [ALARM] and is cleared, because an alarm is one-shot");

    current_task->pending_signals &= ~(1u << SIG_ALRM);
    expire_alarms();
    KTEST_ASSERT((current_task->pending_signals & (1u << SIG_ALRM)) == 0,
                 "[ALARM] a cleared alarm does not deliver a second time");

    current_task->alarm_deadline = saved_deadline;
    current_task->alarm_active = saved_active;
    current_task->signal_handlers[SIG_ALRM] = saved_handler;
    current_task->pending_signals = saved_pending;
}

/**
 * @brief Tests the inter-process signal routing and handler registration.
 *
 * This test ensures that user-space processes can accurately map specific
 * signals to custom memory handlers and that the kernel successfully dispatches
 * these signals into a pending state.
 *
 * Expected Behavior:
 * - Signal handlers are correctly registered to the task's tracking array.
 * - Firing a signal correctly flags the relevant bit in the pending_signals mask.
 * - Allows user process to correctly intercept custom exceptions like SIGKILL simulation.
 *
 * Edge Cases Covered:
 * - Proper bounds handling for signals mapping to the 0x80000000 boundary.
 * - Cleanup of pending queues and handlers to prevent cross-test contamination.
 */
void run_signal_tests(void) {
    printk("\n--- Signal Handling Tests ---\n");

    register_user_signal(9, 0x80000000); // SIGKILL simulation handler
    KTEST_ASSERT(current_task->signal_handlers[9] == 0x80000000, 
                 "Signals: User signal handler successfully registered");

    int my_pid = current_task->pid;
    send_user_signal(my_pid, 9);
    
    // Verify the signal successfully tripped the pending state flag.
    KTEST_ASSERT((current_task->pending_signals & (1 << 9)) != 0, 
                 "Signals: pending_signals bitmask updated after signal dispatch");
                 
    current_task->pending_signals &= ~(1 << 9);
    current_task->signal_handlers[9] = 0;

    run_alarm_tests();
    run_kernel_timer_tests();
}
