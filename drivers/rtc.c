/*
 * File: rtc.c
 * Purpose: Real Time Clock (RTC) driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "kernel.h"
#include "tty.h"
#include "esdtime.h"
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
 * @brief The seven RTC registers, exactly as the chip reports them.
 *
 * Kept together so a whole reading can be compared against another one; see
 * rtc_read_time() for why that matters.
 */
typedef struct {
    uint8_t second, minute, hour, day, month, year, registerB;
} rtc_raw_t;

/**
 * @brief Reads the seven registers once, after waiting out an update.
 *
 * @param r Receives the raw register values.
 */
static void rtc_read_raw(rtc_raw_t *r) {
    while (get_update_in_progress_flag());

    r->second = get_RTC_register(0x00);
    r->minute = get_RTC_register(0x02);
    r->hour   = get_RTC_register(0x04);
    r->day    = get_RTC_register(0x07);
    r->month  = get_RTC_register(0x08);
    r->year   = get_RTC_register(0x09);
    r->registerB = get_RTC_register(0x0B);
}

/**
 * @brief Whether two readings agree in every field.
 */
static int rtc_raw_equal(const rtc_raw_t *a, const rtc_raw_t *b) {
    return a->second == b->second && a->minute == b->minute && a->hour == b->hour &&
           a->day == b->day && a->month == b->month && a->year == b->year &&
           a->registerB == b->registerB;
}

/**
 * @brief Whether a full year is a leap year in the proleptic Gregorian calendar.
 */
static int rtc_is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief Days in a month of a given year.
 *
 * @param month 1-12; anything else answers 31, which is the value that cannot
 *              make a carry happen early.
 * @param year Full year, for February.
 */
static int rtc_days_in_month(int month, int year) {
    static const uint8_t length[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 1 || month > 12) return 31;
    if (month == 2 && rtc_is_leap(year)) return 29;
    return length[month - 1];
}

/*
 * Hours ahead of UTC, as the running system believes it.
 *
 * A build-time constant until /etc/timezone existed, which meant changing the
 * offset meant rebuilding the kernel. RTC_TZ_OFFSET_HOURS is the default this
 * starts at; kernel_main() overwrites it once the filesystem is up, and until
 * then - which is the first status bar draw and nothing else - the default
 * stands.
 */
static int rtc_tz_offset = RTC_TZ_OFFSET_HOURS;

/**
 * @brief Sets the offset local time is reported at.
 *
 * @param hours Hours ahead of UTC. Refused unless -12..14, which is the range
 *              real zones occupy; anything else is a misparsed file rather than
 *              a place, and applying it would move the date by days.
 * @return E_OK when it was accepted, E_INVAL otherwise.
 */
int rtc_set_tz_offset(int hours) {
    if (hours < -12 || hours > 14) return E_INVAL;
    rtc_tz_offset = hours;
    return E_OK;
}

/**
 * @brief The offset local time is currently reported at.
 */
int rtc_get_tz_offset(void) {
    return rtc_tz_offset;
}

/**
 * @brief Reads the current UTC time from the chip, race-free.
 *
 * The single place the hardware is touched. Everything else that wants the time
 * is built on this and on rtc_apply_timezone(), so the read and the arithmetic
 * are each written once.
 *
 * Read twice and compared, which is the fix for a real race. The update flag was
 * checked once and then seven registers were read one after another: the RTC is
 * free to begin an update in the middle of that, and the values that come back
 * straddle it. At a second boundary that produces a time like 10:59:00 when it is
 * either 10:58:59 or 10:59:00 - and at a midnight boundary it produces a date
 * that never existed. Two readings that agree cannot straddle an update, because
 * an update always changes at least the seconds.
 *
 * The chip is assumed to hold UTC, which is what QEMU presents by default and
 * what this is built against. A machine whose CMOS holds local time would need
 * its offset set to 0 rather than the real one.
 *
 * @param out Receives the time, unadjusted, with tz_offset_hours zero.
 */
void rtc_read_utc(esd_time_t *out) {
    if (out == 0) return;

    rtc_raw_t current, previous;

    rtc_read_raw(&previous);
    for (;;) {
        rtc_read_raw(&current);
        if (rtc_raw_equal(&current, &previous)) break;
        previous = current;
    }

    int second = current.second, minute = current.minute, hour = current.hour;
    int day = current.day, month = current.month, year = current.year;
    uint8_t registerB = current.registerB;

    /* Bit 2 of register B clear means the values are BCD rather than binary. */
    if (!(registerB & 0x04)) {
        second = (second & 0x0F) + ((second / 16) * 10);
        minute = (minute & 0x0F) + ((minute / 16) * 10);
        hour   = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
        day    = (day & 0x0F) + ((day / 16) * 10);
        month  = (month & 0x0F) + ((month / 16) * 10);
        year   = (year & 0x0F) + ((year / 16) * 10);
    }

    /* Bit 1 clear means 12-hour mode, with bit 7 of the hour marking PM. */
    if (!(registerB & 0x02) && (hour & 0x80)) hour = ((hour & 0x7F) + 12) % 24;

    out->year   = (uint16_t)(year + 2000);
    out->month  = (uint8_t)month;
    out->day    = (uint8_t)day;
    out->hour   = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->second = (uint8_t)second;
    out->tz_offset_hours = 0;
}

/**
 * @brief Reads the current time and shifts it to local.
 *
 * @param out Receives the time, adjusted by the configured offset.
 */
void rtc_read_local(esd_time_t *out) {
    if (out == 0) return;

    rtc_read_utc(out);
    rtc_apply_timezone(out, rtc_tz_offset);
}

/**
 * @brief Shifts a time by a whole number of hours, carrying the calendar.
 *
 * Separate from rtc_read_time() so it can be tested. The defect this replaces
 * was here and not in the hardware read: the offset was applied as "hour += 3"
 * with a day carry that never looked at how long the month was, so 21:00 UTC on
 * the 31st of August produced 32/08 - and 31 December produced 32/12 rather than
 * the 1st of January.
 *
 * Both directions are written even though the offset in use is positive. Leaving
 * half of it out would be a trap for whoever changes the constant, and the
 * backward case is where February and the year boundary are easiest to get
 * wrong.
 *
 * @param t Time to shift, in place. Its tz_offset_hours is updated.
 * @param offset_hours Hours to add; may be negative.
 */
void rtc_apply_timezone(esd_time_t *t, int offset_hours) {
    if (t == 0) return;

    int hour = t->hour, day = t->day, month = t->month, year = t->year;

    hour += offset_hours;

    while (hour >= 24) {
        hour -= 24;
        day++;
        if (day > rtc_days_in_month(month, year)) {
            day = 1;
            if (++month > 12) { month = 1; year++; }
        }
    }
    while (hour < 0) {
        hour += 24;
        day--;
        if (day < 1) {
            if (--month < 1) { month = 12; year--; }
            day = rtc_days_in_month(month, year);
        }
    }

    t->hour  = (uint8_t)hour;
    t->day   = (uint8_t)day;
    t->month = (uint8_t)month;
    t->year  = (uint16_t)year;
    t->tz_offset_hours = (int8_t)(t->tz_offset_hours + offset_hours);
}

/**
 * @brief Retrieves the current date and time as a formatted string.
 * @param buf Pointer to the buffer where the string will be written, at least 20 bytes.
 */
void get_time_string(char *buf) {
    esd_time_t t;
    rtc_read_local(&t);

    /* The year is printed in full from the struct rather than assuming "20" and
     * two digits, which is what the century was before. */
    buf[0] = (t.day / 10) + '0';    buf[1] = (t.day % 10) + '0';    buf[2] = '/';
    buf[3] = (t.month / 10) + '0';  buf[4] = (t.month % 10) + '0';  buf[5] = '/';
    buf[6] = ((t.year / 1000) % 10) + '0';
    buf[7] = ((t.year / 100) % 10) + '0';
    buf[8] = ((t.year / 10) % 10) + '0';
    buf[9] = (t.year % 10) + '0';
    buf[10] = ' ';
    buf[11] = (t.hour / 10) + '0';   buf[12] = (t.hour % 10) + '0';   buf[13] = ':';
    buf[14] = (t.minute / 10) + '0'; buf[15] = (t.minute % 10) + '0'; buf[16] = ':';
    buf[17] = (t.second / 10) + '0'; buf[18] = (t.second % 10) + '0'; buf[19] = '\0';
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
        /* The same label the boot-time call in kernel_main() uses. The two used
         * to disagree - that one passed the version string and this one a
         * literal - so the left half of the status bar changed as soon as the
         * clock first ticked. */
        draw_status_bar(OS_STATUS_LABEL, time_buf);
    }
}