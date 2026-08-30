#include "types.h"
#include "io.h"
#include "entropy.h"

/*
 * This file defines four public symbols - timer_ticks, timer_interrupt_handler,
 * init_timer and timer_get_ticks - and used to include none of the headers that
 * declare them, so nothing ever compared a definition against its declaration.
 * That is exactly how the missing `volatile` on timer_ticks survived in rtc.h.
 * Including them means the compiler checks.
 */
#include "rtc.h"     /* timer_ticks, timer_get_ticks, TIMER_HZ            */
#include "isr.h"     /* timer_interrupt_handler                          */
#include "signal.h"  /* kernel_timer_tick_handler                        */
#include "xhci.h"    /* xhci_poll                                        */

#define PIT_CMD_PORT 0x43
#define PIT_CH0_PORT 0x40
#define PIT_CMD_INIT 0x36
#define PIT_BASE_FREQ 1193180

volatile uint32_t timer_ticks = 0;

/* Defined in drivers/rtc.c, which has no header entry for it. */
extern void rtc_timer_callback(void);

/**
 * Handles the Programmable Interval Timer (PIT) interrupt.
 * Expected behavior: Increments the global tick counter and invokes any registered callbacks
 * for the RTC or kernel timer ticks. This is called on every IRQ0.
 */
void timer_interrupt_handler(void) {
    timer_ticks++;

    /*
     * Mixed into the entropy pool but credited zero bits: the PIT fires at a
     * fixed rate, so the interval between two of these carries no entropy however
     * it is measured. Feeding it in costs nothing and keeps the pool stirred;
     * counting it would be lying to ourselves. See entropy_add_event().
     */
    entropy_add_event(ENTROPY_SRC_TIMER, timer_ticks);

    rtc_timer_callback();
    kernel_timer_tick_handler();

    /*
     * The USB keyboard, read from here and not from the bottom half beside it.
     *
     * kernel_timer_tick_handler() above only counts down; the handlers it arms
     * run from process_pending_kernel_timers(), which the scheduler calls - and
     * isr.c only calls the scheduler when the interrupted context was Ring 3. So
     * a keyboard driven from that mechanism would go silent for as long as the
     * kernel stayed in Ring 0, and the first thing anybody types on a real
     * machine is the disk passphrase, at a prompt that is a Ring 0 read loop on
     * the boot path. It would be unreachable at exactly the moment it is needed.
     *
     * A direct call runs on every tick at any privilege level, which is why it is
     * here. The price is interrupt context, and xhci_poll() is written to it: it
     * waits on nothing, drains at most a bounded number of events, and returns
     * immediately when there is none - which is what it does on almost every
     * tick.
     *
     * Nothing is fed to the entropy pool from this. entropy_add_event() ran a
     * few lines above on this same tick, so a keystroke arriving here would be a
     * second sample of one instant; the timer's own justification for being
     * mixed in - that it keeps the pool stirred - has already been spent by the
     * time this line is reached.
     */
    xhci_poll();
}

/**
 * Initializes the Programmable Interval Timer (PIT) to a specific frequency.
 * Expected behavior: Calculates the appropriate divisor and configures PIT Channel 0
 * to generate interrupts at the requested frequency.
 */
void init_timer(uint32_t freq) {
    uint32_t divisor = PIT_BASE_FREQ / freq;
    outb(PIT_CMD_PORT, PIT_CMD_INIT);

    uint8_t low = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);

    outb(PIT_CH0_PORT, low);
    outb(PIT_CH0_PORT, high);
}

/**
 * Retrieves the total number of timer ticks since boot.
 * Expected behavior: Returns the current value of the volatile global tick counter.
 */
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}