#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"

/**
 * @brief Maximum number of supported signals in the system.
 */
#define MAX_SIGNALS 32

/**
 * @brief Type definition for a signal handler callback function.
 * This function is executed when the corresponding signal is triggered.
 */
typedef void (*signal_handler_t)(void);

/**
 * @brief Structure representing a registered signal.
 */
typedef struct {
    signal_handler_t handler; /* Address of the function to be executed */
    uint32_t delay_ticks;     /* Countdown timer for the signal trigger */
    uint32_t is_scheduled;    /* Is the signal actively scheduled? */
} signal_t;

/**
 * @brief Initializes the signal handling subsystem.
 */
void init_signals(void);

/**
 * @brief Registers a callback handler for a specific signal.
 * @param sig_num Signal number to register.
 * @param handler Callback function pointer.
 */
void register_signal(int sig_num, signal_handler_t handler);

/**
 * @brief Schedules a signal to be triggered after a specific delay.
 * @param sig_num Signal number to schedule.
 * @param delay_ticks Number of timer ticks before the signal triggers.
 */
void schedule_signal(int sig_num, uint32_t delay_ticks);

/**
 * @brief Kernel timer tick handler for signals.
 * Decrements active signal delays and triggers handlers if they expire.
 */
void signal_tick_handler(void);

/**
 * @brief Demo callback function to test the alarm signal.
 */
void alarm_demo_callback(void);

#endif