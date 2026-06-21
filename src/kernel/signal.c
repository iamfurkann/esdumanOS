#include "signal.h"
#include "stdio.h" // printk için

static signal_t signal_table[MAX_SIGNALS];

void init_signals(void) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        signal_table[i].handler = 0;
        signal_table[i].delay_ticks = 0;
        signal_table[i].is_scheduled = 0;
    }
}

void register_signal(int sig_num, signal_handler_t handler) {
    if (sig_num >= 0 && sig_num < MAX_SIGNALS) {
        signal_table[sig_num].handler = handler;
    }
}

void schedule_signal(int sig_num, uint32_t delay_ticks) {
    if (sig_num >= 0 && sig_num < MAX_SIGNALS && signal_table[sig_num].handler != 0) {
        signal_table[sig_num].delay_ticks = delay_ticks;
        signal_table[sig_num].is_scheduled = 1;
    }
}

void signal_tick_handler(void) {
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (signal_table[i].is_scheduled) {
            if (signal_table[i].delay_ticks > 0) {
                signal_table[i].delay_ticks--;
            } else {
                
                signal_table[i].is_scheduled = 0; 
                if (signal_table[i].handler) {
                    signal_table[i].handler(); 
                }
            }
        }
    }
}