#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

//DEFCON
typedef enum {
    SEC_LEVEL_NORMAL          = 0, // Standart operasyon (Şifreleme isteğe bağlı)
    SEC_LEVEL_LOCKDOWN        = 1, // Yeni görev başlatılamaz, terminal kısıtlanır
    SEC_LEVEL_CRYPTO_ENFORCED = 2, // VFS üzerindeki TÜM okuma/yazmalar şifrelenmek ZORUNDA
    SEC_LEVEL_IMMUTABLE       = 3  // Diske yazma tamamen kapatılır, Kernel "Read-Only" moduna geçer
} security_level_t;

extern security_level_t current_sec_level;

void set_security_level(security_level_t level);

#endif