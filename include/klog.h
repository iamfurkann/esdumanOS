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
 * @brief Bytes the log ring holds before it begins overwriting.
 *
 * Part of the contract now that it wraps: a reader cannot see more than this,
 * however much has been written.
 */
#define KLOG_BUF_SIZE 8192

/**
 * @brief Appends one character to the log ring.
 *
 * The low-level writer everything else is built on. printk() used to call this
 * for every character it printed, which is how the boot banner came to compete
 * with real records for the space; klog() feeds it a composed line at a time now.
 *
 * @param c Character to record.
 */
void klog_write_char(char c);

/**
 * @brief Writes the whole log to the screen.
 *
 * The counterpart to klog_read(): this one goes to the terminal directly and so
 * cannot be piped or redirected, which is why DMESG grew a buffer form.
 */
void dump_klog(void);

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
 * @brief Copies a slice of the kernel log into a caller-supplied buffer.
 *
 * The counterpart to dump_klog(), which writes to the screen and therefore
 * cannot be piped or redirected. Reading a slice at a time keeps the position in
 * the caller's loop, which is what lets each write block and restart on its own.
 *
 * The window slides once the ring has wrapped: offset 0 is the oldest byte still
 * held, not the oldest ever written. A reader looping until this returns 0
 * terminates either way, because what is available is capped by the buffer.
 *
 * @param dst    Kernel buffer receiving the bytes.
 * @param max    Capacity of dst.
 * @param offset Byte position within what is currently held.
 * @return Bytes copied; 0 once offset reaches the end.
 */
int klog_read(char *dst, int max, int offset);

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