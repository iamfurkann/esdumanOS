#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"
#include "registers.h"

/**
 * @brief Maximum number of supported signals in the system.
 */
#define MAX_SIGNALS 32

/**
 * @brief Type definition for a signal handler callback function.
 * This function is executed when the corresponding signal is triggered.
 */
typedef void (*signal_handler_t)(void);

/*
 * signal_t and init_signals() used to be declared here. Both were dead:
 * signal.c defines its own kernel_timer_t (with a uint32_t handler_addr rather
 * than a function pointer) and nothing referenced signal_t; init_signals() was
 * renamed to init_kernel_timers() and the declaration was left behind, pointing
 * at a function that exists nowhere. The comment block below documents fixing
 * three other renames in this header and missed this one.
 */

/*
 * These three were declared here under their pre-rename names - register_signal,
 * schedule_signal and signal_tick_handler - long after signal.c had renamed them.
 * Three translation units included this header and so were compiled against an
 * API that existed nowhere; the real schedule_kernel_timer() meanwhile had a
 * second, invented declaration in process.h with different parameters AND a
 * different return type, which is what its only caller was actually built
 * against. The names and signatures below are the ones signal.c defines.
 */

/**
 * @brief Registers a callback for a kernel timer slot.
 * @param timer_id Slot to register, 0 to MAX_SIGNALS-1.
 * @param handler Callback invoked once the delay expires.
 */
void register_kernel_timer(int timer_id, signal_handler_t handler);

/**
 * @brief Arms a kernel timer slot.
 *
 * Silently does nothing when timer_id is out of range or no handler has been
 * registered for it, so a caller that passes the arguments in the wrong order
 * gets no diagnostic - see the note above.
 *
 * @param timer_id Slot to arm; must already have a handler.
 * @param delay_ticks Timer ticks before the callback becomes due; TIMER_HZ ticks
 *                    is one second.
 */
void schedule_kernel_timer(int timer_id, uint32_t delay_ticks);

/**
 * @brief Counts down every armed kernel timer. Called from the timer interrupt.
 *
 * Only decrements; the handlers themselves run later from
 * process_pending_kernel_timers(), off the interrupt path.
 */
void kernel_timer_tick_handler(void);

/**
 * @brief Runs the callbacks of every kernel timer whose delay has expired.
 */
void process_pending_kernel_timers(void);

/**
 * @brief Demo callback function to test the alarm signal.
 */
void alarm_demo_callback(void);


// --- Added by Refactor Script ---
extern void restore_signal_context(arch_regs_t *regs);

#endif