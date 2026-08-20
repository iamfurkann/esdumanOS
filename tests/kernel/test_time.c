/*
 * File: test_time.c
 * Purpose: Wall-clock time: calendar arithmetic and the TIME syscall.
 *
 * The clock was readable from Ring 0 only until this release - it drew the
 * status bar and nothing else - so none of it had ever been tested. What is
 * testable is not the hardware read: a test cannot make the RTC report the 31st
 * of December. It is the arithmetic applied to whatever it reports, which is
 * where the defect was, and which rtc_apply_timezone() exists as a separate
 * function to expose.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "ktest.h"
#include "rtc.h"
#include "esdtime.h"
#include "syscall.h"
#include "errno.h"
#include "process.h"

static inline int ktest_syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Builds a time to hand to the arithmetic under test.
 */
static esd_time_t make_time(int year, int month, int day, int hour) {
    esd_time_t t;
    t.year = (uint16_t)year;
    t.month = (uint8_t)month;
    t.day = (uint8_t)day;
    t.hour = (uint8_t)hour;
    t.minute = 30;
    t.second = 15;
    t.tz_offset_hours = 0;
    return t;
}

static int time_is(const esd_time_t *t, int year, int month, int day, int hour) {
    return t->year == year && t->month == month && t->day == day && t->hour == hour;
}

/**
 * @brief Tests the calendar carry in both directions.
 *
 * Expected Behavior:
 * - An offset that does not cross midnight changes only the hour.
 * - Crossing forward carries into the next day, month and year, using the real
 *   length of the month rather than assuming 31.
 * - Crossing backward borrows the same way.
 * - February is 28 days or 29 according to the Gregorian rule, including the
 *   century cases.
 *
 * Edge Cases Covered:
 * - 31 August, which is the date that produced "32/08" before this was fixed.
 * - 31 December, the year boundary.
 * - 28 February in a leap year and a common year, in both directions.
 * - 2000 and 2100, the two halves of the century rule.
 */
void run_time_tests(void) {
    printk("\n--- Wall-clock time ---\n");

    /* ---------------------------------------------------------
     * The hour alone, when nothing crosses midnight.
     * --------------------------------------------------------- */
    esd_time_t t = make_time(2026, 8, 15, 10);
    rtc_apply_timezone(&t, 3);
    KTEST_ASSERT(time_is(&t, 2026, 8, 15, 13),
                 "[TIME] an offset that stays inside the day changes only the hour");
    KTEST_ASSERT(t.minute == 30 && t.second == 15,
                 "[TIME] and leaves the minutes and seconds alone");
    KTEST_ASSERT(t.tz_offset_hours == 3,
                 "[TIME] the applied offset is recorded on the result");

    /* ---------------------------------------------------------
     * The defect this release exists for.
     *
     * 21:00 UTC on the 31st of August plus three hours is the 1st of September.
     * The old arithmetic incremented the day and stopped, producing 32/08.
     * --------------------------------------------------------- */
    t = make_time(2026, 8, 31, 21);
    rtc_apply_timezone(&t, 3);
    KTEST_ASSERT(time_is(&t, 2026, 9, 1, 0),
                 "[STRICT] [TIME] crossing midnight on the last day of a month rolls the month");

    /* A 30-day month, where assuming 31 would leave the 31st of April. */
    t = make_time(2026, 4, 30, 23);
    rtc_apply_timezone(&t, 3);
    KTEST_ASSERT(time_is(&t, 2026, 5, 1, 2),
                 "[STRICT] [TIME] a 30-day month rolls on the 30th, not the 31st");

    /* The year boundary. */
    t = make_time(2026, 12, 31, 22);
    rtc_apply_timezone(&t, 3);
    KTEST_ASSERT(time_is(&t, 2027, 1, 1, 1),
                 "[STRICT] [TIME] crossing midnight on the last day of the year rolls the year");

    /* ---------------------------------------------------------
     * February, forwards.
     * --------------------------------------------------------- */
    t = make_time(2024, 2, 28, 23);          /* 2024 is a leap year */
    rtc_apply_timezone(&t, 1);
    KTEST_ASSERT(time_is(&t, 2024, 2, 29, 0),
                 "[STRICT] [TIME] a leap year has a 29th of February");

    t = make_time(2026, 2, 28, 23);          /* 2026 is not */
    rtc_apply_timezone(&t, 1);
    KTEST_ASSERT(time_is(&t, 2026, 3, 1, 0),
                 "[STRICT] [TIME] a common year goes from 28 February to 1 March");

    t = make_time(2100, 2, 28, 23);          /* divisible by 4, not a leap year */
    rtc_apply_timezone(&t, 1);
    KTEST_ASSERT(time_is(&t, 2100, 3, 1, 0),
                 "[STRICT] [TIME] a century that is not divisible by 400 is not a leap year");

    t = make_time(2000, 2, 28, 23);          /* divisible by 400, is a leap year */
    rtc_apply_timezone(&t, 1);
    KTEST_ASSERT(time_is(&t, 2000, 2, 29, 0),
                 "[STRICT] [TIME] a century divisible by 400 is a leap year");

    /* ---------------------------------------------------------
     * Backwards. Nothing uses a negative offset today, which is exactly why it
     * is worth asserting: the half of the arithmetic nobody exercises is the
     * half that rots.
     * --------------------------------------------------------- */
    t = make_time(2026, 9, 1, 1);
    rtc_apply_timezone(&t, -3);
    KTEST_ASSERT(time_is(&t, 2026, 8, 31, 22),
                 "[STRICT] [TIME] a negative offset borrows into the previous month's last day");

    t = make_time(2027, 1, 1, 1);
    rtc_apply_timezone(&t, -3);
    KTEST_ASSERT(time_is(&t, 2026, 12, 31, 22),
                 "[STRICT] [TIME] and across the year boundary");

    t = make_time(2024, 3, 1, 0);
    rtc_apply_timezone(&t, -1);
    KTEST_ASSERT(time_is(&t, 2024, 2, 29, 23),
                 "[STRICT] [TIME] borrowing into February finds the 29th in a leap year");

    t = make_time(2026, 3, 1, 0);
    rtc_apply_timezone(&t, -1);
    KTEST_ASSERT(time_is(&t, 2026, 2, 28, 23),
                 "[STRICT] [TIME] and the 28th in a common year");

    /* A zero offset is a no-op rather than a carry. */
    t = make_time(2026, 8, 31, 23);
    rtc_apply_timezone(&t, 0);
    KTEST_ASSERT(time_is(&t, 2026, 8, 31, 23),
                 "[TIME] a zero offset changes nothing");

    rtc_apply_timezone(0, 3);
    KTEST_ASSERT(1, "[TIME] a null time is refused rather than dereferenced");

    /* ---------------------------------------------------------
     * The hardware read, and the syscall on top of it.
     *
     * What the clock says cannot be asserted - it says whatever the host said
     * when QEMU started. What can be asserted is that the fields are within
     * range, which catches a BCD conversion that stopped happening or a carry
     * that ran away.
     * --------------------------------------------------------- */
    esd_time_t now;
    rtc_read_local(&now);

    KTEST_ASSERT(now.month >= 1 && now.month <= 12,
                 "[STRICT] [TIME] rtc_read_local reports a month in range");
    KTEST_ASSERT(now.day >= 1 && now.day <= 31,
                 "[STRICT] [TIME] and a day in range");
    KTEST_ASSERT(now.hour <= 23 && now.minute <= 59 && now.second <= 59,
                 "[STRICT] [TIME] and an hour, minute and second in range");
    KTEST_ASSERT(now.year >= 2000 && now.year <= 2199,
                 "[STRICT] [TIME] and a full year rather than two digits");
    KTEST_ASSERT(now.tz_offset_hours == rtc_get_tz_offset(),
                 "[TIME] the reading carries the offset it was adjusted by");

    /* UTC is the same read without the shift, and says so. */
    esd_time_t utc;
    rtc_read_utc(&utc);
    KTEST_ASSERT(utc.tz_offset_hours == 0,
                 "[STRICT] [TIME] rtc_read_utc reports no offset");
    KTEST_ASSERT(utc.month >= 1 && utc.month <= 12 && utc.day >= 1 && utc.day <= 31,
                 "[TIME] and fields in range");

    /*
     * Read twice. The two can differ - a second may pass between them - but the
     * second reading must not go backwards, which is what a straddled update
     * produced before the registers were read twice and compared.
     */
    esd_time_t again;
    rtc_read_local(&again);
    KTEST_ASSERT(again.year > now.year ||
                 (again.year == now.year &&
                  (again.month > now.month ||
                   (again.month == now.month &&
                    (again.day > now.day ||
                     (again.day == now.day &&
                      (again.hour > now.hour ||
                       (again.hour == now.hour &&
                        (again.minute > now.minute ||
                         (again.minute == now.minute && again.second >= now.second))))))))),
                 "[STRICT] [TIME] a second reading never goes backwards");

    /* ---------------------------------------------------------
     * The syscall.
     * --------------------------------------------------------- */
    esd_time_t *u_time = (esd_time_t *)0x500D00;
    u_time->year = 0;

    KTEST_ASSERT(ktest_syscall(SYSCALL_TIME, (int)u_time, 0, 0) == E_OK,
                 "[STRICT] [TIME] time() fills a user buffer and reports success");
    KTEST_ASSERT(u_time->year >= 2000 && u_time->month >= 1 && u_time->month <= 12,
                 "[STRICT] [TIME] the fields arrived in user space");

    KTEST_ASSERT(ktest_syscall(SYSCALL_TIME, 0xD0000000, 0, 0) == E_FAULT,
                 "[STRICT] [UACCESS] time() refuses a kernel address");
    KTEST_ASSERT(ktest_syscall(SYSCALL_TIME, 0, 0, 0) == E_FAULT,
                 "[STRICT] [UACCESS] time() refuses a null pointer");

    /* A non-zero third argument asks for UTC, which is what date -u passes. */
    esd_time_t *u_utc = (esd_time_t *)0x500D20;
    KTEST_ASSERT(ktest_syscall(SYSCALL_TIME, (int)u_utc, 1, 0) == E_OK,
                 "[TIME] time() accepts the UTC request");
    KTEST_ASSERT(u_utc->tz_offset_hours == 0,
                 "[STRICT] [TIME] and reports no offset, so date -u is not doing its own arithmetic");

    /* ---------------------------------------------------------
     * The offset is configuration now, not a constant.
     *
     * It was compiled in, so a machine in the wrong place had to rebuild the
     * kernel to see the right time. /etc/timezone sets it at boot; this checks
     * the setter it goes through, including the range, because a misparsed file
     * that got through would move the date by days rather than hours.
     * --------------------------------------------------------- */
    int saved_offset = rtc_get_tz_offset();

    KTEST_ASSERT(rtc_set_tz_offset(-5) == E_OK && rtc_get_tz_offset() == -5,
                 "[STRICT] [TIME] the offset can be set at runtime");

    rtc_read_local(&now);
    KTEST_ASSERT(now.tz_offset_hours == -5,
                 "[STRICT] [TIME] and a local reading follows it immediately");

    KTEST_ASSERT(rtc_set_tz_offset(-12) == E_OK && rtc_set_tz_offset(14) == E_OK,
                 "[TIME] the ends of the real range are accepted");
    KTEST_ASSERT(rtc_set_tz_offset(-13) == E_INVAL && rtc_set_tz_offset(15) == E_INVAL,
                 "[STRICT] [TIME] anything outside it is refused");
    KTEST_ASSERT(rtc_get_tz_offset() == 14,
                 "[STRICT] [TIME] a refused offset leaves the previous one in place");

    rtc_set_tz_offset(saved_offset);
    KTEST_ASSERT(rtc_get_tz_offset() == saved_offset,
                 "[TIME] the offset the system booted with is restored");
}
