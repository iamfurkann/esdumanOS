#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"
#include "registers.h"

/**
 * @brief Maximum number of supported signals in the system.
 */
#define MAX_SIGNALS 32

/*
 * The two signals that terminate a process which has not handled them.
 *
 * Both numbers were already in use - kill(1) sent a bare 9 with a comment
 * explaining what it meant - but nothing named them, so the default action added
 * in the kernel had nothing to agree with. Numbered as POSIX does, which is what
 * the shell's "terminated by signal" reporting assumes.
 */
#define SIG_KILL  9
#define SIG_TERM 15

/*
 * Sent to every process in the foreground group when the user presses Ctrl-C.
 *
 * The keyboard has folded Ctrl-letter combinations into control bytes since
 * v0.5.3, so Ctrl-C has been arriving as a 0x03 in the input ring all along and
 * being handed to whatever happened to be reading as ordinary data. What was
 * missing was not the key but somebody to send it to: interrupting one process
 * is not what Ctrl-C means, and until a process could belong to a group there
 * was no way to name everything the user had started with one command.
 *
 * Terminates by default, and catchable - a program with unsaved work is exactly
 * the kind that wants to hear about this rather than be stopped by it.
 */
#define SIG_INT   2

/*
 * Sent to a process that writes to a pipe no longer being read.
 *
 * pipe_write() has refused such a write since v0.5.2, but refusing it only
 * produced a return value, and nothing in userland checks one: printk() does
 * not, and neither does any of the /bin tools. A stage whose reader had died
 * therefore ran to the end of its input with every write silently failing.
 * Terminating the writer is what makes a pipeline stop when its consumer does.
 */
#define SIG_PIPE 13

/*
 * Sent to every process in the foreground group when the user presses Ctrl-Z.
 *
 * Stops the target by default rather than terminating it: the program keeps its
 * memory, its descriptors and the instruction it was on, and can be put back
 * exactly where it was with SIG_CONT. That is the whole point - a job the user
 * wants out of the way for a moment is not a job they want to lose.
 *
 * Catchable, and therefore declinable, which is load-bearing. The shell runs a
 * foreground program with exec() and init runs the shell the same way, so both
 * are in the group the terminal is pointing at when Ctrl-Z arrives; a signal
 * neither of them could refuse would stop the session itself. This is why there
 * is no SIGSTOP here: an uncatchable stop has nobody to exempt init.
 */
#define SIG_TSTP 20

/*
 * Puts a stopped process back to work.
 *
 * Acted on where the signal is sent rather than where it is delivered, because a
 * stopped task is by definition not running and will never reach a delivery
 * point of its own. It is a no-op against a task that is not stopped.
 *
 * A process that registered a handler is told as well, once it is running again.
 * The default action needs no delivery - being continued is the whole of it -
 * but a full-screen program has to redraw what the shell wrote over its display
 * while it was away, and there is no other moment it could learn to.
 *
 * Numbered as Linux does on i386, which is also what the shell writes out as a
 * literal - these programs are freestanding and cannot include this header.
 */
#define SIG_CONT 18

/*
 * Sent to a process whose alarm() has come due.
 *
 * Terminates by default, which is POSIX's choice and is the useful one: the
 * call exists to put a bound on something, and a bound whose default is to be
 * ignored bounds nothing. A program that wants to survive its own alarm says so
 * by registering a handler, and that is the arrangement every timeout in every
 * Unix program is built on.
 *
 * Number 14, as Linux does on i386 - the same convention SIG_CONT and SIG_TSTP
 * follow here, and the reason it matters is that the shell writes signal numbers
 * out as literals: these programs are freestanding and cannot include this
 * header.
 *
 * Delivered by expire_alarms() from schedule(), not from the timer interrupt.
 * The kernel timer slots in signal.c could not carry this - they are global,
 * hold a bare void(*)(void), and carry no pid to signal - which is the same
 * reason sleep() keeps its deadline in the PCB rather than in a slot.
 */
#define SIG_ALRM 14

/*
 * Sent to a process that tries to read the terminal from the background.
 *
 * There is one keyboard and one input ring, and every blocked reader is woken
 * when a key arrives - so a background job reading standard input does not share
 * the keyboard with the shell, it races it for every keystroke. Half the line the
 * user types goes to the job and half to the prompt, and the shell is left
 * unusable by a job the user deliberately put out of the way.
 *
 * Stops by default, like SIG_TSTP, which is what makes the answer recoverable
 * rather than fatal: the job parks, `jobs` shows it stopped, and `fg` gives it
 * the terminal it was asking for. The read re-runs when it continues.
 */
#define SIG_TTIN 21

/*
 * Disposition meaning "discard this signal", stored in signal_handlers[] where a
 * handler address would go.
 *
 * That array carried two states - 0 for the default action, anything else for a
 * user handler - and a third was needed: a process has to be able to decline a
 * signal that would otherwise kill it. The value is the one POSIX uses for
 * SIG_IGN, and it is not a reachable user address, so it cannot collide with a
 * real handler.
 *
 * Kept in signal_handlers[] rather than a separate mask so that inheritance and
 * reset come for free: inherit_pcb_state() already copies the array on fork and
 * create_process() already zeroes it, which is exactly the POSIX rule that an
 * ignore survives fork and a fresh program image starts with defaults.
 */
#define SIG_IGN 1u

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

/*
 * alarm_demo_callback() was declared here and is gone.
 *
 * It was the callback kernel_main() registered on timer slot 1, and the only
 * thing that armed that slot was SYSCALL_ALARM - which took no duration, sent no
 * signal, and printed a green line from inside the kernel three seconds later.
 * v1.0.0 made that call a real alarm(seconds) delivering SIG_ALRM to the caller,
 * and a per-process deadline in the PCB is what serves it; the demo had nothing
 * left to demonstrate.
 *
 * The timer slots themselves stay. They are still the mechanism behind the
 * bottom half, and test_signal.c exercises them directly through its own slot
 * rather than through a production caller that only ever existed as a display.
 */

// --- Added by Refactor Script ---
extern void restore_signal_context(arch_regs_t *regs);

#endif