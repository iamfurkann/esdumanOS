/**
 * @file stat.c
 * @brief Prints a file's metadata: size, type, owner, mode, times and storage form.
 *
 * The mode, the group and the two timestamps arrived with the v0.9.0 disk format,
 * and v0.9.1 made the mode decide access rather than merely be recorded. It is
 * printed in octal because that is how a mode is read and written - `ls -l` is
 * where the rwx string belongs, and printing both in both places would be two
 * renderings to keep in agreement.
 *
 * Times are shown as dates rather than as the counts they are stored as. The
 * conversion is esdtime.h's, the same one the kernel stamped them with - it is
 * header-only and pure, so this gets it without a link step, exactly as
 * /bin/edit gets the edit buffer.
 *
 * They are stored in UTC and shown in local time, and both halves of that are
 * deliberate. An epoch that is not UTC is not an epoch: two files stamped either
 * side of a timezone change would compare by what somebody's clock said rather
 * than by when it happened, which is the one thing a stored timestamp exists to
 * get right. Nobody reads their own files in UTC, though, so the offset the
 * system is set to goes back on for display and is printed alongside - the same
 * shape date(1) uses, and unambiguous either way.
 */
#include "syscall.h"
#include "stat.h"
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
    syscall(SYSCALL_WRITE, 1, (int)str, len);
}

/**
 * @brief Prints a newline character to the standard output
 */
void print_newline(void) {
    syscall(SYSCALL_WRITE, 1, (int)"\n", 1);
}

/**
 * @brief Prints an unsigned value in decimal
 * @param value The number to print
 */
void print_uint(unsigned int value) {
    char buf[12];
    int i = 0;

    if (value == 0) { print("0"); return; }

    while (value > 0) { buf[i++] = (char)('0' + (value % 10)); value /= 10; }

    char out[12];
    int j = 0;
    while (i > 0) { out[j++] = buf[--i]; }
    out[j] = '\0';

    print(out);
}

/**
 * @brief Prints a value in octal, which is how a mode is read.
 * @param value The mode bits.
 */
void print_octal(unsigned int value) {
    char buf[12];
    char out[13];
    int i = 0, j = 0;

    if (value == 0) { print("0"); return; }

    while (value > 0) { buf[i++] = (char)('0' + (value & 7u)); value >>= 3; }

    out[j++] = '0';
    while (i > 0) { out[j++] = buf[--i]; }
    out[j] = '\0';
    print(out);
}

/**
 * @brief Prints a number as exactly two digits, so a date lines up.
 * @param value 0 to 99.
 */
void print_two(unsigned int value) {
    char out[3];

    out[0] = (char)('0' + (value / 10u) % 10u);
    out[1] = (char)('0' + value % 10u);
    out[2] = '\0';
    print(out);
}

/**
 * @brief Hours the system's clock is ahead of UTC, learned once in main().
 */
int tz_offset = 0;

/**
 * @brief Prints a timezone offset the way a date does: a sign and two digits.
 * @param hours Hours ahead of UTC, negative for behind.
 */
void print_offset(int hours) {
    if (hours < 0) { print("-"); hours = -hours; }
    else print("+");

    print_two((unsigned int)hours);
}

/**
 * @brief Prints a stored timestamp as a local date.
 *
 * Zero is printed as a dash rather than as 1970. A file whose timestamp was
 * never set has no time, and the start of the epoch is a real instant that would
 * read as one - which is what the root directory reports, having never been
 * created.
 *
 * The offset is applied to the count before it is broken down rather than to the
 * fields afterwards, so a time that crosses midnight takes its date with it and
 * there is no second copy of the carry arithmetic to get wrong.
 *
 * @param epoch Seconds since the Unix epoch, UTC.
 */
void print_time(unsigned int epoch) {
    esd_time_t t;
    int shift = tz_offset * 3600;

    if (epoch == 0) { print("-"); return; }

    /* A negative offset cannot be allowed to wrap the count round; no real
     * timestamp is anywhere near the epoch, but a clamp is one comparison. */
    if (shift < 0 && (unsigned int)(-shift) > epoch) epoch = 0;
    else epoch = (unsigned int)((int)epoch + shift);

    esd_time_from_epoch(epoch, &t);
    print_uint(t.year); print("-");
    print_two(t.month); print("-");
    print_two(t.day); print(" ");
    print_two(t.hour); print(":");
    print_two(t.minute); print(":");
    print_two(t.second); print(" ");
    print_offset(tz_offset);
}

/**
 * @brief Main entry point for the application
 *
 * The whole argument string is the path, as the other tools here treat it. Since
 * v0.3.1 the shell passes it through unchanged, so a bare name resolves against
 * the working directory the kernel is holding for this process.
 */
void main(void) {
    char args_buf[128];
    for (int k = 0; k < 128; k++) args_buf[k] = '\0';
    syscall(SYSCALL_GET_ARGS, (int)args_buf, 0, 0);

    if (args_buf[0] == '\0') {
        /*
         * Exit 1, like the failure below. This used to exit 0, which reported a
         * usage error as success - invisible until exit statuses started
         * reaching the shell, and then immediately visible as
         * "stat && echo CHAINED" printing CHAINED with no file named.
         */
        print("stat: no file given"); print_newline();
        syscall(SYSCALL_EXIT, 1, 0, 0);
        while (1);
    }

    /*
     * The offset comes from the clock rather than from a constant here. A local
     * reading carries the offset it was adjusted by, which is the only thing this
     * needs and the only place a program can learn it - there is no syscall that
     * reports the timezone on its own.
     */
    esd_time_t now;
    if (syscall(SYSCALL_TIME, (int)&now, 0, 0) == 0) tz_offset = now.tz_offset_hours;

    esd_stat_t st;
    int res = syscall(SYSCALL_STAT, (int)args_buf, (int)&st, 0);

    if (res < 0) {
        print("stat: cannot stat '"); print(args_buf); print("'"); print_newline();
        syscall(SYSCALL_EXIT, 1, 0, 0);
        while (1);
    }

    print("  File: "); print(args_buf); print_newline();

    print("  Type: ");
    print(st.st_type == 1 ? "directory" : "regular file");
    print_newline();

    print("  Size: "); print_uint(st.st_size); print(" bytes"); print_newline();

    /*
     * Reported separately because they differ whenever the file is stored
     * encrypted: the on-disk form carries an IV, a header and padding on top of
     * the bytes a read() returns. Printing only one of them would make the
     * other one look wrong.
     */
    print("  Disk: "); print_uint(st.st_disk_size);
    print(st.st_encrypted ? " bytes (encrypted)" : " bytes");
    print_newline();

    print(" Inode: "); print_uint(st.st_ino); print_newline();
    print(" Owner: "); print_uint(st.st_uid);
    print(":"); print_uint(st.st_gid); print_newline();

    /* Stored, reported, and not yet consulted by anything - see the file note. */
    print("  Mode: "); print_octal(st.st_mode);
    print(" (stored, not yet enforced)"); print_newline();

    print("Modify: "); print_time(st.st_mtime); print_newline();
    print("Create: "); print_time(st.st_ctime); print_newline();

    syscall(SYSCALL_EXIT, 0, 0, 0);
    while (1);
}
