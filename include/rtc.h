#ifndef RTC_H
# define RTC_H

#include "types.h"

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