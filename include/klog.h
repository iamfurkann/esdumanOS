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
 * @brief Handles critical kernel failures.
 * 
 * Logs the provided reason and halts the system execution, typically used 
 * when an unrecoverable error occurs.
 * 
 * @param reason The cause of the panic.
 */
void kernel_panic(const char *reason);

#endif // KLOG_H