#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"

#define MAX_SIGNALS 32

/* Sinyal tetiklendiğinde çalışacak fonksiyonun (Callback) tipi */
typedef void (*signal_handler_t)(void);

typedef struct {
    signal_handler_t handler; /* Çalışacak fonksiyonun adresi */
    uint32_t delay_ticks;     /* Geri sayım sayacı */
    uint32_t is_scheduled;         /* Aktif olarak zamanlandı mı? */
} signal_t;

void init_signals(void);
void register_signal(int sig_num, signal_handler_t handler);
void schedule_signal(int sig_num, uint32_t delay_ticks);
void signal_tick_handler(void);
void alarm_demo_callback(void);

#endif