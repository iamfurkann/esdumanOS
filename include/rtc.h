#ifndef RTC_H
# define RTC_H

#include "types.h"

/**
 * @brief PIT interrupt frequency, in hertz.
 *
 * The rate init_timer() is called with, and therefore the rate timer_get_ticks()
 * advances at. It exists because the number 100 used to be a literal in
 * kernel_main() with the same assumption then restated in prose in three other
 * places - a tick-to-seconds conversion that nothing would have corrected if the
 * PIT rate ever changed.
 */
#define TIMER_HZ 100

/**
 * @brief Prints the current system time.
 * 
 * Retrieves the current time from the Real-Time Clock (RTC) and prints it 
 * to the standard output or terminal.
 */
void print_time(void);

/**
 * @brief Retrieves the current time as a formatted string.
 * 
 * Fetches the current time from the RTC and formats it into the provided 
 * buffer (e.g., "HH:MM:SS").
 * 
 * @param buf The character buffer where the time string will be stored.
 */
void get_time_string(char *buf);

/**
 * @brief Retrieves the current seconds from the RTC.
 * 
 * Communicates with the RTC hardware to fetch the seconds value of the 
 * current time.
 * 
 * @return uint8_t The current seconds (0-59).
 */
uint8_t get_rtc_second(void);

/**
 * @brief Ticks elapsed since boot, at TIMER_HZ.
 *
 * @return The current value of the tick counter.
 */
extern uint32_t timer_get_ticks(void);

/**
 * @brief The tick counter itself.
 *
 * Declared volatile because it is: timer_interrupt_handler() increments it from
 * IRQ0, and the definition in arch/x86/cpu/timer.c says so. This declaration did
 * not, and the tree builds at -O2 - so a loop that waited on the counter through
 * this header could have had the load hoisted out of it and spun forever.
 *
 * Nothing reads it through here today; every caller goes through
 * timer_get_ticks(). It survived because timer.c includes none of the headers
 * that declare what it defines, so the two were never compared. That is fixed
 * alongside this.
 *
 * Prefer timer_get_ticks(). This is exposed only for code that genuinely needs
 * the variable.
 */
extern volatile uint32_t timer_ticks;

/*
 * These two used to sit below the #endif, outside the include guard. Repeated
 * extern declarations are legal, so nothing broke - but the first typedef or
 * inline definition added there would have broken every translation unit that
 * includes this header, all at once.
 */

# endif
