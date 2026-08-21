#ifndef KLOG_H
#define KLOG_H

#include "types.h"

/**
 * @brief Log levels following the Syslog standard.
 *
 * Defines the severity levels for kernel logging, allowing filtering of messages
 * based on their critical nature.
 */
#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_WARN     2
#define LOG_LEVEL_ERROR    3
#define LOG_LEVEL_CRITICAL 4

/**
 * @brief Global system log filter.
 *
 * Determines the minimum severity level required for a log message to be
 * displayed. Messages with a lower severity than this value are ignored.
 */
extern int current_log_level;

/**
 * @brief Longest module name a record keeps, including the terminator.
 */
#define KLOG_MODULE_MAX 16

/**
 * @brief Longest message a record keeps, including the terminator.
 */
#define KLOG_TEXT_MAX 144

/**
 * @brief Records the ring holds before it begins overwriting.
 *
 * Part of the contract: a reader cannot see more than this, however much has
 * been written. It was 8 KB of flat text, which held roughly 130 lines and could
 * answer nothing about them; this is four times the count and every one of them
 * is structured. At 176 bytes a record that is about 88 KB of kernel data, which
 * on a 128 MB machine buys a great deal for very little.
 */
#define KLOG_RECORDS 512

/**
 * @brief One log record.
 *
 * The log used to be a byte ring holding composed text lines, which meant every
 * question about a record - when did this happen, how many did we lose, show me
 * only the errors - was a question about parsing a string, and so went unasked.
 * A record carries its own answers.
 *
 * `ticks` rather than a wall-clock time. It is monotonic and correct even when
 * the RTC is not, which is the property a log needs; rendering it as a
 * human-readable date would need a boot-time epoch this kernel does not keep,
 * and printing one derived from a clock that can be wrong is worse than printing
 * an offset that cannot be.
 */
typedef struct {
    uint32_t seq;                     /**< Monotonic; 0 means the slot is empty. */
    uint32_t ticks;                   /**< timer_get_ticks() when it was recorded. */
    /**
     * Author of a /dev/kmsg record; 0 for the kernel.
     *
     * As wide as the process control block's uid and not a byte narrower. A
     * uint8_t looked like plenty next to a handful of accounts and would have
     * recorded this system's own `esduman`, uid 1000, as uid 232 - a record
     * attributing itself to a user who does not exist, which is worse than one
     * that declines to say.
     */
    uint32_t uid;
    uint8_t  level;
    uint8_t  from_user;               /**< Non-zero when it arrived through /dev/kmsg. */
    uint8_t  text_len;
    uint8_t  reserved;
    char     module[KLOG_MODULE_MAX];
    char     text[KLOG_TEXT_MAX];
} klog_record_t;

/**
 * @brief Longest line a record renders to, including the terminator.
 */
#define KLOG_LINE_MAX 192

/**
 * @brief Centralized kernel logging function.
 *
 * Outputs a log message with a specified severity level and module name.
 *
 * @param level The severity level of the log message.
 * @param module The name of the subsystem or module generating the log.
 * @param message The text message to log.
 */
void klog(int level, const char *module, const char *message);

/**
 * @brief Logs a message with an integer value.
 *
 * @param level The severity level of the log message.
 * @param module The name of the subsystem or module generating the log.
 * @param message The text message to log.
 * @param val The integer value to append to the message.
 */
void klog_int(int level, const char *module, const char *message, int val);

/**
 * @brief Logs a message with a hexadecimal value.
 *
 * @param level The severity level of the log message.
 * @param module The name of the subsystem or module generating the log.
 * @param message The text message to log.
 * @param hex_val The hexadecimal value to append to the message.
 */
void klog_hex(int level, const char *module, const char *message, uint32_t hex_val);

/**
 * @brief Records a line in the log without printing it.
 *
 * For an event that already has a rendering on screen - the boot milestones
 * print a green "[OK] subsystem up" line of their own, and logging them through
 * klog() would print them a second time in a different shape.
 *
 * @param level Severity.
 * @param module Subsystem name.
 * @param message The text.
 */
void klog_record(int level, const char *module, const char *message);

/**
 * @brief Records a message that came from a user-space program.
 *
 * The write half of /dev/kmsg. Rate limited, because the caller is not the
 * kernel and a program in a loop would otherwise push every diagnostic record
 * out of the ring. Kernel records are deliberately *not* rate limited: a storm
 * of errors is exactly when the records matter, and the drop counter already
 * makes the loss visible.
 *
 * @param level Severity, clamped into range.
 * @param text  The message.
 * @param uid   The calling process's user id, kept with the record.
 * @return E_OK, or E_AGAIN when the caller is over its rate.
 */
int klog_user_record(int level, const char *text, uint32_t uid);

/**
 * @brief Number of records currently held.
 *
 * @return Held record count, never more than KLOG_RECORDS.
 */
uint32_t klog_held(void);

/**
 * @brief Sequence number of the oldest record still held.
 *
 * @return The oldest sequence number, or 0 when the log is empty.
 */
uint32_t klog_oldest_seq(void);

/**
 * @brief Sequence number the next record will be given.
 *
 * @return One past the newest record's sequence number.
 */
uint32_t klog_next_seq(void);

/**
 * @brief Records overwritten since boot.
 *
 * The byte ring could not answer this: it wrapped mid-line and nothing counted
 * what went. A reader that finds records missing between two reads can now say
 * so instead of silently showing a gap.
 *
 * @return Number of records the ring has discarded.
 */
uint32_t klog_dropped(void);

/**
 * @brief Looks up a record by sequence number.
 *
 * @param seq Sequence number to fetch.
 * @return The record, or 0 when that sequence is no longer held or never existed.
 */
const klog_record_t *klog_by_seq(uint32_t seq);

/**
 * @brief Renders one record as a human-readable line.
 *
 * The form `dmesg` prints: a monotonic timestamp, the level, the module and the
 * text. The timestamp is shown to hundredths because TIMER_HZ is 100 - printing
 * the six decimals Linux does would be printing precision this clock does not
 * have.
 *
 * @param rec Record to render.
 * @param dst Destination buffer.
 * @param max Capacity of @p dst.
 * @return Bytes written, not counting the terminator.
 */
int klog_format(const klog_record_t *rec, char *dst, int max);

/**
 * @brief Renders one record in the /dev/kmsg form.
 *
 * `level,seq,ticks,flag;module: text` - Linux's shape, with the machine-readable
 * fields ahead of the semicolon and the human text after it. A reader wanting
 * only errors reads the first field rather than parsing a prefix out of the
 * message, which is the whole reason the ring holds records.
 *
 * The flag distinguishes a record a program wrote from one the kernel produced.
 * A log that cannot tell those apart is a log a program can put words into the
 * kernel's mouth in.
 *
 * @param rec Record to render.
 * @param dst Destination buffer.
 * @param max Capacity of @p dst.
 * @return Bytes written, not counting the terminator.
 */
int klog_format_kmsg(const klog_record_t *rec, char *dst, int max);

/**
 * @brief Copies one rendered record into a caller-supplied buffer.
 *
 * The counterpart to dump_klog(), which writes to the screen and therefore
 * cannot be piped or redirected.
 *
 * **The index is a record, not a byte.** It used to be a byte offset into a flat
 * ring, which a record ring cannot honour: if a record is dropped between two
 * reads every byte position shifts and the reader is handed a torn line. Index 0
 * is the oldest record still held, and a caller walks forward one at a time.
 *
 * @param dst   Buffer receiving the rendered line.
 * @param max   Capacity of @p dst.
 * @param index Record position, counted from the oldest still held.
 * @return Bytes written; 0 once @p index reaches the end.
 */
int klog_read(char *dst, int max, int index);

/**
 * @brief Writes the whole log to the screen.
 *
 * Goes to the terminal directly and so cannot be piped or redirected, which is
 * why DMESG grew a buffer form. Kept for the callers that want the screen.
 */
void dump_klog(void);

/**
 * @brief Discards every held record.
 *
 * Sequence numbers are not reset: a reader holding a cursor from before the
 * clear must be able to tell that what it was pointing at is gone, rather than
 * be handed a different record wearing the same number.
 */
void klog_clear(void);

/**
 * @brief Sets the minimum severity that is recorded and printed.
 *
 * @param level New threshold; clamped into range.
 */
void klog_set_level(int level);

/**
 * @brief Reads the current severity threshold.
 *
 * @return The threshold.
 */
int klog_get_level(void);

/**
 * @brief Writes the log out to /var/log/kern.log, replacing it.
 *
 * Called at checkpoints - sync, halt, reboot - rather than per record, because
 * the on-disk format cannot append: a file is one AES-CBC blob authenticated
 * over its whole plaintext, so adding a line means rewriting all of it.
 *
 * @return E_OK, or a negative errno. A caller should not treat failure as fatal.
 */
int klog_persist(void);

/**
 * @brief Handles critical kernel failures.
 *
 * Logs the provided reason and halts the system execution, typically used
 * when an unrecoverable error occurs.
 *
 * @param reason The cause of the panic.
 */
void kernel_panic(const char *reason);

#endif // KLOG_H
