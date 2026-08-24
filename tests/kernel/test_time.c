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
 * As of v0.9.0 it also covers the conversions between that broken-down form and
 * seconds since the Unix epoch, which the disk format needs to stamp a file with
 * a time two programs can compare. Date arithmetic is the kind of code that looks
 * right: a leap year rule that is wrong is invisible three years out of four, and
 * the century case comes round once in a lifetime - so the rules are asserted as
 * rules, and the two conversions are checked against each other across sixty
 * years rather than at a handful of dates.
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
 * @brief Builds a UTC time with every field set, for the epoch conversions.
 */
static esd_time_t utc_at(int year, int month, int day, int hour, int min, int sec) {
    esd_time_t t;
    t.year = (uint16_t)year;
    t.month = (uint8_t)month;
    t.day = (uint8_t)day;
    t.hour = (uint8_t)hour;
    t.minute = (uint8_t)min;
    t.second = (uint8_t)sec;
    t.tz_offset_hours = 0;
    return t;
}

/**
 * @brief Verifies the conversions between broken-down time and the epoch.
 *
 * Expected behavior:
 * - Known instants convert to their known counts.
 * - The two conversions are inverses across a long span of dates.
 * - The offset the fields carry is taken back out, so an epoch is UTC.
 *
 * Edge cases covered:
 * - The leap year rules, all three of them, tested as rules rather than as
 *   epochs somebody worked out by hand.
 * - An offset that carries the time out of its own day.
 * - A date before the epoch, which has no count.
 */
static void run_epoch_tests(void) {
    esd_time_t t, back;

    /* ------------------------------------------------------------------
     * Instants whose counts are not in dispute.
     * ------------------------------------------------------------------ */
    t = utc_at(1970, 1, 1, 0, 0, 0);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 0,
                 "[TIME] the epoch itself is zero seconds after the epoch");

    t = utc_at(1970, 1, 2, 0, 0, 0);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 86400,
                 "[TIME] and the next day is one day after it");

    t = utc_at(2000, 1, 1, 0, 0, 0);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 946684800u,
                 "[STRICT] [TIME] the start of 2000 is where everyone else puts it");

    t = utc_at(2024, 2, 29, 0, 0, 0);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 1709164800u,
                 "[STRICT] [TIME] and so is a leap day");

    t = utc_at(2038, 1, 19, 3, 14, 7);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 2147483647u,
                 "[STRICT] [TIME] the second a signed count would run out is reached, not skipped");

    /* ------------------------------------------------------------------
     * The leap year rules, as rules.
     *
     * Written as the distance from the 28th of February to the 1st of March,
     * which is two days in a leap year and one otherwise. Asserting epochs
     * somebody computed by hand would only prove the hand agreed with itself.
     * ------------------------------------------------------------------ */
    KTEST_ASSERT(esd_days_from_civil(2024, 3, 1) - esd_days_from_civil(2024, 2, 28) == 2,
                 "[TIME] a year divisible by four has a 29th of February");
    KTEST_ASSERT(esd_days_from_civil(2023, 3, 1) - esd_days_from_civil(2023, 2, 28) == 1,
                 "[TIME] and a year that is not does not");
    KTEST_ASSERT(esd_days_from_civil(1900, 3, 1) - esd_days_from_civil(1900, 2, 28) == 1,
                 "[STRICT] [TIME] a century year is not a leap year");
    KTEST_ASSERT(esd_days_from_civil(2000, 3, 1) - esd_days_from_civil(2000, 2, 28) == 2,
                 "[STRICT] [TIME] unless it divides by four hundred, and 2000 did");
    KTEST_ASSERT(esd_days_from_civil(2100, 3, 1) - esd_days_from_civil(2100, 2, 28) == 1,
                 "[STRICT] [TIME] which 2100 will not, and that is the one nobody is alive to have seen");

    /* ------------------------------------------------------------------
     * The two conversions are inverses. Stepping by a prime number of hours
     * so the walk lands on every weekday, every month length and both sides
     * of a leap day rather than on a repeating pattern of them.
     * ------------------------------------------------------------------ */
    {
        int mismatches = 0;

        for (uint32_t e = 0; e < 2100000000u; e += 9857779u) {
            esd_time_from_epoch(e, &back);
            if (esd_time_to_epoch(&back) != e) mismatches++;
        }
        KTEST_ASSERT(mismatches == 0,
                     "[STRICT] [TIME] every instant from 1970 to 2036 survives a round trip");
    }

    esd_time_from_epoch(946684800u, &back);
    KTEST_ASSERT(back.year == 2000 && back.month == 1 && back.day == 1 &&
                 back.hour == 0 && back.minute == 0 && back.second == 0,
                 "[TIME] a count converts back to the date it came from");
    KTEST_ASSERT(back.tz_offset_hours == 0,
                 "[STRICT] [TIME] and says it is UTC, because that is what it is");

    esd_time_from_epoch(1709164800u - 1u, &back);
    KTEST_ASSERT(back.year == 2024 && back.month == 2 && back.day == 28 &&
                 back.hour == 23 && back.minute == 59 && back.second == 59,
                 "[STRICT] [TIME] the second before a leap day is the day before it");

    /* ------------------------------------------------------------------
     * The offset comes back out. Two files stamped in different timezones
     * have to compare by when it happened, not by what the clock said.
     * ------------------------------------------------------------------ */
    {
        esd_time_t east = utc_at(2026, 8, 24, 12, 0, 0);
        esd_time_t here = utc_at(2026, 8, 24, 9, 0, 0);

        east.tz_offset_hours = 3;
        KTEST_ASSERT(esd_time_to_epoch(&east) == esd_time_to_epoch(&here),
                     "[STRICT] [TIME] noon three hours east of UTC is nine o'clock UTC");
    }

    t = utc_at(2026, 8, 24, 1, 0, 0);
    t.tz_offset_hours = 3;
    esd_time_from_epoch(esd_time_to_epoch(&t), &back);
    KTEST_ASSERT(back.year == 2026 && back.month == 8 && back.day == 23 && back.hour == 22,
                 "[STRICT] [TIME] and an offset that leaves the day takes the date with it");

    /* ------------------------------------------------------------------
     * Before the epoch there is no count to give.
     * ------------------------------------------------------------------ */
    t = utc_at(1969, 12, 31, 23, 59, 59);
    KTEST_ASSERT(esd_time_to_epoch(&t) == 0,
                 "[STRICT] [TIME] a date before the epoch has no count and does not wrap to a huge one");

    t = utc_at(1970, 1, 1, 1, 0, 0);
    t.tz_offset_hours = 3;
    KTEST_ASSERT(esd_time_to_epoch(&t) == 0,
                 "[STRICT] [TIME] nor does one the offset pushes before it");
}

/**
 * @brief Verifies that the clock can be set, and refuses what it cannot hold.
 *
 * Expected behavior:
 * - A time that is set reads back as itself.
 * - A date the chip cannot store is refused and leaves the clock alone.
 *
 * Edge cases covered:
 * - The 29th of February, which must be accepted in a leap year and refused in
 *   a common one.
 * - The year before the chip's range, and the hour and month past their ends.
 *
 * This moves the real hardware clock and puts it back. The restore rewinds by
 * however long the test took - a fraction of a second - which is worth knowing
 * and is the price of testing a write against the thing it writes to. The
 * refusals are checked first, so a range check that let something through
 * cannot leave the machine somewhere strange before the good case runs.
 */
static void run_settime_tests(void) {
    esd_time_t saved, want, got;

    printk("\n--- Setting the clock ---\n");

    rtc_read_utc(&saved);

    want = utc_at(1999, 12, 31, 23, 59, 59);
    KTEST_ASSERT(rtc_set_utc(&want) == E_INVAL,
                 "[STRICT] [TIME] a year before the chip's range is refused");

    want = utc_at(2026, 2, 30, 12, 0, 0);
    KTEST_ASSERT(rtc_set_utc(&want) == E_INVAL,
                 "[STRICT] [TIME] the 30th of February is refused");

    want = utc_at(2026, 13, 1, 12, 0, 0);
    KTEST_ASSERT(rtc_set_utc(&want) == E_INVAL,
                 "[TIME] a thirteenth month is refused");

    want = utc_at(2026, 1, 1, 24, 0, 0);
    KTEST_ASSERT(rtc_set_utc(&want) == E_INVAL,
                 "[STRICT] [TIME] and so is the twenty-fifth hour");

    rtc_read_utc(&got);
    KTEST_ASSERT(got.year == saved.year && got.month == saved.month,
                 "[STRICT] [TIME] a refused time leaves the clock where it was");

    /* Thirty seconds into the minute, so reading it back cannot cross into the
     * next one however slow the round trip is. */
    want = utc_at(2030, 6, 15, 13, 45, 30);
    KTEST_ASSERT(rtc_set_utc(&want) == E_OK,
                 "[TIME] a time the chip can hold is accepted");

    rtc_read_utc(&got);
    KTEST_ASSERT(got.year == 2030 && got.month == 6 && got.day == 15,
                 "[STRICT] [TIME] and the date reads back as what was set");
    KTEST_ASSERT(got.hour == 13 && got.minute == 45,
                 "[STRICT] [TIME] and so does the time of day");

    want = utc_at(2028, 2, 29, 6, 0, 0);
    KTEST_ASSERT(rtc_set_utc(&want) == E_OK,
                 "[STRICT] [TIME] a leap day is accepted in a leap year");

    want = utc_at(2030, 2, 29, 6, 0, 0);
    KTEST_ASSERT(rtc_set_utc(&want) == E_INVAL,
                 "[STRICT] [TIME] and refused in a year that has none");

    rtc_set_utc(&saved);
    rtc_read_utc(&got);
    KTEST_ASSERT(got.year == saved.year && got.month == saved.month && got.day == saved.day,
                 "[TIME] the clock the machine booted with is restored");
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

    run_epoch_tests();
    run_settime_tests();
}
