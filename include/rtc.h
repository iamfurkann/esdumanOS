#ifndef RTC_H
# define RTC_H

#include "types.h"

void print_time(void);
void get_time_string(char *buf);
uint8_t get_rtc_second(void);

# endif