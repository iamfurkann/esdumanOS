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

#endif // ESDTIME_H
