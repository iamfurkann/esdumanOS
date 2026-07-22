// [REFACTOR]: Renamed from signal.c logic to Kernel Timer Callbacks
#include "signal.h"
#include "stdio.h"

// Renamed from signal_t
typedef struct {
    uint32_t handler_addr; // Store address instead of direct function pointer
    uint32_t delay_ticks;
    int is_scheduled;
} kernel_timer_t;

static kernel_timer_t kernel_timers[MAX_SIGNALS];

// Renamed from init_signals
void init_kernel_timers(void) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        kernel_timers[i].handler_addr = 0;
        kernel_timers[i].delay_ticks = 0;
        kernel_timers[i].is_scheduled = 0;
    }
}

// Renamed from register_signal
void register_kernel_timer(int timer_id, void (*handler)(void)) {
    if (timer_id >= 0 && timer_id < MAX_SIGNALS) {
        kernel_timers[timer_id].handler_addr = (uint32_t)handler;
    }
}

// Renamed from schedule_signal
void schedule_kernel_timer(int timer_id, uint32_t delay_ticks) {
    if (timer_id >= 0 && timer_id < MAX_SIGNALS && kernel_timers[timer_id].handler_addr != 0) {
        kernel_timers[timer_id].delay_ticks = delay_ticks;
        kernel_timers[timer_id].is_scheduled = 1;
    }
}

// [SECURITY PATCH]: Bottom-half processing.
// Instead of running the handler inside the interrupt, we just mark it as ready (delay=0).
// The main kernel loop or scheduler will execute it safely later.
void kernel_timer_tick_handler(void) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (kernel_timers[i].is_scheduled) {
            if (kernel_timers[i].delay_ticks > 0) {
                kernel_timers[i].delay_ticks--;
            } 
            // If it hits 0, it stays 0 and remains scheduled.
            // The process_pending_kernel_timers function will pick it up.
        }
    }
}

// [NEW]: Bottom-half executor (to be called by the scheduler)
void process_pending_kernel_timers(void) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (kernel_timers[i].is_scheduled && kernel_timers[i].delay_ticks == 0) {
            kernel_timers[i].is_scheduled = 0; // Clear before running to allow re-scheduling
            
            if (kernel_timers[i].handler_addr) {
                void (*func_ptr)(void) = (void (*)(void))kernel_timers[i].handler_addr;
                func_ptr(); 
            }
        }
    }
}

void alarm_demo_callback(void) {
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    printk("\n[KERNEL TIMER] 3 Saniyelik Alarm Tetiklendi! (Bottom-half)\n");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}