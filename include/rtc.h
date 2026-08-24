#ifndef RTC_H
# define RTC_H

#include "types.h"
#include "esdtime.h"

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
 * @brief Reads the current UTC time from the RTC.
 *
 * The one place the chip is touched. Race-free: the registers are read twice and
 * compared, because the update flag can only be checked before the reads and the
 * RTC is free to begin an update in the middle of them.
 *
 * @param out Receives the time unadjusted, with tz_offset_hours zero.
 */
void rtc_read_utc(esd_time_t *out);

/**
 * @brief Reads the current time and shifts it to local.
 *
 * @param out Receives the time, adjusted by the configured offset.
 */
void rtc_read_local(esd_time_t *out);

/**
 * @brief Sets the hardware clock, in UTC.
 *
 * The clock could be read and not set until v0.9.2. Writes in whatever format
 * register B says the chip is in, and halts the update cycle for the duration so
 * that a tick cannot land between two of the seven registers.
 *
 * @param t Time to set, in UTC; year 2000 to 2099.
 * @return E_OK, or E_INVAL when the fields are not a date this can store.
 */
int rtc_set_utc(const esd_time_t *t);

/**
 * @brief Sets the offset local time is reported at.
 *
 * Read from /etc/timezone at boot. It was a build-time constant, so the only way
 * to correct it was to rebuild the kernel.
 *
 * @param hours Hours ahead of UTC; refused unless -12..14.
 * @return E_OK when accepted, E_INVAL otherwise.
 */
int rtc_set_tz_offset(int hours);

/**
 * @brief The offset local time is currently reported at.
 */
int rtc_get_tz_offset(void);

/**
 * @brief Shifts a time by a whole number of hours, carrying the calendar.
 *
 * Exposed because this is where the defect was, not in the hardware read: the
 * offset was applied with a day carry that never looked at the length of the
 * month, so 21:00 UTC on 31 August produced 32/08. A test can drive this with
 * any date; it cannot make the RTC report one.
 *
 * @param t Time to shift, in place. Its tz_offset_hours is updated.
 * @param offset_hours Hours to add; may be negative.
 */
void rtc_apply_timezone(esd_time_t *t, int offset_hours);

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
