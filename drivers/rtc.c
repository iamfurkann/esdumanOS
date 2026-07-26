/*
 * File: rtc.c
 * Purpose: Real Time Clock (RTC) driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kernel.h"
#include "tty.h"
#define CMOS_ADDRESS 0x70
#define CMOS_DATA 0x71

/**
 * @brief Checks if the RTC update is in progress.
 * @return 1 if update is in progress, 0 otherwise.
 */
int get_update_in_progress_flag() {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

/**
 * @brief Reads a value from a specific RTC register.
 * @param reg The register to read.
 * @return The value from the register.
 */
uint8_t get_RTC_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

/**
 * @brief Retrieves the current second from the RTC.
 * @return The current second.
 */
uint8_t get_rtc_second(void) {
    while (get_update_in_progress_flag());
    return get_RTC_register(0x00);
}

/**
 * @brief Prints a two-digit number, padding with zero if necessary.
 * @param num The number to print.
 */
void print_two_digits(uint8_t num) {
    if (num < 10)
        printk("0");
    printk("%d", num);
}

/**
 * @brief Retrieves and prints the current date and time.
 */
void print_time(void) {
    char time_buf[20];
    get_time_string(time_buf);
    printk("[%s]", time_buf);
}

/**
 * @brief Retrieves the current date and time as a formatted string.
 * @param buf Pointer to the buffer where the string will be written.
 */
void get_time_string(char *buf) {
    uint8_t second, minute, hour, day, month, year, registerB;
    
    while (get_update_in_progress_flag());
    second = get_RTC_register(0x00); minute = get_RTC_register(0x02); hour = get_RTC_register(0x04);
    day = get_RTC_register(0x07); month = get_RTC_register(0x08); year = get_RTC_register(0x09);
    registerB = get_RTC_register(0x0B);

    if (!(registerB & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10); minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour = ( (hour & 0x0F) + (((hour & 0x70) / 16) * 10) ) | (hour & 0x80);
        day = (day & 0x0F) + ((day / 16) * 10); month = (month & 0x0F) + ((month / 16) * 10);
        year = (year & 0x0F) + ((year / 16) * 10);
    }
    if (!(registerB & 0x02) && (hour & 0x80)) hour = ((hour & 0x7F) + 12) % 24;

    hour = hour + 3;
    if (hour >= 24) { hour -= 24; day += 1; }

    buf[0] = (day / 10) + '0'; buf[1] = (day % 10) + '0'; buf[2] = '/';
    buf[3] = (month / 10) + '0'; buf[4] = (month % 10) + '0'; buf[5] = '/';
    buf[6] = '2'; buf[7] = '0'; buf[8] = (year / 10) + '0'; buf[9] = (year % 10) + '0'; buf[10] = ' ';
    buf[11] = (hour / 10) + '0'; buf[12] = (hour % 10) + '0'; buf[13] = ':';
    buf[14] = (minute / 10) + '0'; buf[15] = (minute % 10) + '0'; buf[16] = ':';
    buf[17] = (second / 10) + '0'; buf[18] = (second % 10) + '0'; buf[19] = '\0';
}

static uint8_t last_second = 0xFF;

/**
 * @brief Callback function triggered periodically to update the status bar time.
 */
void rtc_timer_callback(void) {
    uint8_t current_second = get_rtc_second();
    if (current_second != last_second) {
        last_second = current_second;
        char time_buf[20];
        get_time_string(time_buf);
draw_status_bar("esdumanOS", time_buf);
    }
}