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

# endif
// --- Added by Refactor Script ---
extern uint32_t timer_get_ticks(void);
extern uint32_t timer_ticks;

