/*
 * File: test_signal.c
 * Purpose: Signal handling unit tests.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "process.h"
#include "signal.h"

/*
 * Kernel timer slot used by the test below. Slot 1 belongs to the alarm demo
 * registered in kernel_main(); anything well clear of it will do.
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

    run_kernel_timer_tests();
}
