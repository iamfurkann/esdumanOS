/**
 * @file date.c
 * @brief Prints the current date and time
 */

#include "esdtime.h"

/**
 * @brief Invokes a system call
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return Return value from the system call
 */
int syscall(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (num), "b" (arg1), "c" (arg2), "d" (arg3) : "memory");
    return ret;
}

/**
 * @brief Prints a string to the standard output
 * @param str The null-terminated string to print
 */
void print(const char *str) {
    int len = 0;
    while(str[len]) len++;
    syscall(4, 1, (int)str, len);
}

/**
 * @brief Prints a newline character to the standard output
 */
void print_newline() {
    syscall(4, 1, (int)"\n", 1);
}

/**
 * @brief Prints a number in a fixed number of digits, zero-padded.
 *
 * @param value Number to print.
 * @param digits How many digits to emit.
 */
static void print_padded(int value, int digits) {
    char out[8];

    for (int i = digits - 1; i >= 0; i--) {
        out[i] = (char)('0' + (value % 10));
        value /= 10;
    }
    syscall(4, 1, (int)out, digits);
}

static const char *month_names[13] = {
    "???", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/**
 * @brief Main entry point for the application
 */
/**
 * @brief Reads exactly "YYYY-MM-DD HH:MM:SS", and nothing looser.
 *
 * Fixed width and fixed separators, because a clock is the wrong place to guess.
 * "2030-1-1" could mean the first of January and could mean somebody stopped
 * typing, and a date accepted on a guess is a date every file written afterwards
 * is stamped with.
 *
 * Whether the numbers make a real date is the kernel's to say - it has the
 * leap-year rule and the month lengths - so this checks the shape and leaves the
 * meaning alone.
 *
 * @param s Text to read.
 * @param out Receives the fields; the offset is left at zero for the caller.
 * @return 1 when the whole string was consumed in the expected shape.
 */
static int parse_datetime(const char *s, esd_time_t *out) {
    static const char sep[6] = { '-', '-', ' ', ':', ':', '\0' };
    static const int width[6] = { 4, 2, 2, 2, 2, 2 };
    int v[6];
    int i = 0;

    for (int f = 0; f < 6; f++) {
        int n = 0;

        for (int d = 0; d < width[f]; d++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            n = n * 10 + (s[i] - '0');
            i++;
        }
        v[f] = n;

        if (sep[f] != '\0') {
            if (s[i] != sep[f]) return 0;
            i++;
        }
    }
    if (s[i] != '\0') return 0;

    out->year   = (uint16_t)v[0];
    out->month  = (uint8_t)v[1];
    out->day    = (uint8_t)v[2];
    out->hour   = (uint8_t)v[3];
    out->minute = (uint8_t)v[4];
    out->second = (uint8_t)v[5];
    out->tz_offset_hours = 0;
    return 1;
}

void main(void) {
    /*
     * This used to print a string compiled into the binary - the same date and
     * time on every boot, in every year, whatever the clock said. The clock was
     * readable from Ring 0 only: it drew the status bar and nothing else. The
     * TIME syscall is what changed that.
     */
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(42, (int)args_buf, 0, 0); // SYSCALL_GET_ARGS

    /*
     * "-u" asks for UTC. The shift is done by the kernel rather than here: moving
     * a time between zones means the calendar carry, and a second copy of that
     * arithmetic in user space is exactly what this release was written to avoid.
     */
    int want_utc = (args_buf[0] == '-' && args_buf[1] == 'u' &&
                    (args_buf[2] == '\0' || args_buf[2] == ' '));

    esd_time_t now;
    int status = 0;

    /*
     * "-s YYYY-MM-DD HH:MM:SS" sets the clock, in local time - which is the time
     * the line above prints, so what you read is what you type back.
     *
     * The offset is read from the clock rather than assumed, and handed to the
     * kernel on the struct: the kernel takes it back out and stores UTC, because
     * that is what the chip holds. Doing the subtraction here would be a second
     * copy of the calendar carry, which is the thing this program's own comment
     * about -u already refuses to do.
     */
    if (args_buf[0] == '-' && args_buf[1] == 's' && args_buf[2] == ' ') {
        esd_time_t want;

        if (syscall(55, (int)&now, 0, 0) != 0) {   // SYSCALL_TIME, for the offset
            print("date: cannot read the clock"); print_newline();
            syscall(1, 1, 0, 0);
            while (1);
        }

        if (!parse_datetime(&args_buf[3], &want)) {
            print("date: expected -s \"YYYY-MM-DD HH:MM:SS\""); print_newline();
            syscall(1, 1, 0, 0);
            while (1);
        }
        want.tz_offset_hours = now.tz_offset_hours;

        int rc = syscall(66, (int)&want, 0, 0);    // SYSCALL_SETTIME
        if (rc != 0) {
            print(rc == -1 ? "date: only root can set the clock"
                           : "date: that is not a date this clock can hold");
            print_newline();
            syscall(1, 1, 0, 0);
            while (1);
        }
        syscall(1, 0, 0, 0);
        while (1);
    }

    if (syscall(55, (int)&now, want_utc, 0) != 0) {   // SYSCALL_TIME
        print("date: cannot read the clock"); print_newline();
        status = 1;
    } else {
        const char *month = (now.month >= 1 && now.month <= 12)
                          ? month_names[now.month] : month_names[0];

        print(month);
        print(" ");
        print_padded(now.day, 2);
        print(" ");
        print_padded(now.year, 4);
        print(" ");
        print_padded(now.hour, 2);
        print(":");
        print_padded(now.minute, 2);
        print(":");
        print_padded(now.second, 2);

        /*
         * The offset is reported rather than a zone name. There is no timezone
         * database here and nothing that could turn +3 into a name that would be
         * right all year - see /etc/timezone, which holds the same number for the
         * same reason.
         */
        print(" UTC");
        if (now.tz_offset_hours != 0) {
            print(now.tz_offset_hours > 0 ? "+" : "-");
            print_padded(now.tz_offset_hours > 0 ? now.tz_offset_hours
                                                 : -now.tz_offset_hours, 1);
        }

        print_newline();
    }

    syscall(1, status, 0, 0); // EXIT
    while(1);
}
