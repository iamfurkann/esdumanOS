#include "types.h"
#include "io.h"
#include "entropy.h"

#define PIT_CMD_PORT 0x43
#define PIT_CH0_PORT 0x40
#define PIT_CMD_INIT 0x36
#define PIT_BASE_FREQ 1193180

volatile uint32_t timer_ticks = 0; 
extern void rtc_timer_callback(void);
extern void kernel_timer_tick_handler(void);

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