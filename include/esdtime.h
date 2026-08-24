#ifndef ESDTIME_H
#define ESDTIME_H

#include "types.h"

/*
 * Named esdtime.h rather than time.h on purpose.
 *
 * The host-side tests compile with "-iquote ./include", which applies only to
 * the quoted include form, so a file called time.h would not shadow the system
 * <time.h> today. It would the moment somebody wrote -I instead, and the failure
 * would arrive in a test that has nothing to do with this header.
 */

/**
 * @brief Hours to add to the RTC's UTC reading.
 *
 * The offset used to be the literal 3 in the middle of the date arithmetic, with
 * no name and no way to find every place that assumed it. There is no timezone
 * database and no way for a user to choose: this is the one the machine is built
 * for, and changing it means rebuilding.
 *
 * Signed, and the arithmetic handles both directions - a negative value borrows
 * from the previous day, month and year the same way a positive one carries into
 * the next. Nothing uses a negative offset today, but leaving only half the
 * arithmetic written is a trap for whoever changes this constant.
 */
#define RTC_TZ_OFFSET_HOURS 3

/**
 * @brief A wall-clock time, as the RTC reports it after adjustment.
 *
 * Shared verbatim between the kernel and user space - programs build with
 * "-I include" (see USER_CFLAGS in the Makefile), so there is one definition
 * rather than a copy on each side that could drift. Same arrangement as
 * esd_stat_t in stat.h.
 *
 * Laid out with no padding holes at -m32: a 16-bit year followed by six bytes,
 * which is eight bytes at an alignment of two. copy_to_user() moves it as a flat
 * block, and a hole would hand user space whatever the kernel stack happened to
 * be holding there.
 *
 * Broken-down fields rather than a count of seconds since an epoch. There is no
 * epoch to count from that anything else in this system agrees on, the RTC
 * reports these fields directly, and the first two consumers - date(1) and the
 * timestamps /var/log will want - both need them broken down anyway.
 */
typedef struct {
    uint16_t year;              /**< Full year, e.g. 2026. */
    uint8_t  month;             /**< 1-12. */
    uint8_t  day;               /**< 1-31. */
    uint8_t  hour;              /**< 0-23. */
    uint8_t  minute;            /**< 0-59. */
    uint8_t  second;            /**< 0-59. */
    int8_t   tz_offset_hours;   /**< Hours ahead of UTC these fields are. */
} esd_time_t;

/* ------------------------------------------------------------------------- *
 * Seconds since an epoch.
 *
 * The struct above says there is no epoch anything in this system agrees on, and
 * when it was written that was true: the consumers were date(1) and the log, and
 * both want the fields broken down. A file's timestamp is the consumer that does
 * not. Every question anybody asks of one - is this newer than that, sort these
 * by age, has it changed since I looked - is a comparison, and comparing seven
 * fields in order is a loop that gets written wrong once per program. It is also
 * four bytes on disk against eight, in an entry where the space was worth going
 * and finding.
 *
 * So the epoch exists now and it is the Unix one, 1970-01-01 00:00:00 UTC. The
 * broken-down fields stay: this converts between the two rather than replacing
 * either, and the RTC still reports what it reports.
 *
 * Header-only and pure, for the reason editbuf.h and umalloc.h are - no globals,
 * no system calls, so the test suite can call it directly. Date arithmetic is
 * exactly the kind of code that looks right: an off-by-one in a leap year rule
 * is invisible for three years out of four, and a century that is not a leap
 * year comes round once in a lifetime.
 *
 * uint32_t seconds runs out in 2106. That is written down rather than worked
 * around; a 64-bit count would cost four more bytes in every directory entry to
 * push a problem past the lifetime of the format itself.
 * ------------------------------------------------------------------------- */

/** @brief Seconds in a day. */
#define ESD_SECS_PER_DAY 86400u

/**
 * @brief Days from 1970-01-01 to a civil date.
 *
 * Shifts the year to start in March so that the leap day lands at the end of it
 * and the month-length pattern repeats every five months, which is what lets the
 * day-of-year be computed without a table. The 400-year era is the cycle the
 * Gregorian rules actually repeat on: 146097 days, exactly.
 *
 * @param y Full year, 1970 or later.
 * @param m Month, 1-12.
 * @param d Day, 1-31.
 * @return Days since 1970-01-01, negative for dates before it.
 */
static inline int esd_days_from_civil(int y, int m, int d) {
    int era, yoe, doy, doe;

    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;                                   /* [0, 399] */
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  /* [0, 365] */
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;           /* [0, 146096] */

    return era * 146097 + doe - 719468;
}

/**
 * @brief The civil date a day count lands on.
 *
 * The inverse of esd_days_from_civil(), and written as its inverse rather than
 * from a different derivation - the two are tested against each other, which is
 * only evidence if they can disagree.
 *
 * @param days Days since 1970-01-01.
 * @param y Receives the full year.
 * @param m Receives the month, 1-12.
 * @param d Receives the day, 1-31.
 */
static inline void esd_civil_from_days(int days, int *y, int *m, int *d) {
    int z = days + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;                                          /* [0, 146096] */
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;     /* [0, 399] */
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                   /* [0, 365] */
    int mp = (5 * doy + 2) / 153;                                        /* [0, 11] */

    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yoe + era * 400 + (*m <= 2);
}

/**
 * @brief Converts a wall-clock time to seconds since the Unix epoch.
 *
 * The fields carry the offset they were adjusted by, and it is taken back out
 * here: an epoch is UTC or it is not an epoch, and two files stamped in different
 * timezones would otherwise compare by when somebody's clock said rather than by
 * when it happened.
 *
 * @param t The time.
 * @return Seconds since 1970-01-01 UTC, or 0 for a date before it.
 */
static inline uint32_t esd_time_to_epoch(const esd_time_t *t) {
    int days;
    int secs;

    if (t->year < 1970) return 0;

    days = esd_days_from_civil((int)t->year, (int)t->month, (int)t->day);
    secs = (int)t->hour * 3600 + (int)t->minute * 60 + (int)t->second
         - (int)t->tz_offset_hours * 3600;

    /* The offset can carry the time out of its own day in either direction, and
     * the day count absorbs it rather than the seconds wrapping. */
    while (secs < 0)      { secs += (int)ESD_SECS_PER_DAY; days--; }
    while (secs >= (int)ESD_SECS_PER_DAY) { secs -= (int)ESD_SECS_PER_DAY; days++; }

    if (days < 0) return 0;
    return (uint32_t)days * ESD_SECS_PER_DAY + (uint32_t)secs;
}

/**
 * @brief Converts seconds since the Unix epoch back to a wall-clock time.
 *
 * The result is UTC and says so - tz_offset_hours is 0. A caller that wants it
 * shown in local time applies rtc_apply_timezone() to it, which is the same path
 * the RTC's own reading takes rather than a second copy of that arithmetic.
 *
 * @param epoch Seconds since 1970-01-01 UTC.
 * @param out Receives the broken-down time.
 */
static inline void esd_time_from_epoch(uint32_t epoch, esd_time_t *out) {
    int days = (int)(epoch / ESD_SECS_PER_DAY);
    uint32_t rem = epoch % ESD_SECS_PER_DAY;
    int y, m, d;

    esd_civil_from_days(days, &y, &m, &d);

    out->year = (uint16_t)y;
    out->month = (uint8_t)m;
    out->day = (uint8_t)d;
    out->hour = (uint8_t)(rem / 3600u);
    out->minute = (uint8_t)((rem % 3600u) / 60u);
    out->second = (uint8_t)(rem % 60u);
    out->tz_offset_hours = 0;
}

#endif // ESDTIME_H
