#ifndef KLOG_H
#define KLOG_H

#include "types.h"

// Log Seviyeleri (Syslog standardı)
#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_WARN     2
#define LOG_LEVEL_ERROR    3
#define LOG_LEVEL_CRITICAL 4

// Sistemin genel log filtresi (Bundan düşük seviyeliler ekrana basılmaz)
extern int current_log_level;

// Merkezi Loglama Fonksiyonu
void klog(int level, const char *module, const char *message);
void klog_int(int level, const char *module, const char *message, int val);
void klog_hex(int level, const char *module, const char *message, uint32_t hex_val);

// Hayati hatalar için (Sistemi durdurur)
void kernel_panic(const char *reason);

#endif // KLOG_H